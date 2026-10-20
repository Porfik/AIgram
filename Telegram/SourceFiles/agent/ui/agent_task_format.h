/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_task.h"

namespace Main {
class Session;
} // namespace Main

namespace Agent {
class Manager;
} // namespace Agent

namespace Agent::Ui {

[[nodiscard]] QString FormatTaskTrigger(
	not_null<Main::Session*> session,
	const Task &task);
[[nodiscard]] QString FormatTaskSchedule(const Schedule &schedule);
[[nodiscard]] QString FormatTaskFrame(
	not_null<Main::Session*> session,
	const Task &task);
[[nodiscard]] QString FormatTaskLimits(
	const TaskLimits &limits,
	bool costKnown);
[[nodiscard]] QString FormatTaskNotify(const Task &task);
[[nodiscard]] QString FormatTaskModel(
	not_null<Manager*> manager,
	const Task &task);
[[nodiscard]] QString FormatTaskState(
	not_null<Manager*> manager,
	const Task &task);
[[nodiscard]] QString FormatTaskRunTrigger(
	not_null<Main::Session*> session,
	const TaskRun &run);
[[nodiscard]] QString FormatTaskRunOutcome(const TaskRun &run);
[[nodiscard]] bool TaskCostKnown(
	not_null<Manager*> manager,
	const Task &task);
[[nodiscard]] QString FormatTaskTool(const QString &tool);

} // namespace Agent::Ui
