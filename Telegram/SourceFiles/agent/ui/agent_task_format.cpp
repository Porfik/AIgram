/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_task_format.h"

#include "agent/agent_manager.h"
#include "main/main_session.h"

namespace Agent::Ui {

QString FormatTaskTrigger(
		not_null<Main::Session*> session,
		const Task &task) {
	return QString();
}

QString FormatTaskSchedule(const Schedule &schedule) {
	return QString();
}

QString FormatTaskFrame(
		not_null<Main::Session*> session,
		const Task &task) {
	return QString();
}

QString FormatTaskLimits(const TaskLimits &limits, bool costKnown) {
	return QString();
}

QString FormatTaskNotify(const Task &task) {
	return QString();
}

QString FormatTaskModel(not_null<Manager*> manager, const Task &task) {
	return QString();
}

QString FormatTaskState(not_null<Manager*> manager, const Task &task) {
	return QString();
}

QString FormatTaskRunTrigger(
		not_null<Main::Session*> session,
		const TaskRun &run) {
	return QString();
}

QString FormatTaskRunOutcome(const TaskRun &run) {
	return QString();
}

QString FormatTaskTool(const QString &tool) {
	return tool;
}

} // namespace Agent::Ui
