/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_sse.h"

namespace Agent {

std::vector<SseEvent> SseParser::feed(const QByteArray &chunk) {
	return {};
}

std::vector<SseEvent> SseParser::finish() {
	return {};
}

} // namespace Agent
