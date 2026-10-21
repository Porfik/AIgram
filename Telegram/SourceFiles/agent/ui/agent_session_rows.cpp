/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_session_rows.h"

#include "agent/agent_chat.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/agent_settings.h"
#include "agent/ui/agent_sessions_list.h"
#include "agent/ui/agent_spending_format.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/unread_badge_paint.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_agent.h"
#include "styles/style_agent_general.h"
#include "styles/style_agent_panel.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {

SessionRows::SessionRows(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope,
	bool compact)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope)
, _compact(compact)
, _st(compact ? st::agentPanelSessionRow : st::defaultDialogRow) {
	setMouseTracking(true);
	_manager->listChanges(
	) | rpl::filter([=](const Scope &changed) {
		return (changed == _scope);
	}) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	rpl::merge(
		Agent::ProvidersChanged(),
		_manager->exclusions().changes()
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	_manager->activityValue(
		_scope
	) | rpl::on_next([=] {
		refreshActivity();
	}, lifetime());
	rebuild();
}

rpl::producer<QString> SessionRows::openRequests() const {
	return _openRequests.events();
}

bool SessionRows::empty() const {
	return _rows.empty();
}

void SessionRows::rebuild() {
	_rows.clear();
	_selected.reset();
	_pressed.reset();
	_userpicLifetime.destroy();
	const auto showSpending = _manager->providers().showSpending;
	const auto owner = &_manager->session().data();
	for (const auto &info : _manager->list(_scope)) {
		auto &row = _rows.emplace_back();
		row.id = info.id;
		row.title.setText(
			st::semiboldTextStyle,
			(info.title.isEmpty()
				? tr::lng_agent_session_untitled(tr::now)
				: info.title));
		auto preview = info.preview;
		if (showSpending) {
			const auto spending = FormatSpending(UsageTotals(info.usage));
			if (!spending.isEmpty()) {
				preview = preview.isEmpty()
					? spending
					: (spending + u" · "_q + preview);
			}
		}
		row.preview.setText(st::dialogsTextStyle, preview);
		row.outgoing = info.previewOutgoing && !info.preview.isEmpty();
		row.date = ::Ui::FormatDialogsDate(
			base::unixtime::parse(TimeId(info.updated)));
		if (_compact) {
			continue;
		}
		for (const auto key : info.touched) {
			if (_manager->exclusions().excluded(key)) {
				continue;
			} else if (const auto peer = owner->peerLoaded(PeerId(key))) {
				row.userpic = peer;
				break;
			}
		}
		if (!row.userpic) {
			row.userpic = _manager->chat().user();
		}
		row.userpic->loadUserpic();
		_manager->session().changes().peerUpdates(
			row.userpic,
			Data::PeerUpdate::Flag::Photo
		) | rpl::on_next([=] {
			update();
		}, _userpicLifetime);
	}
	refreshActivity();
	resizeToWidth(width());
	update();
}

void SessionRows::refreshActivity() {
	for (auto &row : _rows) {
		const auto session = _manager->loaded(row.id);
		const auto &state = session ? session->state() : SessionState();
		row.working = (state.status == SessionStatus::Working)
			|| (state.status == SessionStatus::Compacting);
		row.pending = state.pendingCards;
	}
	update();
}

int SessionRows::resizeGetHeight(int newWidth) {
	return (_compact && _rows.empty())
		? st::agentSessionListEmptyHeight
		: int(_rows.size()) * _st.height;
}

void SessionRows::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	p.fillRect(clip, _compact ? st::windowBg : st::dialogsBg);
	if (_compact && _rows.empty()) {
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		p.drawText(
			QRect(0, 0, width(), st::agentSessionListEmptyHeight),
			Qt::AlignCenter,
			tr::lng_agent_sessions_empty(tr::now));
		return;
	}
	const auto height = _st.height;
	const auto from = std::max(clip.y() / height, 0);
	const auto till = std::min(
		(clip.y() + clip.height() + height - 1) / height,
		int(_rows.size()));
	for (auto i = from; i < till; ++i) {
		paintRow(p, _rows[i], i * height, (_selected == i));
	}
}

void SessionRows::paintUserpic(Painter &p, Row &row, int top) {
	const auto &st = _st;
	if (row.userpic->isSelf()) {
		::Ui::EmptyUserpic::PaintSavedMessages(
			p,
			st.padding.left(),
			top + st.padding.top(),
			width(),
			st.photoSize);
	} else {
		row.userpic->paintUserpicLeft(
			p,
			row.view,
			st.padding.left(),
			top + st.padding.top(),
			width(),
			st.photoSize);
	}
}

