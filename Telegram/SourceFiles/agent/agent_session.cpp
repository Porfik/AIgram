/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_session.h"

#include "agent/agent_manager.h"
#include "agent/core/agent_loop.h"
#include "agent/core/agent_policy.h"
#include "agent/tools/agent_tools.h"

namespace Agent {

Session::Session(not_null<Manager*> manager, SessionRecord record)
: _manager(manager)
, _record(std::move(record)) {
}

Session::~Session() = default;

Manager &Session::manager() const {
	return *_manager;
}

const QString &Session::id() const {
	return _record.info.id;
}

const Scope &Session::scope() const {
	return _record.info.scope;
}

const SessionRecord &Session::record() const {
	return _record;
}

rpl::producer<> Session::recordChanges() const {
	return _recordChanges.events();
}

rpl::producer<EngineEvent> Session::live() const {
	return _live.events();
}

const SessionState &Session::state() const {
	return _state;
}

rpl::producer<> Session::stateChanges() const {
	return _stateChanges.events();
}

void Session::send(SendRequest request) {
}

void Session::stop() {
}

void Session::retry() {
}

} // namespace Agent
