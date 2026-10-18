/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_session_store.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Agent {

SessionStore::SessionStore(not_null<Main::Session*> session)
: _session(session) {
}

std::vector<SessionInfo> SessionStore::readIndex() const {
	const auto bytes = _session->local().readAgentValue(u"index"_q);
	if (!bytes) {
		return {};
	}
	auto list = DeserializeIndex(*bytes);
	if (!list) {
		LOG(("Agent Error: could not parse the session index."));
		return {};
	}
	return std::move(*list);
}

void SessionStore::writeIndex(const std::vector<SessionInfo> &list) {
	_session->local().writeAgentValue(
		u"index"_q,
		SerializeIndex(list));
}

std::optional<SessionRecord> SessionStore::readSession(
		const QString &id) const {
	const auto bytes = _session->local().readAgentValue(
		u"s/%1"_q.arg(id));
	if (!bytes) {
		return std::nullopt;
	}
	auto record = DeserializeSession(*bytes);
	if (!record) {
		LOG(("Agent Error: could not parse the session record."));
		return std::nullopt;
	}
	return record;
}

void SessionStore::writeSession(const SessionRecord &record) {
	_session->local().writeAgentValue(
		u"s/%1"_q.arg(record.info.id),
		SerializeSession(record));
}

void SessionStore::removeSession(const QString &id) {
	_session->local().removeAgentValue(u"s/%1"_q.arg(id));
}

} // namespace Agent
