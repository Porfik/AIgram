/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_memories.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Agent {

Memories::Memories(not_null<Main::Session*> session)
: _session(session) {
}

const Memory &Memories::get(PeerKey owner) {
	load();
	return _book[owner];
}

MemoryResult Memories::add(PeerKey owner, const QString &text) {
	load();
	auto &memory = _book[owner];
	auto result = MemoryAdd(memory, text);
	if (result.error == MemoryError::None) {
		save();
		_changes.fire_copy(owner);
	}
	return result;
}

MemoryResult Memories::update(
		PeerKey owner,
		const QString &id,
		const QString &text) {
	load();
	const auto i = _book.find(owner);
	if (i == _book.end()) {
		return { .error = MemoryError::UnknownId };
	}
	auto result = MemoryUpdate(i->second, id, text);
	if (result.error == MemoryError::None) {
		save();
		_changes.fire_copy(owner);
	}
	return result;
}

MemoryResult Memories::remove(PeerKey owner, const QString &id) {
	load();
	const auto i = _book.find(owner);
	if (i == _book.end()) {
		return { .error = MemoryError::UnknownId };
	}
	auto result = MemoryRemove(i->second, id);
	if (result.error == MemoryError::None) {
		save();
		_changes.fire_copy(owner);
	}
	return result;
}

MemoryResult Memories::restore(
		PeerKey owner,
		MemoryFact fact,
		int index) {
	load();
	auto &memory = _book[owner];
	auto result = MemoryRestore(memory, std::move(fact), index);
	if (result.error == MemoryError::None) {
		save();
		_changes.fire_copy(owner);
	}
	return result;
}

void Memories::clear(PeerKey owner) {
	load();
	auto &memory = _book[owner];
	if (memory.facts.empty()) {
		return;
	}
	memory.facts.clear();
	save();
	_changes.fire_copy(owner);
}

void Memories::clearAll() {
	_session->local().removeAgentValue(u"memory"_q);
	auto owners = std::vector<PeerKey>();
	for (const auto &[owner, memory] : _book) {
		if (!memory.facts.empty()) {
			owners.push_back(owner);
		}
	}
	_book = {};
	for (const auto owner : owners) {
		_changes.fire_copy(owner);
	}
}

rpl::producer<PeerKey> Memories::changes() const {
	return _changes.events();
}

void Memories::load() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	const auto bytes = _session->local().readAgentValue(u"memory"_q);
	if (!bytes) {
		return;
	}
	auto book = DeserializeMemoryBook(*bytes);
	if (!book) {
		LOG(("Agent Error: could not parse the agent memory."));
		return;
	}
	_book = std::move(*book);
}

void Memories::save() {
	_session->local().writeAgentValue(
		u"memory"_q,
		SerializeMemoryBook(_book));
}

} // namespace Agent