void SessionRows::paintRow(Painter &p, Row &row, int top, bool selected) {
	const auto &st = _st;
	if (selected) {
		p.fillRect(
			0,
			top,
			width(),
			st.height,
			_compact ? st::windowBgOver : st::dialogsBgOver);
	}
	if (row.userpic) {
		paintUserpic(p, row, top);
	}
	const auto trash = _compact && selected;
	if (trash) {
		st::agentSessionListDeleteIcon.paint(
			p,
			deleteIconRect(top).topLeft(),
			width());
	}
	const auto right = trash
		? (width()
			- deleteIconRect(top).x()
			+ st::agentSessionListDeleteTextGap)
		: st.padding.right();

	auto nameWidth = width() - st.nameLeft - right;
	if (!row.date.isEmpty() && !trash) {
		const auto dateWidth = st::dialogsDateFont->width(row.date);
		nameWidth -= dateWidth + st::dialogsDateSkip;
		p.setFont(st::dialogsDateFont);
		p.setPen(selected ? st::dialogsDateFgOver : st::dialogsDateFg);
		p.drawText(
			width() - st.padding.right() - dateWidth,
			(top
				+ st.nameTop
				+ st::semiboldFont->height
				- st::normalFont->descent),
			row.date);
	}
	if (row.outgoing && !row.working) {
		nameWidth -= st::dialogsSendStateSkip;
		const auto &icon = selected
			? st::dialogsReceivedIcon.over
			: st::dialogsReceivedIcon.icon;
		icon.paint(p, st.nameLeft + nameWidth, top + st.nameTop, width());
	}
	p.setPen(selected ? st::dialogsNameFgOver : st::dialogsNameFg);
	row.title.draw(p, {
		.position = { st.nameLeft, top + st.nameTop },
		.availableWidth = std::max(nameWidth, 0),
		.elisionLines = 1,
	});

	auto textWidth = width() - st.textLeft - right;
	if (row.pending && !trash) {
		auto style = ::Ui::UnreadBadgeStyle();
		style.selected = selected;
		style.align = style::al_right;
		const auto badgeTop = top
			+ st.textTop
			+ st::dialogsTextFont->ascent
			- st::dialogsUnreadFont->ascent
			- (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
		const auto badge = ::Ui::PaintUnreadBadge(
			p,
			QString::number(row.pending),
			width() - st.padding.right(),
			badgeTop,
			style);
		textWidth -= badge.width() + st::dialogsUnreadPadding;
	}
	textWidth = std::max(textWidth, 0);
	if (row.working || row.pending) {
		p.setFont(st::dialogsTextFont);
		p.setPen(selected
			? st::dialogsTextFgServiceOver
			: st::dialogsTextFgService);
		const auto text = row.pending
			? tr::lng_agent_waiting_card(tr::now)
			: (tr::lng_typing(tr::now) + QString::fromUtf8("\xe2\x80\xa6"));
		p.drawTextLeft(
			st.textLeft,
			top + st.textTop,
			width(),
			st::dialogsTextFont->elided(text, textWidth));
		return;
	}
	p.setPen(selected ? st::dialogsTextFgOver : st::dialogsTextFg);
	row.preview.draw(p, {
		.position = { st.textLeft, top + st.textTop },
		.availableWidth = textWidth,
		.elisionLines = 1,
	});
}

QRect SessionRows::deleteIconRect(int top) const {
	const auto &icon = st::agentSessionListDeleteIcon;
	return QRect(
		width() - st::agentSessionListDeleteRight - icon.width(),
		top + (_st.height - icon.height()) / 2,
		icon.width(),
		icon.height());
}

std::optional<int> SessionRows::rowAt(int y) const {
	const auto index = (y >= 0) ? (y / _st.height) : -1;
	if (index < 0 || index >= int(_rows.size())) {
		return std::nullopt;
	}
	return index;
}

void SessionRows::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressed = rowAt(e->pos().y());
	}
}

void SessionRows::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = base::take(_pressed);
	if (e->button() != Qt::LeftButton || !pressed) {
		return;
	} else if (rowAt(e->pos().y()) == pressed) {
		const auto id = _rows[*pressed].id;
		if (_compact
			&& deleteIconRect(*pressed * _st.height).contains(e->pos())) {
			ShowDeleteSessionBox(_controller, _manager, id);
			return;
		}
		_openRequests.fire_copy(id);
	}
}

void SessionRows::mouseMoveEvent(QMouseEvent *e) {
	const auto index = rowAt(e->pos().y());
	if (index != _selected) {
		_selected = index;
		setCursor(index ? style::cur_pointer : style::cur_default);
		update();
	}
}

void SessionRows::leaveEventHook(QEvent *e) {
	if (_selected) {
		_selected.reset();
		setCursor(style::cur_default);
		update();
	}
}

void SessionRows::contextMenuEvent(QContextMenuEvent *e) {
	const auto index = rowAt(e->pos().y());
	if (!index) {
		return;
	}
	_menu = base::make_unique_q<PopupMenu>(this, st::popupMenuWithIcons);
	const auto id = _rows[*index].id;
	const auto addAction = Menu::CreateAddActionCallback(_menu);
	addAction(tr::lng_agent_session_rename(tr::now), [=] {
		ShowRenameSessionBox(_controller, _manager, id);
	}, &st::menuIconEdit);
	addAction({
		.text = tr::lng_agent_session_delete(tr::now),
		.handler = [=] { ShowDeleteSessionBox(_controller, _manager, id); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	_menu->popup(e->globalPos());
}

} // namespace Agent::Ui
