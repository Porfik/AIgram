/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_scope.h"
#include "agent/core/agent_types.h"

#include <optional>
#include <vector>

namespace Agent {

struct SessionInfo {
	QString id;
	Scope scope;
	QString title;
	bool titleFinal = false;
	qint64 created = 0;
	qint64 updated = 0;
	std::vector<PeerKey> touched;
};

struct SessionRecord {
	SessionInfo info;
	std::vector<Message> messages;
	int summaryUntil = 0;
	QString summary;
	Usage usage;
	bool tainted = false;
	std::vector<Card> cards;
	std::vector<QString> loadedGroups;
	std::vector<QString> undone;
};

[[nodiscard]] QString GenerateSessionId();

[[nodiscard]] QByteArray SerializeSession(const SessionRecord &record);
[[nodiscard]] std::optional<SessionRecord> DeserializeSession(
	const QByteArray &bytes);

bool CloseInterruptedTurn(SessionRecord &record);

[[nodiscard]] QByteArray SerializeIndex(const std::vector<SessionInfo> &list);
[[nodiscard]] std::optional<std::vector<SessionInfo>> DeserializeIndex(
	const QByteArray &bytes);

} // namespace Agent
