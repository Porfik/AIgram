/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_tasks.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Agent {

Tasks::Tasks(not_null<Main::Session*> session)
: _session(session) {
}

Tasks::~Tasks() = default;

const std::vector<Task> &Tasks::list() const {
	load();
	return _list;
}

std::vector<Task> Tasks::list(const Scope &scope) const {
	return {};
}

const Task *Tasks::find(const QString &id) const {
	return nullptr;
}

void Tasks::add(Task task) {
}

void Tasks::update(const Task &task) {
}

void Tasks::remove(const QString &id) {
}

void Tasks::clearAll() {
}

std::vector<TaskRun> Tasks::runs(const QString &taskId) const {
	return {};
}

std::vector<QString> Tasks::saveRun(
		const QString &taskId,
		const TaskRun &run) {
	return {};
}

void Tasks::addUsage(const QString &taskId, const Usage &usage) {
}

SpendingTotals Tasks::backgroundToday() const {
	return {};
}

rpl::producer<QString> Tasks::changes() const {
	return _changes.events();
}

rpl::producer<QString> Tasks::runChanges() const {
	return _runChanges.events();
}

void Tasks::load() const {
	_loaded = true;
}

void Tasks::save() {
}

} // namespace Agent
