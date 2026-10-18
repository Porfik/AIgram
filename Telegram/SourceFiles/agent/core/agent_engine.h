/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_adapter.h"

#include <rpl/producer.h>

#include <optional>
#include <variant>
#include <vector>

namespace Agent {

struct ToolCallRequested {
	ToolUseBlock call;
};

struct ToolCallFinished {
	ToolResultBlock result;
};

struct UsageReported {
	Usage usage;
};

enum class TurnEnd : uchar {
	Completed,
	Interrupted,
	StepLimit,
	MaxTokens,
	Refusal,
	Failed,
};

struct TurnFinished {
	TurnEnd end = TurnEnd::Completed;
	std::optional<Error> error;
};

using EngineEvent = std::variant<
	TextDelta,
	ThinkingDelta,
	ToolUseStarted,
	ToolCallRequested,
	ToolCallFinished,
	UsageReported,
	TurnFinished>;

class ToolExecutor {
public:
	virtual ~ToolExecutor() = default;

	[[nodiscard]] virtual std::vector<ToolSpec> tools() const = 0;
	virtual void execute(
		ToolUseBlock call,
		Fn<void(ToolResultBlock)> done) = 0;
	virtual void cancel(const QString &toolUseId) = 0;

};

class Engine {
public:
	virtual ~Engine() = default;

	virtual void send(Message message) = 0;
	virtual void interrupt() = 0;
	[[nodiscard]] virtual bool busy() const = 0;
	[[nodiscard]] virtual const std::vector<Message> &history() const = 0;
	[[nodiscard]] virtual rpl::producer<EngineEvent> events() const = 0;

};

} // namespace Agent
