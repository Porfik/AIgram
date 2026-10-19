/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_sessions_list.h"

#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "agent/agent_settings.h"
#include "agent/core/agent_record.h"
#include "agent/ui/agent_spending_format.h"
#include "agent/ui/agent_userpic.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/panel_animation.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_agent.h"
#include "styles/style_agent_general.h"
#include "styles/style_agent_panel.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {

void ShowRenameSessionBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &id) {
	const auto info = manager->info(id);
	if (!info) {
		return;
	}
	const auto current = info->title;
	controller->show(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_agent_session_rename_title());
		const auto input = box->addRow(
			object_ptr<InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_session_title_placeholder(),
				current));
		box->setFocusCallback([=] {
			input->setFocusFast();
			input->selectAll();
		});
		const auto save = [=] {
			const auto text = input->getLastText().trimmed();
			if (text.isEmpty() || text == current) {
				return;
			}
			manager->rename(id, text);
			box->closeBox();
		};
		input->submits() | rpl::on_next(save, box->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowDeleteSessionBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &id) {
	controller->show(MakeConfirmBox({
		.text = tr::lng_agent_session_delete_sure(tr::marked),
		.confirmed = [=](Fn<void()> close) {
			manager->remove(id);
			close();
		},
		.confirmText = tr::lng_box_delete(),
	}));
}

void ShowSessionMenu(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		not_null<QWidget*> button,
		const QString &id,
		base::unique_qptr<PopupMenu> &menu) {
	menu = base::make_unique_q<PopupMenu>(button, st::popupMenuWithIcons);
	const auto addAction = Menu::CreateAddActionCallback(menu);
	addAction(tr::lng_agent_session_rename(tr::now), [=] {
		ShowRenameSessionBox(controller, manager, id);
	}, &st::menuIconEdit);
	addAction({
		.text = tr::lng_agent_session_delete_current(tr::now),
		.handler = [=] { ShowDeleteSessionBox(controller, manager, id); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	menu->setForcedOrigin(PanelAnimation::Origin::TopRight);
	menu->popup(button->mapToGlobal(
		QPoint(button->width(), button->height())));
}

SessionsList::SessionsList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope) {
	setMouseTracking(true);
	_manager->listChanges(
	) | rpl::on_next([=](const Scope &scope) {
		if (scope == _scope) {
			rebuild();
		}
	}, lifetime());
	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	_manager->exclusions().changes(
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	rebuild();
}

rpl::producer<QString> SessionsList::openSessionRequests() const {
	return _openSessionRequests.events();
}

void SessionsList::rebuild() {
	_rows.clear();
	_selected.reset();
	_userpicLifetime.destroy();
	const auto list = _manager->list(_scope);
	_rows.reserve(list.size());
	constexpr auto kMaxUserpics = 3;
	for (const auto &info : list) {
		auto date = FormatDateTime(
			QDateTime::fromSecsSinceEpoch(info.updated));
		if (_manager->providers().showSpending) {
			const auto spending = FormatSpending(UsageTotals(info.usage));
			if (!spending.isEmpty()) {
				date += u" · "_q + spending;
			}
		}
		auto &row = _rows.emplace_back(Row{
			.id = info.id,
			.title = !info.title.isEmpty()
				? info.title
				: tr::lng_agent_session_untitled(tr::now),
			.date = std::move(date),
		});
		if (_scope.kind != ScopeKind::Account) {
			continue;
		}
		for (const auto key : info.touched) {
			if (row.userpics.size() == kMaxUserpics) {
				break;
			}
			if (_manager->exclusions().excluded(key)) {
				continue;
			}
			const auto peer = _manager->session().data().peerLoaded(
				PeerId(key));
			if (!peer) {
				continue;
			}
			peer->loadUserpic();
			row.userpics.push_back({ peer, {} });
		}
		for (const auto &userpic : row.userpics) {
			_manager->session().changes().peerUpdates(
				userpic.peer,
				Data::PeerUpdate::Flag::Photo
			) | rpl::on_next([=] {
				update();
			}, _userpicLifetime);
		}
	}
	resizeToWidth(width());
	update();
}

int SessionsList::rowTextLeft() const {
	const auto padding = st::agentSessionListRowPadding;
	if (_scope.kind != ScopeKind::Account) {
		return padding.left();
	}
	return padding.left()
		+ st::agentSessionListUserpicSize
		+ st::agentSessionListUserpicGap;
}

void SessionsList::paintUserpicStack(Painter &p, Row &row, int top) {
	const auto size = st::agentSessionListUserpicSize;
	const auto padding = st::agentSessionListRowPadding;
	const auto x = padding.left();
	const auto y = top + (st::agentSessionListRowHeight - size) / 2;
	if (row.userpics.empty()) {
		PaintAgentUserpic(p, x, y, width(), size);
		return;
	}
	auto left = x;
	for (auto &userpic : row.userpics) {
		if (userpic.peer->isSelf()) {
			Ui::EmptyUserpic::PaintSavedMessages(p, left, y, width(), size);
		} else {
			userpic.peer->paintUserpic(p, userpic.view, left, y, size, true);
		}
		left += size - st::agentSessionListUserpicShift;
	}
}

int SessionsList::resizeGetHeight(int newWidth) {
	return _rows.empty()
		? st::agentSessionListEmptyHeight
		: int(_rows.size()) * st::agentSessionListRowHeight;
}

void SessionsList::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(rect(), st::windowBg);
	if (_rows.empty()) {
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(
				0,
				0,
				width(),
				st::agentSessionListEmptyHeight),
			Qt::AlignCenter,
			tr::lng_agent_sessions_empty(tr::now));
		return;
	}
	const auto rowHeight = st::agentSessionListRowHeight;
	const auto padding = st::agentSessionListRowPadding;
	const auto textLeft = rowTextLeft();
	const auto textWidth = width() - textLeft - padding.right();
	const auto titleFont = st::agentSessionListTitleStyle.font;
	const auto dateFont = st::agentSessionListDateStyle.font;
	const auto dateTop = padding.top() + titleFont->height;
	const auto deleteSpace = st::agentSessionListDeleteRight
		+ st::agentSessionListDeleteIcon.width()
		+ st::agentSessionListDeleteTextGap;
	auto top = 0;
	auto index = 0;
	for (auto &row : _rows) {
		const auto hovered = (index == _selected);
		const auto rowTextWidth = hovered
			? std::max(textWidth - deleteSpace, 0)
			: textWidth;
		if (hovered) {
			p.fillRect(0, top, width(), rowHeight, st::windowBgOver);
		}
		if (_scope.kind == ScopeKind::Account) {
			paintUserpicStack(p, row, top);
		}
		p.setPen(st::windowFg);
		p.setFont(titleFont);
		p.drawText(
			QRect(
				textLeft,
				top + padding.top(),
				rowTextWidth,
				titleFont->height),
			Qt::AlignLeft,
			titleFont->elided(row.title, rowTextWidth));
		p.setPen(st::windowSubTextFg);
		p.setFont(dateFont);
		p.drawText(
			QRect(textLeft, top + dateTop, rowTextWidth, dateFont->height),
			Qt::AlignLeft,
			dateFont->elided(row.date, rowTextWidth));
		if (hovered) {
			st::agentSessionListDeleteIcon.paint(
				p,
				deleteIconRect(top).topLeft(),
				width());
		}
		top += rowHeight;
		++index;
	}
}

QRect SessionsList::deleteIconRect(int top) const {
	const auto &icon = st::agentSessionListDeleteIcon;
	return QRect(
		width()
			- st::agentSessionListDeleteRight
			- icon.width(),
		top + (st::agentSessionListRowHeight - icon.height()) / 2,
		icon.width(),
		icon.height());
}

void SessionsList::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto index = findRowIndex(e->pos().y());
	if (!index) {
		return;
	}
	if (deleteIconRect(*index * st::agentSessionListRowHeight)
			.contains(e->pos())) {
		ShowDeleteSessionBox(_controller, _manager, _rows[*index].id);
		return;
	}
	_openSessionRequests.fire_copy(_rows[*index].id);
}

