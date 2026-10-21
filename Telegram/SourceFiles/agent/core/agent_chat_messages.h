/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_engine.h"
#include "agent/core/agent_feed.h"

#include <optional>
#include <vector>

namespace Agent {

enum class ChatMessageKind : uchar {
	User,
	Agent,
	Card,
	Summary,
	TurnEnd,
};

struct ChatMessage {
	ChatMessageKind kind = ChatMessageKind::User;
	QString key;
	int index = 0;
	QString text;
	std::vector<MediaBlock> attachments;
	std::vector<FeedStep> steps;
	std::vector<FeedTool> actions;
	std::optional<FeedCard> card;
	QString replyTo;
	QString replyQuote;
	TurnEnd turnEnd = TurnEnd::Interrupted;
};

[[nodiscard]] std::vector<ChatMessage> BuildChatMessages(
	const std::vector<FeedItem> &feed);

class LiveTail final {
public:
	void apply(const EngineEvent &event);
	void clear();

	[[nodiscard]] bool empty() const;
	[[nodiscard]] std::vector<Message> messages() const;

private:
	[[nodiscard]] Message &assistant();

	std::optional<Message> _assistant;
	std::optional<Message> _results;

};

} // namespace Agent
