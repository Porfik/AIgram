/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_task_runner.h"

#include "agent/agent_manager.h"

namespace Agent {

TaskRunner::TaskRunner(not_null<Manager*> manager)
: _manager(manager) {
}

TaskRunner::~TaskRunner() = default;

std::vector<WaitingRun> TaskRunner::waiting(const Scope &scope) const {
	return {};
}

rpl::producer<> TaskRunner::waitingChanges() const {
	return _waitingChanges.events();
}

bool TaskRunner::running(const QString &taskId) const {
	return false;
}

rpl::producer<QString> TaskRunner::runningChanges() const {
	return _runningChanges.events();
}

void TaskRunner::taskRemoved(const QString &taskId) {
}

void TaskRunner::stopAll() {
}

} // namespace Agent
