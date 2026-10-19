/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_sessions_list.h"

#include "agent/agent_manager.h"
#include "agent/core/agent_record.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_general.h"
#include "styles/style_agent_panel.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Agent::Ui {

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
		auto &row = _rows.emplace_back(Row{
			.id = info.id,
			.title = !info.title.isEmpty()
				? info.title
				: tr::lng_agent_session_untitled(tr::now),
			.date = FormatDateTime(
				QDateTime::fromSecsSinceEpoch(info.updated)),
		});
		if (_scope.kind != ScopeKind::Account) {
			continue;
		}
		for (const auto key : info.touched) {
			if (row.userpics.size() == kMaxUserpics) {
				break;
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
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(x, y, size, size);
		st::agentChatsRowIcon.paint(
			p,
			QPoint(
				x + (size - st::agentChatsRowIcon.width()) / 2,
				y + (size - st::agentChatsRowIcon.height()) / 2),
			x + size);
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
	auto top = 0;
	auto index = 0;
	for (auto &row : _rows) {
		if (index == _selected) {
			p.fillRect(0, top, width(), rowHeight, st::windowBgOver);
		}
		if (_scope.kind == ScopeKind::Account) {
			paintUserpicStack(p, row, top);
		}
		p.setPen(st::windowFg);
		p.setFont(titleFont);
		p.drawText(
			QRect(textLeft, top + padding.top(), textWidth, titleFont->height),
			Qt::AlignLeft,
			titleFont->elided(row.title, textWidth));
		p.setPen(st::windowSubTextFg);
		p.setFont(dateFont);
		p.drawText(
			QRect(textLeft, top + dateTop, textWidth, dateFont->height),
			Qt::AlignLeft,
			dateFont->elided(row.date, textWidth));
		top += rowHeight;
		++index;
	}
}

void SessionsList::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (const auto index = findRowIndex(e->pos().y())) {
		_openSessionRequests.fire_copy(_rows[*index].id);
	}
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
		renameSession(id);
	}, &st::menuIconEdit);
	addAction(tr::lng_agent_session_delete(tr::now), [=] {
		removeSession(id);
	}, &st::menuIconDelete);
	_menu->popup(e->globalPos());
}

void SessionsList::renameSession(const QString &id) {
	const auto manager = _manager;
	const auto current = titleOf(id);
	_controller->show(Box([manager, id, current](
			not_null<GenericBox*> box) {
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

void SessionsList::removeSession(const QString &id) {
	const auto manager = _manager;
	_controller->show(MakeConfirmBox({
		.text = tr::lng_agent_session_delete_sure(tr::marked),
		.confirmed = [manager, id] {
			manager->remove(id);
		},
		.confirmText = tr::lng_box_delete(),
	}));
}

QString SessionsList::titleOf(const QString &id) const {
	const auto list = _manager->list(_scope);
	for (const auto &info : list) {
		if (info.id == id) {
			return info.title;
		}
	}
	return QString();
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