/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_engine.h"

#include <rpl/event_stream.h>

#include <memory>
#include <vector>

namespace Agent {

struct LoopOptions {
	QString model;
	QString system;
	int maxOutputTokens = 0;
	int maxSteps = 25;
};

class Loop final : public Engine {
public:
	Loop(
		std::unique_ptr<Adapter> adapter,
		not_null<Transport*> transport,
		not_null<ToolExecutor*> tools,
		LoopOptions options,
		std::vector<Message> history = {});
	~Loop();

	void send(Message message) override;
	void interrupt() override;
	[[nodiscard]] bool busy() const override;
	[[nodiscard]] const std::vector<Message> &history() const override;
	[[nodiscard]] rpl::producer<EngineEvent> events() const override;

private:
	const std::unique_ptr<Adapter> _adapter;
	const not_null<Transport*> _transport;
	const not_null<ToolExecutor*> _tools;
	const LoopOptions _options;
	std::vector<Message> _history;
	rpl::event_stream<EngineEvent> _events;

};

} // namespace Agent
