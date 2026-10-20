/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_task.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

#include <vector>

namespace Agent {

class Manager;

struct WaitingRun {
	QString taskId;
	QString sessionId;
	int cards = 0;
	qint64 since = 0;
};

class TaskRunner final : public base::has_weak_ptr {
public:
	explicit TaskRunner(not_null<Manager*> manager);
	~TaskRunner();

	[[nodiscard]] std::vector<WaitingRun> waiting(const Scope &scope) const;
	[[nodiscard]] rpl::producer<> waitingChanges() const;
	[[nodiscard]] bool running(const QString &taskId) const;
	[[nodiscard]] rpl::producer<QString> runningChanges() const;

	void taskRemoved(const QString &taskId);
	void stopAll();

private:
	const not_null<Manager*> _manager;
	rpl::event_stream<> _waitingChanges;
	rpl::event_stream<QString> _runningChanges;
	rpl::lifetime _lifetime;

};

} // namespace Agent
