/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_memory.h"
#include "agent/core/agent_scope.h"

#include <rpl/event_stream.h>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class Memories final {
public:
	explicit Memories(not_null<Main::Session*> session);

	[[nodiscard]] const Memory &get(PeerKey owner);

	MemoryResult add(PeerKey owner, const QString &text);
	MemoryResult update(
		PeerKey owner,
		const QString &id,
		const QString &text);
	MemoryResult remove(PeerKey owner, const QString &id);
	MemoryResult restore(PeerKey owner, MemoryFact fact, int index);
	void clear(PeerKey owner);
	void clearAll();

	[[nodiscard]] rpl::producer<PeerKey> changes() const;

private:
	void load();
	void save();

	const not_null<Main::Session*> _session;
	MemoryBook _book;
	bool _loaded = false;
	rpl::event_stream<PeerKey> _changes;

};

} // namespace Agent
