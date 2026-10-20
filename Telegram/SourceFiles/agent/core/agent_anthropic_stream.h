/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_adapter.h"

#include <QtCore/QJsonObject>

#include <memory>
#include <optional>
#include <vector>

namespace Agent::Anthropic {

class StreamAssembler final {
public:
	StreamAssembler();
	~StreamAssembler();

	void event(const QJsonObject &event);
	[[nodiscard]] std::vector<StreamDelta> takeDeltas();

	[[nodiscard]] bool started() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] const std::optional<Error> &error() const;
	[[nodiscard]] Response finish() const;

private:
	struct State;
	const std::unique_ptr<State> _state;

};

[[nodiscard]] Usage ParseUsage(const QJsonObject &usage);
[[nodiscard]] StopReason ParseStopReason(const QString &reason);
[[nodiscard]] ErrorKind ErrorKindForType(const QString &type, int status);
[[nodiscard]] Error ParseErrorObject(const QJsonObject &error, int status);

} // namespace Agent::Anthropic
