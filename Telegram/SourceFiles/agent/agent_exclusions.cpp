/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_exclusions.h"

#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Agent {

Exclusions::Exclusions(not_null<Main::Session*> session)
: _session(session) {
	load();
	_session->data().chatsFilters().changed(
	) | rpl::on_next([=] {
		_changes.fire({});
	}, _lifetime);
}

bool Exclusions::excluded(PeerKey peer) const {
	if (ranges::contains(_list.peers, peer)) {
		return true;
	}
	if (_list.folders.empty() || !peer) {
		return false;
	}
	const auto history = _session->data().historyLoaded(PeerId(peer));
	if (!history) {
		return false;
	}
	for (const auto &filter : _session->data().chatsFilters().list()) {
		if (ranges::contains(_list.folders, int(filter.id()))
			&& filter.contains(history)) {
			return true;
		}
	}
	return false;
}

bool Exclusions::peerListed(PeerKey peer) const {
	return ranges::contains(_list.peers, peer);
}

bool Exclusions::folderExcluded(int folder) const {
	return ranges::contains(_list.folders, folder);
}

const ExclusionList &Exclusions::list() const {
	return _list;
}

void Exclusions::setPeerExcluded(PeerKey peer, bool excluded) {
	if (SetPeerExcluded(_list, peer, excluded)) {
		save();
		_changes.fire({});
	}
}

void Exclusions::setFolderExcluded(int folder, bool excluded) {
	if (SetFolderExcluded(_list, folder, excluded)) {
		save();
		_changes.fire({});
	}
}

rpl::producer<> Exclusions::changes() const {
	return _changes.events();
}

void Exclusions::load() {
	const auto bytes = _session->local().readAgentValue(u"excluded"_q);
	if (!bytes) {
		return;
	}
	auto list = DeserializeExclusions(*bytes);
	if (!list) {
		LOG(("Agent Error: could not parse the agent exclusions."));
		return;
	}
	_list = std::move(*list);
}

void Exclusions::save() {
	_session->local().writeAgentValue(
		u"excluded"_q,
		SerializeExclusions(_list));
}

} // namespace Agent
