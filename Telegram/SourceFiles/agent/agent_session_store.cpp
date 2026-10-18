/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_session_store.h"

namespace Agent {

SessionStore::SessionStore(not_null<Main::Session*> session)
: _session(session) {
}

std::vector<SessionInfo> SessionStore::readIndex() const {
	return {};
}

void SessionStore::writeIndex(const std::vector<SessionInfo> &list) {
}

std::optional<SessionRecord> SessionStore::readSession(
		const QString &id) const {
	return std::nullopt;
}

void SessionStore::writeSession(const SessionRecord &record) {
}

void SessionStore::removeSession(const QString &id) {
}

} // namespace Agent
