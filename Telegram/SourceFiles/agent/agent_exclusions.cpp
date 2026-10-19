/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_exclusions.h"

#include "main/main_session.h"

namespace Agent {

Exclusions::Exclusions(not_null<Main::Session*> session)
: _session(session) {
}

bool Exclusions::excluded(PeerKey peer) const {
	return false;
}

bool Exclusions::peerListed(PeerKey peer) const {
	return false;
}

bool Exclusions::folderExcluded(int folder) const {
	return false;
}

const ExclusionList &Exclusions::list() const {
	return _list;
}

void Exclusions::setPeerExcluded(PeerKey peer, bool excluded) {
}

void Exclusions::setFolderExcluded(int folder, bool excluded) {
}

rpl::producer<> Exclusions::changes() const {
	return _changes.events();
}

} // namespace Agent
