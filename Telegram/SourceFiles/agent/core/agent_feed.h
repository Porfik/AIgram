/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_types.h"
#include "base/flat_set.h"

#include <optional>
#include <variant>
#include <vector>

namespace Agent {

struct FeedUser {
	int index = 0;
	QString text;
	std::vector<MediaBlock> attachments;
};

struct FeedAssistant {
	int index = 0;
	QString text;
};

struct FeedThinking {
	int index = 0;
	QString text;
};

struct FeedTool {
	int index = 0;
	ToolUseBlock call;
	std::optional<ToolResultBlock> result;
};

using FeedStep = std::variant<FeedThinking, FeedTool>;

struct FeedSteps {
	int index = 0;
	std::vector<FeedStep> steps;
};

struct FeedSummary {
	int until = 0;
};

struct FeedActions {
	int index = 0;
	std::vector<FeedTool> actions;
};

struct FeedCard {
	int index = 0;
	ToolUseBlock call;
	std::optional<ToolResultBlock> result;
	Card card;
};

using FeedItem = std::variant<
	FeedUser,
	FeedAssistant,
	FeedSteps,
	FeedSummary,
	FeedActions,
	FeedCard>;

[[nodiscard]] std::vector<FeedItem> BuildFeed(
	const std::vector<Message> &messages,
	int summaryUntil,
	const std::vector<Card> &cards = {},
	const base::flat_set<QString> &actionTools = {});

} // namespace Agent
