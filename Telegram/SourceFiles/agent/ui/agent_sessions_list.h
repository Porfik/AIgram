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
#include "ui/userpic_view.h"

#include <optional>

class Painter;
class PeerData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

class SessionsList final : public RpWidget {
public:
	SessionsList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);

	[[nodiscard]] rpl::producer<QString> openSessionRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void rebuild();
	void renameSession(const QString &id);
	void removeSession(const QString &id);
	[[nodiscard]] QString titleOf(const QString &id) const;

	struct Userpic {
		not_null<PeerData*> peer;
		PeerUserpicView view;
	};
	struct Row {
		QString id;
		QString title;
		QString date;
		std::vector<Userpic> userpics;
	};
	void paintUserpicStack(Painter &p, Row &row, int top);
	[[nodiscard]] int rowTextLeft() const;
	[[nodiscard]] std::optional<int> findRowIndex(int y) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	std::vector<Row> _rows;
	std::optional<int> _selected;
	rpl::event_stream<QString> _openSessionRequests;
	base::unique_qptr<PopupMenu> _menu;
	rpl::lifetime _userpicLifetime;

};

} // namespace Agent::Ui