void SessionsList::mouseMoveEvent(QMouseEvent *e) {
	const auto index = findRowIndex(e->pos().y());
	if (index != _selected) {
		_selected = index;
		setCursor(index ? style::cur_pointer : style::cur_default);
		update();
	}
}

void SessionsList::leaveEventHook(QEvent *e) {
	if (_selected) {
		_selected.reset();
		setCursor(style::cur_default);
		update();
	}
}

void SessionsList::contextMenuEvent(QContextMenuEvent *e) {
	const auto index = findRowIndex(e->pos().y());
	if (!index) {
		return;
	}
	_menu = base::make_unique_q<PopupMenu>(this, st::defaultPopupMenu);
	const auto id = _rows[*index].id;
	const auto addAction = Menu::CreateAddActionCallback(_menu);
	addAction(tr::lng_agent_session_rename(tr::now), [=] {
		ShowRenameSessionBox(_controller, _manager, id);
	}, &st::menuIconEdit);
	addAction(tr::lng_agent_session_delete(tr::now), [=] {
		ShowDeleteSessionBox(_controller, _manager, id);
	}, &st::menuIconDelete);
	_menu->popup(e->globalPos());
}

std::optional<int> SessionsList::findRowIndex(int y) const {
	if (_rows.empty()) {
		return std::nullopt;
	}
	const auto rowHeight = st::agentSessionListRowHeight;
	const auto index = y / rowHeight;
	if (index < 0 || index >= int(_rows.size())) {
		return std::nullopt;
	}
	return index;
}

} // namespace Agent::Ui