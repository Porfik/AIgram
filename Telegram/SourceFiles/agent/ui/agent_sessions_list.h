/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Agent {
class Manager;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

class SessionsList final : public RpWidget {
public:
	SessionsList(
		QWidget *parent,
		not_null<Manager*> manager,
		Scope scope);

	[[nodiscard]] rpl::producer<QString> openSessionRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void rebuild();
	void openSession(const QString &id);
	void renameSession(const QString &id);
	void removeSession(const QString &id);

	struct Row {
		QString id;
		QString title;
		QString date;
		int height = 0;
	};
	[[nodiscard]] const Row *findRow(int y) const;

	const not_null<Manager*> _manager;
	const Scope _scope;
	std::vector<Row> _rows;
	rpl::event_stream<QString> _openSessionRequests;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Agent::Ui
