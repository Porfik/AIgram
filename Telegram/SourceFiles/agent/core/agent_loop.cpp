/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_loop.h"

#include "base/algorithm.h"
#include "base/assertion.h"

#include <crl/crl_on_main.h>

#include <utility>

namespace Agent {
namespace {

[[nodiscard]] bool UsageNonZero(const Usage &usage) {
	return usage.inputTokens
		|| usage.outputTokens
		|| usage.cachedInputTokens;
}

} // namespace

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

Loop::~Loop() {
	if (const auto id = base::take(_requestId)) {
		_transport->cancel(id);
	}
	for (auto i = 0, count = int(_toolUses.size()); i != count; ++i) {
		if (!_toolResults[i]) {
			_tools->cancel(_toolUses[i].id);
		}
	}
}

void Loop::send(Message message) {
	Expects(!_busy);

	_busy = true;
	_steps = 0;
	_history.push_back(std::move(message));
	step();
}

void Loop::interrupt() {
	if (!_busy) {
		return;
	}
	++_generation;
	const auto generation = _generation;
	if (const auto id = base::take(_requestId)) {
		_transport->cancel(id);
	}
	_parser = nullptr;
	_runningTools = false;
	_pendingTools = 0;
	if (_toolUses.empty()) {
		if (const auto text = base::take(_streamText); !text.isEmpty()) {
			_history.push_back(Message{
				Role::Assistant,
				{ TextBlock{ text } },
			});
		}
	} else {
		auto uses = base::take(_toolUses);
		auto finished = base::take(_toolResults);
		auto results = Message{ Role::User, {} };
		auto synthesized = std::vector<int>();
		results.blocks.reserve(uses.size());
		for (auto i = 0, count = int(uses.size()); i != count; ++i) {
			if (!finished[i]) {
				_tools->cancel(uses[i].id);
				finished[i] = ToolResultBlock{
					uses[i].id,
					u"Interrupted by the user."_q,
					true,
				};
				synthesized.push_back(i);
			}
			results.blocks.push_back(*finished[i]);
		}
		_history.push_back(std::move(results));
		for (const auto i : synthesized) {
			_events.fire(ToolCallFinished{ *finished[i] });
			if (_generation != generation) {
				return;
			}
		}
	}
	if (_generation == generation) {
		finishTurn(TurnEnd::Interrupted, std::nullopt);
	}
}

bool Loop::busy() const {
	return _busy;
}

const std::vector<Message> &Loop::history() const {
	return _history;
}

rpl::producer<EngineEvent> Loop::events() const {
	return _events.events();
}

void Loop::step() {
	++_generation;
	++_steps;
	_runningTools = false;
	_pendingTools = 0;
	const auto generation = _generation;
	auto request = Request{
		_options.model,
		_options.system,
		_history,
		_tools->tools(),
		_options.maxOutputTokens,
	};
	_streamText = QString();
	_parser = _adapter->parser();
	_requestId = _transport->start(
		_adapter->prepare(request),
		TransportHandlers{
			crl::guard(this, [=](HttpHead head) {
				if (_generation != generation) {
					return;
				}
				_parser->head(head);
			}),
			crl::guard(this, [=](QByteArray chunk) {
				if (_generation != generation) {
					return;
				}
				_parser->feed(chunk);
				auto deltas = _parser->takeDeltas();
				for (auto &delta : deltas) {
					if (_generation != generation) {
						return;
					}
					forwardDelta(std::move(delta));
				}
			}),
			crl::guard(this, [=](QString error) {
				if (_generation != generation) {
					return;
				}
				handleFinished(error);
			}),
		});
}

void Loop::forwardDelta(StreamDelta &&delta) {
	if (const auto text = std::get_if<TextDelta>(&delta)) {
		_streamText += text->text;
		_events.fire(std::move(*text));
	} else if (const auto thinking = std::get_if<ThinkingDelta>(&delta)) {
		_events.fire(std::move(*thinking));
	} else if (const auto use = std::get_if<ToolUseStarted>(&delta)) {
		_events.fire(std::move(*use));
	} else {
		Unexpected("Delta in Agent::Loop::forwardDelta.");
	}
}

void Loop::handleFinished(const QString &networkError) {
	const auto generation = _generation;
	_requestId = 0;
	const auto response = _parser->finish(networkError);
	_parser = nullptr;
	if (UsageNonZero(response.usage)) {
		_events.fire(UsageReported{ response.usage });
		if (_generation != generation) {
			return;
		}
	}
	if (response.error) {
		_streamText = QString();
		finishTurn(TurnEnd::Failed, response.error);
		return;
	}
	if (!response.message.blocks.empty()) {
		_history.push_back(response.message);
	}
	_streamText = QString();
	if (response.stopReason != StopReason::ToolUse) {
		const auto end = (response.stopReason == StopReason::MaxTokens)
			? TurnEnd::MaxTokens
			: (response.stopReason == StopReason::Refusal)
			? TurnEnd::Refusal
			: TurnEnd::Completed;
		finishTurn(end, std::nullopt);
		return;
	}
	auto uses = std::vector<ToolUseBlock>();
	for (const auto &block : response.message.blocks) {
		if (const auto use = std::get_if<ToolUseBlock>(&block)) {
			uses.push_back(*use);
		}
	}
	if (uses.empty()) {
		finishTurn(TurnEnd::Completed, std::nullopt);
	} else {
		startTools(std::move(uses));
	}
}

void Loop::startTools(std::vector<ToolUseBlock> uses) {
	_toolUses = std::move(uses);
	_toolResults.assign(_toolUses.size(), std::nullopt);
	_pendingTools = int(_toolUses.size());
	_runningTools = true;
	const auto generation = _generation;
	for (auto i = 0, count = int(_toolUses.size()); i != count; ++i) {
		const auto use = _toolUses[i];
		_events.fire(ToolCallRequested{ use });
		if (_generation != generation) {
			return;
		}
		if (!use.inputError.isEmpty()) {
			completeTool(
				i,
				ToolResultBlock{
					use.id,
					u"Invalid tool arguments: "_q + use.inputError,
					true,
				});
		} else {
			_tools->execute(use, crl::guard(
				this,
				[=](ToolResultBlock result) {
					if (_generation != generation) {
						return;
					}
					completeTool(i, std::move(result));
				}));
		}
		if (_generation != generation) {
			return;
		}
	}
	_runningTools = false;
	if (_busy && !_pendingTools && _generation == generation) {
		finishTools();
	}
}

void Loop::completeTool(int index, ToolResultBlock result) {
	const auto generation = _generation;
	_toolResults[index] = result;
	_events.fire(ToolCallFinished{ result });
	if (_generation != generation) {
		return;
	}
	--_pendingTools;
	if (!_pendingTools && !_runningTools && _generation == generation) {
		finishTools();
	}
}

void Loop::finishTools() {
	auto results = Message{ Role::User, {} };
	results.blocks.reserve(_toolResults.size());
	for (const auto &result : _toolResults) {
		results.blocks.push_back(*result);
	}
	_history.push_back(std::move(results));
	_toolUses.clear();
	_toolResults.clear();
	if (_steps >= _options.maxSteps) {
		finishTurn(TurnEnd::StepLimit, std::nullopt);
	} else {
		step();
	}
}

void Loop::finishTurn(TurnEnd end, std::optional<Error> error) {
	++_generation;
	_requestId = 0;
	_parser = nullptr;
	_runningTools = false;
	_pendingTools = 0;
	_toolUses.clear();
	_toolResults.clear();
	_busy = false;
	_events.fire(TurnFinished{ end, std::move(error) });
}

} // namespace Agent
