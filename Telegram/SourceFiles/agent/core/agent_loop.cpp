/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_loop.h"

namespace Agent {

Loop::Loop(
	std::unique_ptr<Adapter> adapter,
	not_null<Transport*> transport,
	not_null<ToolExecutor*> tools,
	LoopOptions options,
	std::vector<Message> history)
: _adapter(std::move(adapter))
, _transport(transport)
, _tools(tools)
, _options(std::move(options))
, _history(std::move(history)) {
}

Loop::~Loop() = default;

void Loop::send(Message message) {
}

void Loop::interrupt() {
}

bool Loop::busy() const {
	return false;
}

const std::vector<Message> &Loop::history() const {
	return _history;
}

rpl::producer<EngineEvent> Loop::events() const {
	return _events.events();
}

} // namespace Agent
