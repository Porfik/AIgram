/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_exclusion_list.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class Exclusions final {
public:
	explicit Exclusions(not_null<Main::Session*> session);

	[[nodiscard]] bool excluded(PeerKey peer) const;
	[[nodiscard]] bool peerListed(PeerKey peer) const;
	[[nodiscard]] bool folderExcluded(int folder) const;
	[[nodiscard]] const ExclusionList &list() const;

	void setPeerExcluded(PeerKey peer, bool excluded);
	void setFolderExcluded(int folder, bool excluded);

	[[nodiscard]] rpl::producer<> changes() const;

private:
	const not_null<Main::Session*> _session;
	ExclusionList _list;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

} // namespace Agent
