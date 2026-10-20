/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_queue.h"

#include "agent/agent_manager.h"
#include "window/window_session_controller.h"

namespace Agent::Ui {

QueueStrip::QueueStrip(
	QWidget *parent,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _manager(manager)
, _scope(scope) {
}

rpl::producer<> QueueStrip::clicks() const {
	return _clicks.events();
}

int QueueStrip::resizeGetHeight(int newWidth) {
	return 0;
}

void ShowQueueBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope,
		Fn<void(QString sessionId)> open) {
}

} // namespace Agent::Ui
