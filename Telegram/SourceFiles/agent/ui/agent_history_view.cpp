/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_history_view.h"

#include "agent/agent_manager.h"
#include "window/window_session_controller.h"

namespace Agent::Ui {

HistoryView::HistoryView(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope) {
}

rpl::producer<QString> HistoryView::openSessionRequests() const {
	return _openSessionRequests.events();
}

void HistoryView::showTab(HistoryTab tab) {
	_tab = tab;
}

HistoryTab HistoryView::tab() const {
	return _tab;
}

void HistoryView::setInnerFocus() {
	setFocus();
}

} // namespace Agent::Ui
