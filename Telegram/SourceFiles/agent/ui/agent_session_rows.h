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
#include "ui/text/text.h"
#include "ui/userpic_view.h"

#include <optional>

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

class SessionRows final : public RpWidget {
public:
	SessionRows(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);

	[[nodiscard]] rpl::producer<QString> openRequests() const;
	[[nodiscard]] bool empty() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	struct Row {
		QString id;
		Text::String title;
		Text::String preview;
		QString date;
		bool outgoing = false;
		bool working = false;
		int pending = 0;
		PeerData *userpic = nullptr;
		PeerUserpicView view;
	};

	void rebuild();
	void refreshActivity();
	void paintRow(Painter &p, Row &row, int top, bool selected);
	[[nodiscard]] std::optional<int> rowAt(int y) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	std::vector<Row> _rows;
	std::optional<int> _selected;
	std::optional<int> _pressed;
	base::unique_qptr<PopupMenu> _menu;
	rpl::event_stream<QString> _openRequests;
	rpl::lifetime _userpicLifetime;

};

} // namespace Agent::Ui
