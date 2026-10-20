/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_tasks.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

#include <QtCore/QDate>

namespace Agent {
namespace {

constexpr auto kSaveDelay = 2 * crl::time(1000);

[[nodiscard]] QString RunsKey(const QString &taskId) {
	return u"t/"_q + taskId;
}

} // namespace

Tasks::Tasks(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(); }) {
}

Tasks::~Tasks() {
	if (_tasksPending || _backgroundPending) {
		save();
	}
}

const std::vector<Task> &Tasks::list() const {
	load();
	return _list;
}

std::vector<Task> Tasks::list(const Scope &scope) const {
	load();
	auto result = std::vector<Task>();
	for (const auto &task : _list) {
		if (scope.kind == ScopeKind::Account || task.scope == scope) {
			result.push_back(task);
		}
	}
	return result;
}

const Task *Tasks::find(const QString &id) const {
	load();
	for (const auto &task : _list) {
		if (task.id == id) {
			return &task;
		}
	}
	return nullptr;
}

void Tasks::add(Task task) {
	load();
	const auto id = task.id;
	auto replaced = false;
	for (auto &existing : _list) {
		if (existing.id == id) {
			existing = task;
			replaced = true;
			break;
		}
	}
	if (!replaced) {
		_list.push_back(std::move(task));
	}
	_tasksPending = true;
	save();
	_changes.fire_copy(id);
}

void Tasks::update(const Task &task) {
	load();
	for (auto &existing : _list) {
		if (existing.id == task.id) {
			existing = task;
			_tasksPending = true;
			save();
			_changes.fire_copy(task.id);
			return;
		}
	}
}

void Tasks::remove(const QString &id) {
	load();
	auto removed = false;
	for (auto i = _list.begin(); i != _list.end(); ++i) {
		if (i->id == id) {
			_list.erase(i);
			removed = true;
			break;
		}
	}
	if (!removed) {
		return;
	}
	_session->local().removeAgentValue(RunsKey(id));
	_runs.remove(id);
	_tasksPending = true;
	save();
	_changes.fire_copy(id);
	_runChanges.fire_copy(id);
}

void Tasks::clearAll() {
	load();
	_saveTimer.cancel();
	_session->local().removeAgentValue(u"tasks"_q);
	for (const auto &task : _list) {
		_session->local().removeAgentValue(RunsKey(task.id));
	}
	_session->local().removeAgentValue(u"bgspending"_q);
	_list = {};
	_runs = {};
	_background = {};
	_tasksPending = false;
	_backgroundPending = false;
	_changes.fire_copy(QString());
}

std::vector<TaskRun> Tasks::runs(const QString &taskId) const {
	load();
	loadRuns(taskId);
	return _runs[taskId];
}

std::vector<QString> Tasks::saveRun(
		const QString &taskId,
		const TaskRun &run) {
	load();
	loadRuns(taskId);
	auto &runs = _runs[taskId];
	auto dropped = std::vector<QString>();
	auto replaced = false;
	for (auto &existing : runs) {
		if (existing.sessionId == run.sessionId) {
			existing = run;
			replaced = true;
			break;
		}
	}
	if (!replaced) {
		runs.insert(runs.begin(), run);
	}
	while (runs.size() > kTaskMaxRuns) {
		dropped.push_back(runs.back().sessionId);
		runs.pop_back();
	}
	saveRuns(taskId);
	_runChanges.fire_copy(taskId);
	return dropped;
}

void Tasks::addUsage(const QString &taskId, const Usage &usage) {
	load();
	const auto day = QDate::currentDate().toJulianDay();
	for (auto &task : _list) {
		if (task.id != taskId) {
			continue;
		}
		if (task.spendingDay != day) {
			task.spendingDay = day;
			task.spentToday = {};
		}
		task.spentToday.add(usage);
		_tasksPending = true;
		break;
	}
	AddSpending(_background, day, usage);
	_backgroundPending = true;
	_saveTimer.callOnce(kSaveDelay);
	_changes.fire_copy(taskId);
}

SpendingTotals Tasks::backgroundToday() const {
	load();
	const auto day = QDate::currentDate().toJulianDay();
	return SumSpending(_background, day, day);
}

rpl::producer<QString> Tasks::changes() const {
	return _changes.events();
}

rpl::producer<QString> Tasks::runChanges() const {
	return _runChanges.events();
}

void Tasks::load() const {
	if (_loaded) {
		return;
	}
	_loaded = true;
	if (const auto bytes = _session->local().readAgentValue(u"tasks"_q)) {
		if (auto tasks = DeserializeTasks(*bytes)) {
			_list = std::move(*tasks);
		} else {
			LOG(("Agent Error: could not parse the agent tasks."));
		}
	}
	if (const auto bytes
		= _session->local().readAgentValue(u"bgspending"_q)) {
		if (auto stats = DeserializeSpending(*bytes)) {
			_background = std::move(*stats);
		} else {
			LOG(("Agent Error: could not parse the background spending."));
		}
	}
}

void Tasks::loadRuns(const QString &taskId) const {
	if (_runs.contains(taskId)) {
		return;
	}
	auto value = std::vector<TaskRun>();
	if (const auto bytes
		= _session->local().readAgentValue(RunsKey(taskId))) {
		if (auto runs = DeserializeRuns(*bytes)) {
			value = std::move(*runs);
		} else {
			LOG(("Agent Error: could not parse the runs of task %1."
				).arg(taskId));
		}
	}
	_runs.emplace(taskId, std::move(value));
}

void Tasks::saveRuns(const QString &taskId) {
	_session->local().writeAgentValue(
		RunsKey(taskId),
		SerializeRuns(_runs[taskId]));
}

void Tasks::save() {
	if (_tasksPending) {
		_tasksPending = false;
		_session->local().writeAgentValue(
			u"tasks"_q,
			SerializeTasks(_list));
	}
	if (_backgroundPending) {
		_backgroundPending = false;
		_session->local().writeAgentValue(
			u"bgspending"_q,
			SerializeSpending(_background));
	}
}

} // namespace Agent
