/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_transport.h"
#include "agent/core/agent_types.h"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace Agent {

struct TextDelta {
	QString text;
};

struct ThinkingDelta {
	QString text;
};

struct ToolUseStarted {
	QString id;
	QString name;
};

using StreamDelta = std::variant<TextDelta, ThinkingDelta, ToolUseStarted>;

struct Response {
	std::optional<Error> error;
	Message message;
	StopReason stopReason = StopReason::EndTurn;
	Usage usage;
};

class ResponseParser {
public:
	virtual ~ResponseParser() = default;

	virtual void head(const HttpHead &head) = 0;
	virtual void feed(const QByteArray &chunk) = 0;
	[[nodiscard]] virtual std::vector<StreamDelta> takeDeltas() = 0;
	[[nodiscard]] virtual Response finish(const QString &networkError) = 0;

};

class Adapter {
public:
	virtual ~Adapter() = default;

	[[nodiscard]] virtual HttpRequest prepare(
		const Request &request) const = 0;
	[[nodiscard]] virtual std::unique_ptr<ResponseParser> parser() const = 0;

};

} // namespace Agent
