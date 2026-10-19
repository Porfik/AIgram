/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"

#include <QtCore/QByteArray>

#include <optional>
#include <vector>

namespace Agent {

struct ExclusionList {
	std::vector<PeerKey> peers;
	std::vector<int> folders;
};

bool SetPeerExcluded(ExclusionList &list, PeerKey peer, bool excluded);
bool SetFolderExcluded(ExclusionList &list, int folder, bool excluded);

[[nodiscard]] QByteArray SerializeExclusions(const ExclusionList &list);
[[nodiscard]] std::optional<ExclusionList> DeserializeExclusions(
	const QByteArray &bytes);

} // namespace Agent
