/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_anthropic_stream.h"

namespace Agent::Anthropic {

struct StreamAssembler::State {
	std::optional<Error> error;
};

StreamAssembler::StreamAssembler()
: _state(std::make_unique<State>()) {
}

StreamAssembler::~StreamAssembler() = default;

void StreamAssembler::event(const QJsonObject &event) {
}

std::vector<StreamDelta> StreamAssembler::takeDeltas() {
	return {};
}

bool StreamAssembler::started() const {
	return false;
}

bool StreamAssembler::stopped() const {
	return false;
}

const std::optional<Error> &StreamAssembler::error() const {
	return _state->error;
}

Response StreamAssembler::finish() const {
	return {};
}

Usage ParseUsage(const QJsonObject &usage) {
	return {};
}

StopReason ParseStopReason(const QString &reason) {
	return StopReason::EndTurn;
}

ErrorKind ErrorKindForType(const QString &type, int status) {
	return ErrorKind::BadResponse;
}

Error ParseErrorObject(const QJsonObject &error, int status) {
	return {};
}

} // namespace Agent::Anthropic
