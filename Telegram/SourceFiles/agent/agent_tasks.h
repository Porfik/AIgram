/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_spending.h"
#include "agent/core/agent_task.h"
#include "base/flat_map.h"

#include <rpl/event_stream.h>

#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class Tasks final {
public:
	explicit Tasks(not_null<Main::Session*> session);
	~Tasks();

	[[nodiscard]] const std::vector<Task> &list() const;
	[[nodiscard]] std::vector<Task> list(const Scope &scope) const;
	[[nodiscard]] const Task *find(const QString &id) const;

	void add(Task task);
	void update(const Task &task);
	void remove(const QString &id);
	void clearAll();

	[[nodiscard]] std::vector<TaskRun> runs(const QString &taskId) const;
	std::vector<QString> saveRun(const QString &taskId, const TaskRun &run);

	void addUsage(const QString &taskId, const Usage &usage);
	[[nodiscard]] SpendingTotals backgroundToday() const;

	[[nodiscard]] rpl::producer<QString> changes() const;
	[[nodiscard]] rpl::producer<QString> runChanges() const;

private:
	void load() const;
	void save();

	const not_null<Main::Session*> _session;
	mutable std::vector<Task> _list;
	mutable base::flat_map<QString, std::vector<TaskRun>> _runs;
	mutable SpendingStats _background;
	mutable bool _loaded = false;
	rpl::event_stream<QString> _changes;
	rpl::event_stream<QString> _runChanges;

};

} // namespace Agent
