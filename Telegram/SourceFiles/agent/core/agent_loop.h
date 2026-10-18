/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_engine.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>

#include <memory>
#include <optional>
#include <vector>

namespace Agent {

struct LoopOptions {
	QString model;
	QString system;
	int maxOutputTokens = 0;
	int maxSteps = 25;
};

class Loop final : public Engine, public base::has_weak_ptr {
public:
	Loop(
		std::unique_ptr<Adapter> adapter,
		not_null<Transport*> transport,
		not_null<ToolExecutor*> tools,
		LoopOptions options,
		std::vector<Message> history = {});
	~Loop();

	void send(Message message) override;
	void resume() override;
	void interrupt() override;
	[[nodiscard]] bool busy() const override;
	[[nodiscard]] const std::vector<Message> &history() const override;
	[[nodiscard]] rpl::producer<EngineEvent> events() const override;

private:
	void step();
	void handleFinished(const QString &networkError);
	void startTools(std::vector<ToolUseBlock> uses);
	void completeTool(int index, ToolResultBlock result);
	void finishTools();
	void finishTurn(TurnEnd end, std::optional<Error> error);
	void forwardDelta(StreamDelta &&delta);

	const std::unique_ptr<Adapter> _adapter;
	const not_null<Transport*> _transport;
	const not_null<ToolExecutor*> _tools;
	const LoopOptions _options;
	std::vector<Message> _history;
	rpl::event_stream<EngineEvent> _events;

	int _generation = 0;
	bool _busy = false;
	int _steps = 0;
	uint64 _requestId = 0;
	bool _runningTools = false;
	int _pendingTools = 0;
	QString _streamText;
	std::unique_ptr<ResponseParser> _parser;
	std::vector<ToolUseBlock> _toolUses;
	std::vector<std::optional<ToolResultBlock>> _toolResults;

};

} // namespace Agent
