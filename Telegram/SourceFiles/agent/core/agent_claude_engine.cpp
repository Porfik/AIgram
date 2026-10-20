/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_claude_engine.h"

#include "agent/core/agent_anthropic_stream.h"
#include "base/algorithm.h"
#include "base/variant.h"

#include <crl/crl_on_main.h>

#include <QtCore/QJsonDocument>

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/find.hpp>

namespace Agent::Claude {
namespace {

constexpr auto kInterruptedText = "Interrupted by the user.";
constexpr auto kUnfinishedText = "The tool call did not finish.";

[[nodiscard]] bool UsageNonZero(const Usage &usage) {
	return usage.inputTokens
		|| usage.outputTokens
		|| usage.cachedInputTokens
		|| usage.cacheWriteTokens;
}

[[nodiscard]] QJsonObject McpEnvelope(const QJsonObject &message) {
	auto result = QJsonObject();
	result.insert(u"mcp_response"_q, message);
	return result;
}

[[nodiscard]] Error ProcessError(const QString &text) {
	return Error{
		ErrorKind::Network,
		0,
		text.isEmpty()
			? u"Claude Code stopped."_q
			: u"Claude Code stopped: %1"_q.arg(text),
	};
}

} // namespace

Connection::Connection(
	not_null<Launcher*> launcher,
	const LaunchSettings &launch,
	SessionOptions options,
	QString owner)
: _options(std::move(options))
, _owner(std::move(owner)) {
	_key.owner = _owner;
	_process = launcher->start(
		LaunchSpec{
			launch.program,
			LaunchArguments(_options),
			launch.workingDirectory,
			launch.environment,
		},
		ProcessHandlers{
			[=](QByteArray line) { handleLine(line); },
			[=](QString error) { handleFinished(error); },
		});
}

Connection::~Connection() {
	_process = nullptr;
}

const SessionOptions &Connection::options() const {
	return _options;
}

const QString &Connection::owner() const {
	return _owner;
}

const ConnectionKey &Connection::key() const {
	return _key;
}

void Connection::setKey(ConnectionKey key) {
	_key = std::move(key);
}

bool Connection::alive() const {
	return _process && !_exit.has_value();
}

bool Connection::initialized() const {
	return _initialized;
}

void Connection::setInitialized() {
	_initialized = true;
}

void Connection::setHandlers(
		Fn<void(Line)> line,
		Fn<void(QString)> finished) {
	_line = std::move(line);
	_finished = std::move(finished);
}

void Connection::write(const QByteArray &line) {
	if (alive()) {
		_process->write(line);
	}
}

QString Connection::nextRequestId() {
	return u"aigram_%1"_q.arg(++_requestId);
}

void Connection::handleLine(const QByteArray &line) {
	auto parsed = ParseLine(line);
	if (const auto handler = _line) {
		handler(std::move(parsed));
	} else if (const auto request = std::get_if<ControlRequest>(&parsed)) {
		if (request->subtype != u"control_cancel_request"_q) {
			write(ControlErrorLine(request->requestId, u"Not ready."_q));
		}
	}
}

void Connection::handleFinished(const QString &error) {
	_exit = error;
	if (const auto finished = base::take(_finished)) {
		_line = nullptr;
		finished(error);
	}
}

Engine::Engine(
	not_null<Launcher*> launcher,
	ConnectionPool *pool,
	not_null<ToolExecutor*> tools,
	EngineOptions options,
	std::vector<Message> history)
: _launcher(launcher)
, _pool(pool)
, _tools(tools)
, _options(std::move(options))
, _history(std::move(history)) {
}

Engine::~Engine() {
	for (const auto &entry : _uses) {
		if (entry.running) {
			_tools->cancel(entry.use.id);
		}
	}
	if (_connection) {
		_connection->setHandlers(nullptr, nullptr);
	}
}

void Engine::send(Message message) {
	Expects(!_busy);

	_history.push_back(std::move(message));
	resume();
}

void Engine::resume() {
	Expects(!_busy);
	Expects(!_history.empty() && _history.back().role == Role::User);

	_busy = true;
	_appended = 0;
	_assistantError = QString();
	_lastStop = StopReason::EndTurn;
	start();
}

bool Engine::busy() const {
	return _busy;
}

const std::vector<Message> &Engine::history() const {
	return _history;
}

rpl::producer<EngineEvent> Engine::events() const {
	return _events.events();
}

void Engine::start() {
	const auto expected = ConnectionKey{
		_options.owner,
		_options.recordMessages - 1,
		_options.summaryUntil,
	};
	auto warm = (_pool && !_options.owner.isEmpty())
		? _pool->take(_options.owner)
		: nullptr;
	if (warm
		&& warm->alive()
		&& warm->initialized()
		&& warm->options() == _options.session
		&& warm->key() == expected) {
		_connection = std::move(warm);
		attach();
		sendQuery();
		return;
	}
	warm = nullptr;
	_connection = std::make_unique<Connection>(
		_launcher,
		_options.launch,
		_options.session,
		_options.owner);
	attach();
	sendInitialize();
}

void Engine::attach() {
	const auto generation = _generation;
	_connection->setHandlers([=](Line line) {
		if (_generation == generation) {
			handleLine(std::move(line));
		}
	}, [=](QString error) {
		if (_generation == generation) {
			handleExit(error);
		}
	});
}

void Engine::sendInitialize() {
	_phase = Phase::Initializing;
	_initRequestId = _connection->nextRequestId();
	_connection->write(ControlRequestLine(
		_initRequestId,
		InitializeRequest(_options.session.system)));
}

void Engine::replayNext() {
	while (_replayIndex < int(_replay.size())) {
		const auto &frame = _replay[_replayIndex++];
		const auto last = (_replayIndex == int(_replay.size()));
		if (last) {
			const auto line = UserLine(frame.content, true);
			_replay.clear();
			_replayIndex = 0;
			_phase = Phase::Querying;
			_connection->write(line);
			return;
		} else if (frame.role == Role::Assistant) {
			_connection->write(AssistantLine(frame.content));
		} else {
			_connection->write(UserLine(frame.content, false));
			return;
		}
	}
}

void Engine::sendQuery() {
	_phase = Phase::Querying;
	_announcedTools = toolNames();
	_connection->write(UserLine(UserContent(_history.back()), true));
}

QStringList Engine::toolNames() const {
	auto result = QStringList();
	for (const auto &tool : _tools->tools()) {
		result.push_back(tool.name);
	}
	return result;
}

void Engine::handleLine(Line line) {
	v::match(line, [&](const ControlResponse &response) {
		if (_phase != Phase::Initializing
			|| response.requestId != _initRequestId) {
			return;
		} else if (!response.success) {
			finishTurn(TurnEnd::Failed, Error{
				ErrorKind::BadResponse,
				0,
				u"Claude Code refused to start: %1"_q.arg(response.error),
			});
			return;
		}
		_connection->setInitialized();
		_replay = ReplayFrames(_history);
		if (_replay.empty() || _replay.back().role != Role::User) {
			finishTurn(TurnEnd::Failed, Error{
				ErrorKind::BadRequest,
				0,
				u"The conversation must end with a user message."_q,
			});
			return;
		}
		_replayIndex = 0;
		_phase = Phase::Replaying;
		_announcedTools = toolNames();
		replayNext();
	}, [&](const ControlRequest &request) {
		handleControl(request);
	}, [&](const StreamEvent &event) {
		if (!event.nested && _phase == Phase::Querying) {
			handleStream(event.event);
		}
	}, [&](const AssistantEcho &echo) {
		if (!echo.nested && !echo.error.isEmpty()) {
			_assistantError = echo.error;
		}
	}, [&](const UserEcho &echo) {
		if (!echo.nested && !echo.replay && _phase == Phase::Querying) {
			handleEcho(echo);
		}
	}, [&](const ResultLine &result) {
		if (_phase == Phase::Replaying
			&& result.numTurns == 0
			&& !result.isError) {
			replayNext();
		} else if (_phase == Phase::Replaying
			|| _phase == Phase::Querying) {
			handleResult(result);
		}
	}, [&](const Limits &limits) {
		if (_options.limits) {
			_options.limits(limits);
		}
	}, [](const auto &) {
	});
}

void Engine::handleExit(const QString &error) {
	if (!_busy) {
		return;
	}
	keepPartialText();
	finishTurn(TurnEnd::Failed, ProcessError(error));
}

void Engine::handleStream(const QJsonObject &event) {
	const auto type = event.value(u"type"_q).toString();
	if (type == u"message_start"_q) {
		_assembler = std::make_unique<Anthropic::StreamAssembler>();
		_streamText = QString();
	}
	if (!_assembler) {
		return;
	}
	_assembler->event(event);
	const auto generation = _generation;
	for (auto &delta : _assembler->takeDeltas()) {
		if (const auto text = std::get_if<TextDelta>(&delta)) {
			_streamText += text->text;
		} else if (const auto use = std::get_if<ToolUseStarted>(&delta)) {
			const auto own = OwnToolName(use->name);
			if (!own.isEmpty()) {
				use->name = own;
			}
		}
		v::match(delta, [&](const auto &value) {
			_events.fire_copy(value);
		});
		if (_generation != generation) {
			return;
		}
	}
	if (type != u"message_stop"_q) {
		return;
	}
	auto response = base::take(_assembler)->finish();
	_streamText = QString();
	if (response.error) {
		return;
	}
	for (auto &block : response.message.blocks) {
		if (const auto use = std::get_if<ToolUseBlock>(&block)) {
			const auto own = OwnToolName(use->name);
			if (!own.isEmpty()) {
				use->name = own;
			}
			_uses.push_back({ .use = *use });
		}
	}
	_lastStop = response.stopReason;
	if (!response.message.blocks.empty()) {
		_history.push_back(std::move(response.message));
		++_appended;
	}
	if (UsageNonZero(response.usage)) {
		_events.fire(UsageReported{ response.usage });
	}
}

void Engine::handleControl(const ControlRequest &request) {
	if (request.subtype == u"control_cancel_request"_q) {
		return;
	} else if (request.subtype == u"mcp_message"_q) {
		handleMcp(
			request.requestId,
			request.request.value(u"message"_q).toObject());
		return;
	} else if (request.subtype == u"can_use_tool"_q) {
		const auto name = request.request.value(u"tool_name"_q).toString();
		auto response = QJsonObject();
		if (!OwnToolName(name).isEmpty()) {
			response.insert(u"behavior"_q, u"allow"_q);
			response.insert(
				u"updatedInput"_q,
				request.request.value(u"input"_q).toObject());
		} else {
			response.insert(u"behavior"_q, u"deny"_q);
			response.insert(
				u"message"_q,
				u"Only the AIgram tools are available."_q);
		}
		_connection->write(ControlResponseLine(request.requestId, response));
		return;
	}
	_connection->write(ControlErrorLine(
		request.requestId,
		u"Not supported by AIgram."_q));
}

void Engine::handleMcp(const QString &requestId, const QJsonObject &message) {
	const auto method = message.value(u"method"_q).toString();
	const auto id = message.value(u"id"_q);
	const auto params = message.value(u"params"_q).toObject();
	const auto respond = [&](const QJsonObject &mcp) {
		_connection->write(ControlResponseLine(requestId, McpEnvelope(mcp)));
	};
	if (method == u"tools/call"_q) {
		handleToolCall(requestId, id, params);
		return;
	} else if (method == u"notifications/cancelled"_q) {
		handleCancelled(params);
	}
	if (id.isUndefined() || id.isNull()) {
		respond(McpResult(0, QJsonObject()));
	} else if (method == u"initialize"_q) {
		respond(McpResult(id, McpInitializeResult(params)));
	} else if (method == u"tools/list"_q) {
		_announcedTools = toolNames();
		respond(McpResult(id, McpToolsList(_tools->tools())));
	} else if (method == u"ping"_q) {
		respond(McpResult(id, QJsonObject()));
	} else {
		respond(McpError(id, -32601, u"Method not found."_q));
	}
}

void Engine::handleToolCall(
		const QString &requestId,
		const QJsonValue &id,
		const QJsonObject &params) {
	const auto name = params.value(u"name"_q).toString();
	const auto meta = params.value(u"_meta"_q).toObject();
	auto toolUseId = meta.value(u"claudecode/toolUseId"_q).toString();
	auto entry = findUse(toolUseId);
	if (!entry) {
		auto use = ToolUseBlock();
		use.id = toolUseId.isEmpty()
			? u"aigram_call_%1"_q.arg(_connection->nextRequestId())
			: toolUseId;
		use.name = name;
		use.input = params.value(u"arguments"_q).toObject();
		_uses.push_back({ .use = use });
		entry = &_uses.back();
		toolUseId = use.id;
	}
	if (entry->requested || entry->result) {
		_connection->write(ControlResponseLine(requestId, McpEnvelope(
			McpError(id, -32600, u"The call was already handled."_q))));
		return;
	}
	entry->requested = true;
	entry->running = true;
	entry->mcpId = id;
	entry->mcpRequestId = requestId;
	const auto use = entry->use;
	const auto generation = _generation;
	_events.fire(ToolCallRequested{ use });
	if (_generation != generation) {
		return;
	}
	_tools->execute(use, crl::guard(this, [=](ToolResultBlock result) {
		if (_generation != generation) {
			return;
		}
		completeTool(toolUseId, std::move(result));
	}));
}

void Engine::handleCancelled(const QJsonObject &params) {
	const auto cancelled = params.value(u"requestId"_q);
	for (auto &entry : _uses) {
		if (entry.running && entry.mcpId == cancelled) {
			entry.running = false;
			entry.mcpRequestId = QString();
			_tools->cancel(entry.use.id);
		}
	}
}

void Engine::completeTool(const QString &toolUseId, ToolResultBlock result) {
	const auto entry = findUse(toolUseId);
	if (!entry || entry->result) {
		return;
	}
	result.toolUseId = toolUseId;
	entry->running = false;
	entry->result = result;
	if (!entry->mcpRequestId.isEmpty()) {
		_connection->write(ControlResponseLine(
			base::take(entry->mcpRequestId),
			McpEnvelope(McpResult(entry->mcpId, McpToolResult(result)))));
	}
	const auto generation = _generation;
	_events.fire(ToolCallFinished{ std::move(result) });
	if (_generation != generation) {
		return;
	}
	announceToolsChange();
	finishToolsIfReady();
}

void Engine::handleEcho(const UserEcho &echo) {
	for (const auto &result : echo.results) {
		const auto entry = findUse(result.toolUseId);
		if (!entry || entry->result) {
			continue;
		}
		const auto generation = _generation;
		if (!entry->requested) {
			entry->requested = true;
			_events.fire(ToolCallRequested{ entry->use });
			if (_generation != generation) {
				return;
			}
		}
		completeTool(result.toolUseId, ToolResultBlock{
			result.toolUseId,
			result.content,
			true,
		});
		if (_generation != generation) {
			return;
		}
	}
}

void Engine::finishToolsIfReady() {
	if (_uses.empty()
		|| ranges::any_of(_uses, [](const PendingUse &entry) {
			return !entry.result.has_value();
		})) {
		return;
	}
	auto results = Message{ Role::User, {} };
	for (auto &entry : base::take(_uses)) {
		results.blocks.push_back(std::move(*entry.result));
	}
	_history.push_back(std::move(results));
	++_appended;
}

void Engine::announceToolsChange() {
	if (!_connection || _phase != Phase::Querying) {
		return;
	}
	auto names = toolNames();
	if (names == _announcedTools) {
		return;
	}
	_announcedTools = std::move(names);
	auto message = QJsonObject();
	message.insert(u"jsonrpc"_q, u"2.0"_q);
	message.insert(u"method"_q, u"notifications/tools/list_changed"_q);
	auto request = QJsonObject();
	request.insert(u"subtype"_q, u"mcp_message"_q);
	request.insert(u"server_name"_q, ServerName());
	request.insert(u"message"_q, message);
	_connection->write(ControlRequestLine(
		_connection->nextRequestId(),
		request));
}

void Engine::handleResult(const ResultLine &result) {
	if (_phase == Phase::Replaying) {
		finishTurn(TurnEnd::Failed, ResultError(result, _assistantError));
		return;
	}
	keepPartialText();
	for (auto &entry : _uses) {
		if (!entry.result) {
			if (entry.running) {
				_tools->cancel(entry.use.id);
				entry.running = false;
			}
			entry.result = ToolResultBlock{
				entry.use.id,
				QString::fromLatin1(kUnfinishedText),
				true,
			};
		}
	}
	finishToolsIfReady();
	if (result.subtype == u"error_max_turns"_q) {
		finishTurn(TurnEnd::StepLimit, std::nullopt);
	} else if (_assistantError == u"max_output_tokens"_q) {
		finishTurn(TurnEnd::MaxTokens, std::nullopt);
	} else if (result.isError || result.subtype != u"success"_q) {
		finishTurn(TurnEnd::Failed, ResultError(result, _assistantError));
	} else {
		finishTurn((_lastStop == StopReason::MaxTokens)
			? TurnEnd::MaxTokens
			: (_lastStop == StopReason::Refusal)
			? TurnEnd::Refusal
			: TurnEnd::Completed, std::nullopt);
	}
}

void Engine::keepPartialText() {
	_assembler = nullptr;
	if (const auto text = base::take(_streamText); !text.isEmpty()) {
		_history.push_back(Message{ Role::Assistant, { TextBlock{ text } } });
		++_appended;
	}
}

Engine::PendingUse *Engine::findUse(const QString &toolUseId) {
	if (toolUseId.isEmpty()) {
		return nullptr;
	}
	const auto i = ranges::find(_uses, toolUseId, [](const PendingUse &e) {
		return e.use.id;
	});
	return (i != _uses.end()) ? &*i : nullptr;
}

void Engine::interrupt() {
	if (!_busy) {
		return;
	}
	++_generation;
	const auto generation = _generation;
	if (_connection && _phase == Phase::Querying) {
		auto request = QJsonObject();
		request.insert(u"subtype"_q, u"interrupt"_q);
		_connection->write(ControlRequestLine(
			_connection->nextRequestId(),
			request));
	}
	keepPartialText();
	auto synthesized = std::vector<ToolResultBlock>();
	for (auto &entry : _uses) {
		if (!entry.result) {
			if (entry.running) {
				_tools->cancel(entry.use.id);
				entry.running = false;
			}
			entry.result = ToolResultBlock{
				entry.use.id,
				QString::fromLatin1(kInterruptedText),
				true,
			};
			synthesized.push_back(*entry.result);
		}
	}
	finishToolsIfReady();
	for (auto &result : synthesized) {
		_events.fire(ToolCallFinished{ std::move(result) });
		if (_generation != generation) {
			return;
		}
	}
	finishTurn(TurnEnd::Interrupted, std::nullopt);
}

void Engine::finishTurn(TurnEnd end, std::optional<Error> error) {
	++_generation;
	const auto clean = (end == TurnEnd::Completed
		|| end == TurnEnd::MaxTokens
		|| end == TurnEnd::Refusal);
	releaseConnection(clean);
	_assembler = nullptr;
	_uses.clear();
	_replay.clear();
	_replayIndex = 0;
	_phase = Phase::Idle;
	_busy = false;
	_events.fire(TurnFinished{ end, std::move(error) });
}

void Engine::releaseConnection(bool clean) {
	auto connection = base::take(_connection);
	if (!connection) {
		return;
	}
	connection->setHandlers(nullptr, nullptr);
	if (clean
		&& _options.keepWarm
		&& _pool
		&& connection->alive()
		&& connection->initialized()) {
		connection->setKey({
			_options.owner,
			_options.recordMessages + _appended,
			_options.summaryUntil,
		});
		_pool->give(std::move(connection));
	}
}

class OneShot::NoTools final : public ToolExecutor {
public:
	[[nodiscard]] std::vector<ToolSpec> tools() const override {
		return {};
	}
	void execute(ToolUseBlock call, Fn<void(ToolResultBlock)> done) override {
		done(ToolResultBlock{ call.id, u"No tools are available."_q, true });
	}
	void cancel(const QString &toolUseId) override {
	}

};

OneShot::OneShot(
	not_null<Launcher*> launcher,
	LaunchSettings launch,
	SessionOptions options,
	std::vector<Message> messages,
	Fn<void(Response)> done)
: _tools(std::make_unique<NoTools>())
, _done(std::move(done)) {
	options.maxTurns = 1;
	const auto initial = int(messages.size());
	_engine = std::make_unique<Engine>(
		launcher,
		nullptr,
		_tools.get(),
		EngineOptions{
			.session = std::move(options),
			.launch = std::move(launch),
			.keepWarm = false,
		},
		std::move(messages));
	_engine->events(
	) | rpl::on_next([=](EngineEvent event) {
		if (const auto usage = std::get_if<UsageReported>(&event)) {
			_usage += usage->usage;
		} else if (const auto finished = std::get_if<TurnFinished>(&event)) {
			auto response = Response();
			response.usage = _usage;
			response.error = finished->error;
			const auto &history = _engine->history();
			auto message = Message{ Role::Assistant, {} };
			for (auto i = initial; i < int(history.size()); ++i) {
				if (history[i].role == Role::Assistant) {
					for (const auto &block : history[i].blocks) {
						message.blocks.push_back(block);
					}
				}
			}
			response.message = std::move(message);
			if (!response.error
				&& finished->end != TurnEnd::Completed
				&& finished->end != TurnEnd::MaxTokens) {
				response.error = Error{
					ErrorKind::BadResponse,
					0,
					u"Claude Code did not answer."_q,
				};
			}
			if (const auto done = base::take(_done)) {
				done(std::move(response));
			}
		}
	}, _lifetime);
	_engine->resume();
}

OneShot::~OneShot() = default;

ModelsQuery::ModelsQuery(
	not_null<Launcher*> launcher,
	const LaunchSettings &launch,
	Fn<void(std::vector<ModelOption> models, QString error)> done)
: _connection(std::make_unique<Connection>(
	launcher,
	launch,
	SessionOptions{ .maxTurns = 1 },
	QString()))
, _done(std::move(done)) {
	_requestId = _connection->nextRequestId();
	_connection->setHandlers([=](Line line) {
		if (const auto request = std::get_if<ControlRequest>(&line)) {
			if (request->subtype == u"mcp_message"_q) {
				const auto message = request->request.value(
					u"message"_q).toObject();
				const auto id = message.value(u"id"_q);
				auto result = (message.value(u"method"_q).toString()
					== u"initialize"_q)
					? McpResult(id, McpInitializeResult(
						message.value(u"params"_q).toObject()))
					: McpResult(id.isUndefined() ? QJsonValue(0) : id, {});
				_connection->write(ControlResponseLine(
					request->requestId,
					McpEnvelope(result)));
			} else if (request->subtype != u"control_cancel_request"_q) {
				_connection->write(ControlErrorLine(
					request->requestId,
					u"Not supported by AIgram."_q));
			}
		} else if (const auto response = std::get_if<ControlResponse>(
				&line)) {
			if (response->requestId != _requestId) {
				return;
			} else if (!response->success) {
				finish({}, response->error);
			} else {
				finish(ParseModels(response->response), QString());
			}
		}
	}, [=](QString error) {
		finish({}, ProcessError(error).text);
	});
	_connection->write(ControlRequestLine(
		_requestId,
		InitializeRequest(QString())));
}

ModelsQuery::~ModelsQuery() {
	if (_connection) {
		_connection->setHandlers(nullptr, nullptr);
	}
}

void ModelsQuery::finish(std::vector<ModelOption> models, QString error) {
	if (const auto done = base::take(_done)) {
		done(std::move(models), std::move(error));
	}
}

} // namespace Agent::Claude
