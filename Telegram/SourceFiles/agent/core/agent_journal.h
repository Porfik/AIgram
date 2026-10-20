/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_record.h"
#include "base/flat_set.h"

#include <optional>
#include <vector>

namespace Agent {

struct JournalEntry {
	QString sessionId;
	QString sessionTitle;
	Scope scope;
	QString task;
	ToolUseBlock call;
	std::optional<ToolResultBlock> result;
	std::optional<Card> card;
	qint64 date = 0;
	bool undone = false;
	bool failed = false;
};

[[nodiscard]] std::vector<JournalEntry> BuildJournal(
	const std::vector<SessionRecord> &records,
	const base::flat_set<QString> &actionTools,
	int limit);

} // namespace Agent
