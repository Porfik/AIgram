/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_memories.h"

#include "main/main_session.h"

namespace Agent {

Memories::Memories(not_null<Main::Session*> session)
: _session(session) {
}

const Memory &Memories::get(PeerKey owner) {
	return _book[owner];
}

MemoryResult Memories::add(PeerKey owner, const QString &text) {
	return { .error = MemoryError::Full };
}

MemoryResult Memories::update(
		PeerKey owner,
		const QString &id,
		const QString &text) {
	return { .error = MemoryError::UnknownId };
}

MemoryResult Memories::remove(PeerKey owner, const QString &id) {
	return { .error = MemoryError::UnknownId };
}

MemoryResult Memories::restore(
		PeerKey owner,
		MemoryFact fact,
		int index) {
	return { .error = MemoryError::Full };
}

void Memories::clear(PeerKey owner) {
}

void Memories::clearAll() {
}

rpl::producer<PeerKey> Memories::changes() const {
	return _changes.events();
}

void Memories::load() {
}

void Memories::save() {
}

} // namespace Agent
