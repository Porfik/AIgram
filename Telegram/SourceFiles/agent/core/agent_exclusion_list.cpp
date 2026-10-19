/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_exclusion_list.h"

namespace Agent {

bool SetPeerExcluded(ExclusionList &list, PeerKey peer, bool excluded) {
	return false;
}

bool SetFolderExcluded(ExclusionList &list, int folder, bool excluded) {
	return false;
}

QByteArray SerializeExclusions(const ExclusionList &list) {
	return QByteArray();
}

std::optional<ExclusionList> DeserializeExclusions(const QByteArray &bytes) {
	return std::nullopt;
}

} // namespace Agent
