/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_tasks_view.h"

#include "agent/agent_manager.h"
#include "window/window_session_controller.h"

namespace Agent::Ui {

TasksView::TasksView(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope) {
}

rpl::producer<QString> TasksView::openRunRequests() const {
	return _openRunRequests.events();
}

void TasksView::showList() {
	_taskId = QString();
}

void TasksView::showTask(const QString &taskId) {
	_taskId = taskId;
}

QString TasksView::openedTask() const {
	return _taskId;
}

int TasksView::resizeGetHeight(int newWidth) {
	return 0;
}

} // namespace Agent::Ui
