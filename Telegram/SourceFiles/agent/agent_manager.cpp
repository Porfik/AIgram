/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_manager.h"

#include "agent/agent_http_transport.h"
#include "agent/agent_session.h"
#include "agent/agent_session_store.h"

namespace Agent {

Manager::Manager(not_null<Main::Session*> session)
: _session(session)
, _store(std::make_unique<SessionStore>(session))
, _transport(std::make_unique<HttpTransport>()) {
}

Manager::~Manager() = default;

Main::Session &Manager::session() const {
	return *_session;
}

bool Manager::available() const {
	return false;
}

bool Manager::enabled() const {
	return _enabled.current();
}

rpl::producer<bool> Manager::enabledValue() const {
	return _enabled.value();
}

const ProvidersState &Manager::providers() const {
	return _providers;
}

int Manager::contextWindow() const {
	return 0;
}

std::vector<SessionInfo> Manager::list(const Scope &scope) const {
	return {};
}

rpl::producer<Scope> Manager::listChanges() const {
	return _listChanges.events();
}

not_null<Session*> Manager::create(const Scope &scope) {
	auto record = SessionRecord();
	record.info.scope = scope;
	_sessions.push_back(std::make_unique<Session>(this, std::move(record)));
	return _sessions.back().get();
}

Session *Manager::open(const QString &id) {
	return nullptr;
}

void Manager::rename(const QString &id, const QString &title) {
}

void Manager::remove(const QString &id) {
}

Transport &Manager::transport() const {
	return *_transport;
}

void Manager::save(not_null<Session*> session) {
}

} // namespace Agent
