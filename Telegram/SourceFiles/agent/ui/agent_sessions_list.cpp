/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_sessions_list.h"

#include "agent/agent_manager.h"
#include "agent/core/agent_record.h"
#include "boxes/abstract_box.h" // Ui::show().
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_agent_panel.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Agent::Ui {

SessionsList::SessionsList(
	QWidget *parent,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _manager(manager)
, _scope(scope) {
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
	const auto list = _manager->list(_scope);
	if (list.empty()) {
		resize(width(), st::agentSessionListEmptyHeight);
		update();
		return;
	}
	auto height = 0;
	for (const auto &info : list) {
		auto row = Row{
			.id = info.id,
			.title = !info.title.isEmpty()
				? info.title
				: tr::lng_agent_session_untitled(tr::now),
			.date = Ui::FormatDateTime(
				QDateTime::fromSecsSinceEpoch(info.updated)),
			.height = st::agentSessionListRowHeight,
		};
		height += row.height;
		_rows.push_back(std::move(row));
	}
	resize(width(), height);
	update();
}

void SessionsList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
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
	auto top = 0;
	for (const auto &row : _rows) {
		p.setPen(st::windowFg);
		p.setFont(st::agentSessionListTitleStyle.font);
		p.drawText(
			QRect(
				padding.left(),
				top + padding.top(),
				width() - padding.left() - padding.right(),
				padding.top() + st::agentSessionListTitleStyle.font->height),
			Qt::AlignLeft,
			row.title);
		p.setPen(st::windowSubTextFg);
		p.setFont(st::agentSessionListDateStyle.font);
		p.drawText(
			QRect(
				padding.left(),
				top + padding.top()
					+ st::agentSessionListTitleStyle.font->height,
				width() - padding.left() - padding.right(),
				padding.bottom()),
			Qt::AlignLeft,
			row.date);
		top += rowHeight;
	}
}

void SessionsList::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (const auto row = findRow(e->pos().y())) {
		_openSessionRequests.fire_copy(row->id);
	}
}

void SessionsList::contextMenuEvent(QContextMenuEvent *e) {
	const auto row = findRow(e->pos().y());
	if (!row) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::defaultPopupMenu);
	const auto id = row->id;
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	addAction(tr::lng_agent_session_rename(tr::now), [=] {
		renameSession(id);
	}, &st::menuIconEdit);
	addAction(tr::lng_agent_session_delete(tr::now), [=] {
		removeSession(id);
	}, &st::menuIconDelete);
	_menu->popup(e->globalPos());
}

void SessionsList::renameSession(const QString &id) {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_agent_session_rename_title());
		const auto input = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_session_title_placeholder(),
				QString()));
		box->addButton(tr::lng_settings_save(), [=] {
			if (!input->getLastText().isEmpty()) {
				_manager->rename(id, input->getLastText());
			}
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void SessionsList::removeSession(const QString &id) {
	Ui::show(Ui::MakeConfirmBox({
		.text = tr::lng_agent_session_delete_sure(tr::marked),
		.confirmed = [=] {
			_manager->remove(id);
		},
		.confirmText = tr::lng_box_delete(),
	}));
}

const SessionsList::Row *SessionsList::findRow(int y) const {
	if (_rows.empty()) {
		return nullptr;
	}
	const auto rowHeight = st::agentSessionListRowHeight;
	const auto index = y / rowHeight;
	if (index < 0 || index >= int(_rows.size())) {
		return nullptr;
	}
	return &_rows[index];
}

} // namespace Agent::Ui
