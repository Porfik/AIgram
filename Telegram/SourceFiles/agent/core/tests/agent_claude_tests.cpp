/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_claude_engine.h"
#include "agent/core/agent_claude_protocol.h"

#include <QtCore/QJsonDocument>

#include <memory>

namespace Agent::Tests {
namespace {

using namespace Agent::Claude;

[[nodiscard]] QJsonObject Parse(const QByteArray &line) {
	return QJsonDocument::fromJson(line).object();
}

[[nodiscard]] QByteArray Json(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray Line(const char *json) {
	return QByteArray(json);
}

class FakeProcess final : public Process {
public:
	explicit FakeProcess(ProcessHandlers handlers)
	: handlers(std::move(handlers)) {
	}
	~FakeProcess() {
		if (destroyed) {
			*destroyed = true;
		}
	}

	void write(const QByteArray &line) override {
		written.push_back(line);
	}

	ProcessHandlers handlers;
	std::vector<QByteArray> written;
	bool *destroyed = nullptr;

};

class FakeLauncher final : public Launcher {
public:
	[[nodiscard]] std::unique_ptr<Process> start(
			LaunchSpec spec,
			ProcessHandlers handlers) override {
		specs.push_back(spec);
		auto result = std::make_unique<FakeProcess>(std::move(handlers));
		last = result.get();
		destroyedFlags.push_back(std::make_unique<bool>(false));
		result->destroyed = destroyedFlags.back().get();
		return result;
	}

	void deliver(const QByteArray &line) {
		last->handlers.line(line);
	}
	[[nodiscard]] QJsonObject written(int index) const {
		return Parse(last->written[index]);
	}
	[[nodiscard]] int writtenCount() const {
		return int(last->written.size());
	}

	std::vector<LaunchSpec> specs;
	FakeProcess *last = nullptr;
	std::vector<std::unique_ptr<bool>> destroyedFlags;

};

class FakePool final : public ConnectionPool {
public:
	[[nodiscard]] std::unique_ptr<Connection> take(
			const QString &owner) override {
		++taken;
		return std::move(stored);
	}
	void give(std::unique_ptr<Connection> connection) override {
		stored = std::move(connection);
	}

	std::unique_ptr<Connection> stored;
	int taken = 0;

};

class FakeTools final : public ToolExecutor {
public:
	[[nodiscard]] std::vector<ToolSpec> tools() const override {
		return specs;
	}
	void execute(ToolUseBlock call, Fn<void(ToolResultBlock)> done) override {
		calls.push_back(call);
		pending.push_back(std::move(done));
	}
	void cancel(const QString &toolUseId) override {
		cancelled.push_back(toolUseId);
	}

	std::vector<ToolSpec> specs = {
		{ u"get_time"_q, u"Time."_q, QJsonObject() },
	};
	std::vector<ToolUseBlock> calls;
	std::vector<Fn<void(ToolResultBlock)>> pending;
	std::vector<QString> cancelled;

};

[[nodiscard]] Message UserText(const QString &text) {
	return Message{ Role::User, { TextBlock{ text } } };
}

[[nodiscard]] Message AssistantText(const QString &text) {
	return Message{ Role::Assistant, { TextBlock{ text } } };
}

[[nodiscard]] QByteArray StreamLine(const QByteArray &event) {
	return "{\"type\":\"stream_event\",\"parent_tool_use_id\":null,"
		"\"event\":" + event + "}";
}

void StreamText(FakeLauncher &launcher, const QString &text) {
	launcher.deliver(StreamLine(
		"{\"type\":\"message_start\",\"message\":{\"usage\":"
		"{\"input_tokens\":10,\"cache_read_input_tokens\":5,"
		"\"output_tokens\":1}}}"));
	launcher.deliver(StreamLine(
		"{\"type\":\"content_block_start\",\"index\":0,"
		"\"content_block\":{\"type\":\"text\",\"text\":\"\"}}"));
	auto delta = QJsonObject();
	delta.insert(u"type"_q, u"text_delta"_q);
	delta.insert(u"text"_q, text);
	auto event = QJsonObject();
	event.insert(u"type"_q, u"content_block_delta"_q);
	event.insert(u"index"_q, 0);
	event.insert(u"delta"_q, delta);
	launcher.deliver(StreamLine(Json(event)));
	launcher.deliver(StreamLine(
		"{\"type\":\"content_block_stop\",\"index\":0}"));
	launcher.deliver(StreamLine(
		"{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":"
		"\"end_turn\"},\"usage\":{\"output_tokens\":7}}"));
	launcher.deliver(StreamLine("{\"type\":\"message_stop\"}"));
}

void StreamToolUse(FakeLauncher &launcher, const QString &id) {
	launcher.deliver(StreamLine(
		"{\"type\":\"message_start\",\"message\":{\"usage\":"
		"{\"input_tokens\":10,\"output_tokens\":1}}}"));
	auto block = QJsonObject();
	block.insert(u"type"_q, u"tool_use"_q);
	block.insert(u"id"_q, id);
	block.insert(u"name"_q, u"mcp__aigram__get_time"_q);
	block.insert(u"input"_q, QJsonObject());
	auto start = QJsonObject();
	start.insert(u"type"_q, u"content_block_start"_q);
	start.insert(u"index"_q, 0);
	start.insert(u"content_block"_q, block);
	launcher.deliver(StreamLine(Json(start)));
	launcher.deliver(StreamLine(
		"{\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
		"{\"type\":\"input_json_delta\",\"partial_json\":\"{}\"}}"));
	launcher.deliver(StreamLine(
		"{\"type\":\"content_block_stop\",\"index\":0}"));
	launcher.deliver(StreamLine(
		"{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":"
		"\"tool_use\"},\"usage\":{\"output_tokens\":12}}"));
	launcher.deliver(StreamLine("{\"type\":\"message_stop\"}"));
}

void ToolCall(FakeLauncher &launcher, const QString &id, int rpcId) {
	auto meta = QJsonObject();
	meta.insert(u"claudecode/toolUseId"_q, id);
	auto params = QJsonObject();
	params.insert(u"name"_q, u"get_time"_q);
	params.insert(u"arguments"_q, QJsonObject());
	params.insert(u"_meta"_q, meta);
	auto message = QJsonObject();
	message.insert(u"jsonrpc"_q, u"2.0"_q);
	message.insert(u"id"_q, rpcId);
	message.insert(u"method"_q, u"tools/call"_q);
	message.insert(u"params"_q, params);
	auto request = QJsonObject();
	request.insert(u"subtype"_q, u"mcp_message"_q);
	request.insert(u"server_name"_q, u"aigram"_q);
	request.insert(u"message"_q, message);
	auto root = QJsonObject();
	root.insert(u"type"_q, u"control_request"_q);
	root.insert(u"request_id"_q, u"cli_%1"_q.arg(rpcId));
	root.insert(u"request"_q, request);
	launcher.deliver(Json(root));
}

void Result(FakeLauncher &launcher, int turns, bool error = false) {
	auto root = QJsonObject();
	root.insert(u"type"_q, u"result"_q);
	root.insert(u"subtype"_q, u"success"_q);
	root.insert(u"is_error"_q, error);
	root.insert(u"num_turns"_q, turns);
	root.insert(u"result"_q, error ? u"Not logged in"_q : QString());
	launcher.deliver(Json(root));
}

void Initialized(FakeLauncher &launcher) {
	const auto request = launcher.written(0);
	auto response = QJsonObject();
	response.insert(u"subtype"_q, u"success"_q);
	response.insert(
		u"request_id"_q,
		request.value(u"request_id"_q).toString());
	response.insert(u"response"_q, QJsonObject());
	auto root = QJsonObject();
	root.insert(u"type"_q, u"control_response"_q);
	root.insert(u"response"_q, response);
	launcher.deliver(Json(root));
}

struct Recorder {
	std::vector<EngineEvent> events;
	rpl::lifetime lifetime;

	void attach(Agent::Engine &engine) {
		engine.events() | rpl::on_next([this](EngineEvent event) {
			events.push_back(std::move(event));
		}, lifetime);
	}
	[[nodiscard]] std::optional<TurnFinished> finished() const {
		for (const auto &event : events) {
			if (const auto value = std::get_if<TurnFinished>(&event)) {
				return *value;
			}
		}
		return std::nullopt;
	}
	template <typename T>
	[[nodiscard]] int count() const {
		auto result = 0;
		for (const auto &event : events) {
			if (std::get_if<T>(&event)) {
				++result;
			}
		}
		return result;
	}
};

[[nodiscard]] EngineOptions TestOptions(int recordMessages) {
	return EngineOptions{
		.session = { u"haiku"_q, QString(), u"system"_q, 25 },
		.launch = { u"/usr/bin/claude"_q, u"/tmp/aigram-claude"_q, {} },
		.owner = u"session1"_q,
		.recordMessages = recordMessages,
		.summaryUntil = 0,
	};
}

[[nodiscard]] QString ResponseTextOf(const Message &message) {
	auto result = QString();
	for (const auto &block : message.blocks) {
		if (const auto text = std::get_if<TextBlock>(&block)) {
			result += text->text;
		}
	}
	return result;
}

} // namespace

std::vector<Check> ClaudeChecks() {
	return {
		{ u"claude_launch_arguments"_q, [] {
			const auto args = LaunchArguments(
				{ u"opus"_q, u"high"_q, u"system"_q, 7 });
			const auto has = [&](const QString &flag, const QString &value) {
				const auto i = args.indexOf(flag);
				return (i >= 0) && (i + 1 < args.size()) && args[i + 1] == value;
			};
			if (!args.contains(u"-p"_q)
				|| !has(u"--input-format"_q, u"stream-json"_q)
				|| !has(u"--output-format"_q, u"stream-json"_q)
				|| !args.contains(u"--verbose"_q)
				|| !args.contains(u"--include-partial-messages"_q)
				|| !has(u"--tools"_q, QString())
				|| !has(u"--setting-sources"_q, QString())
				|| !args.contains(u"--strict-mcp-config"_q)
				|| !args.contains(u"--no-session-persistence"_q)
				|| !args.contains(u"--disable-slash-commands"_q)
				|| !has(u"--permission-prompt-tool"_q, u"stdio"_q)
				|| !has(u"--thinking-display"_q, u"summarized"_q)
				|| !has(u"--max-turns"_q, u"7"_q)
				|| !has(u"--model"_q, u"opus"_q)
				|| !has(u"--effort"_q, u"high"_q)) {
				return u"missing flags: "_q + args.join(' ');
			}
			const auto config = QJsonDocument::fromJson(
				args[args.indexOf(u"--mcp-config"_q) + 1].toUtf8()).object();
			const auto server = config.value(u"mcpServers"_q).toObject()
				.value(u"aigram"_q).toObject();
			if (server.value(u"type"_q).toString() != u"sdk"_q
				|| !server.value(u"alwaysLoad"_q).toBool()) {
				return u"bad mcp config"_q;
			}
			const auto noEffort = LaunchArguments(
				{ u"opus"_q, QString(), u"s"_q, 25 });
			return noEffort.contains(u"--effort"_q)
				? u"empty effort must not be passed"_q
				: QString();
		} },
		{ u"claude_initialize_request"_q, [] {
			const auto request = InitializeRequest(u"Prompt"_q);
			const auto config = request.value(u"sdkMcpServerConfigs"_q)
				.toObject().value(u"aigram"_q).toObject();
			if (request.value(u"subtype"_q).toString() != u"initialize"_q
				|| request.value(u"sdkMcpServers"_q).toArray()
					!= QJsonArray{ u"aigram"_q }
				|| !config.value(u"disableAutoBackground"_q).toBool()
				|| config.value(u"timeout"_q).toInt() != kToolTimeoutMs
				|| request.value(u"systemPrompt"_q).toArray()
					!= QJsonArray{ u"Prompt"_q }) {
				return u"bad initialize request"_q;
			}
			const auto empty = InitializeRequest(QString());
			return empty.value(u"systemPrompt"_q).toArray()[0]
				.toString().isEmpty()
				? u"an empty system prompt must get a default"_q
				: QString();
		} },
		{ u"claude_replay_frames"_q, [] {
			auto image = MediaBlock();
			image.kind = MediaKind::Image;
			image.bytes = "jpeg";
			image.mimeType = u"image/jpeg"_q;
			auto audio = MediaBlock();
			audio.kind = MediaKind::Audio;
			audio.bytes = "wav";
			audio.mimeType = u"audio/wav"_q;
			auto first = ToolUseBlock();
			first.id = u"call_0"_q;
			first.name = u"get_time"_q;
			auto second = first;
			const auto messages = std::vector<Message>{
				Message{ Role::User, {
					TextBlock{ u"hi"_q },
					TextBlock{ u"[Context] hint"_q, true },
					image,
					audio,
				} },
				Message{ Role::Assistant, {
					ThinkingBlock{ u"think"_q, "sig" },
					TextBlock{ u"Checking."_q },
					first,
				} },
				Message{ Role::User, {
					ToolResultBlock{ u"call_0"_q, u"10:00"_q, false },
				} },
				Message{ Role::Assistant, { ThinkingBlock{ u"x"_q, "y" } } },
				Message{ Role::Assistant, { TextBlock{ u"Again."_q } } },
				Message{ Role::Assistant, { second } },
				Message{ Role::User, {
					TextBlock{ u"after"_q },
					ToolResultBlock{ u"call_0"_q, u"11:00"_q, true },
				} },
			};
			const auto frames = ReplayFrames(messages);
			if (frames.size() != 5) {
				return u"expected 5 frames, got %1"_q.arg(frames.size());
			}
			const auto user = frames[0].content;
			if (user.size() != 3
				|| user[1].toObject().value(u"text"_q).toString()
					!= u"[Context] hint"_q
				|| user[2].toObject().value(u"type"_q).toString()
					!= u"image"_q) {
				return u"bad first user frame"_q;
			}
			const auto assistant = frames[1].content;
			if (assistant.size() != 2
				|| assistant[1].toObject().value(u"name"_q).toString()
					!= u"mcp__aigram__get_time"_q
				|| assistant[1].toObject().value(u"id"_q).toString()
					!= u"call_0"_q) {
				return u"bad assistant frame"_q;
			}
			const auto merged = frames[3];
			if (merged.role != Role::Assistant || merged.content.size() != 2) {
				return u"consecutive assistant messages must merge"_q;
			}
			const auto remapped = merged.content[1].toObject()
				.value(u"id"_q).toString();
			if (remapped == u"call_0"_q || remapped.isEmpty()) {
				return u"repeated id must be remapped"_q;
			}
			const auto last = frames[4].content;
			if (last.size() != 2
				|| last[0].toObject().value(u"type"_q).toString()
					!= u"tool_result"_q
				|| last[0].toObject().value(u"tool_use_id"_q).toString()
					!= remapped
				|| !last[0].toObject().value(u"is_error"_q).toBool()) {
				return u"bad last frame"_q;
			}
			return (frames[1].content[0].toObject()
				.value(u"type"_q).toString() == u"text"_q)
				? QString()
				: u"thinking must be dropped"_q;
		} },
		{ u"claude_replay_drops_thinking"_q, [] {
			const auto messages = std::vector<Message>{
				Message{ Role::User, { TextBlock{ u"q"_q } } },
				Message{ Role::Assistant, {
					ThinkingBlock{ u"think"_q, "sig" },
					TextBlock{ u"answer"_q },
				} },
				Message{ Role::Assistant, {
					ThinkingBlock{ u"more"_q, "{json" },
				} },
			};
			const auto frames = ReplayFrames(messages);
			if (frames.size() != 2) {
				return u"expected 2 frames, got %1"_q.arg(frames.size());
			}
			if (frames[0].role != Role::User
				|| frames[0].content.size() != 1) {
				return u"bad user frame"_q;
			}
			const auto parts = frames[1].content;
			if (parts.size() != 1
				|| parts[0].toObject().value(u"type"_q).toString()
					!= u"text"_q
				|| parts[0].toObject().value(u"text"_q).toString()
					!= u"answer"_q) {
				return u"thinking must be dropped"_q;
			}
			return QString();
		} },
		{ u"claude_parse_lines"_q, [] {
			const auto request = ParseLine(Line(
				"{\"type\":\"control_request\",\"request_id\":\"r1\","
				"\"request\":{\"subtype\":\"can_use_tool\","
				"\"tool_name\":\"mcp__aigram__x\",\"input\":{}}}"));
			const auto control = std::get_if<ControlRequest>(&request);
			if (!control
				|| control->requestId != u"r1"_q
				|| control->subtype != u"can_use_tool"_q) {
				return u"control request"_q;
			}
			const auto nested = ParseLine(Line(
				"{\"type\":\"stream_event\",\"parent_tool_use_id\":\"t\","
				"\"event\":{\"type\":\"ping\"}}"));
			const auto event = std::get_if<StreamEvent>(&nested);
			if (!event || !event->nested) {
				return u"nested stream event"_q;
			}
			const auto echo = ParseLine(Line(
				"{\"type\":\"assistant\",\"error\":\"authentication_failed\","
				"\"message\":{\"content\":[{\"type\":\"text\","
				"\"text\":\"Not logged in\"}]}}"));
			const auto assistant = std::get_if<AssistantEcho>(&echo);
			if (!assistant
				|| assistant->error != u"authentication_failed"_q
				|| assistant->text != u"Not logged in"_q) {
				return u"assistant echo"_q;
			}
			const auto user = ParseLine(Line(
				"{\"type\":\"user\",\"message\":{\"content\":[{\"type\":"
				"\"tool_result\",\"tool_use_id\":\"t1\",\"is_error\":true,"
				"\"content\":[{\"type\":\"text\",\"text\":\"denied\"}]}]}}"));
			const auto results = std::get_if<UserEcho>(&user);
			if (!results
				|| results->results.size() != 1
				|| results->results[0].toolUseId != u"t1"_q
				|| results->results[0].content != u"denied"_q
				|| !results->results[0].error) {
				return u"user echo"_q;
			}
			const auto limits = ParseLine(Line(
				"{\"type\":\"rate_limit_event\",\"rate_limit_info\":{"
				"\"status\":\"allowed\",\"unifiedWindows\":{\"five_hour\":"
				"{\"utilization\":0.13,\"resetsAt\":1792538400},"
				"\"seven_day\":{\"utilization\":0.1,"
				"\"resetsAt\":1792713600}}}}"));
			const auto parsed = std::get_if<Limits>(&limits);
			if (!parsed
				|| parsed->status != u"allowed"_q
				|| parsed->fiveHour.utilization != 0.13
				|| parsed->fiveHour.resetsAt != 1792538400
				|| parsed->sevenDay.resetsAt != 1792713600) {
				return u"limits"_q;
			}
			const auto result = ParseLine(Line(
				"{\"type\":\"result\",\"subtype\":\"error_during_execution\","
				"\"is_error\":true,\"num_turns\":3,\"errors\":[\"x\"]}"));
			const auto line = std::get_if<ResultLine>(&result);
			if (!line || !line->isError || line->numTurns != 3
				|| line->errors != QStringList{ u"x"_q }) {
				return u"result"_q;
			}
			return std::holds_alternative<std::monostate>(
				ParseLine("not json"))
				? QString()
				: u"garbage must give monostate"_q;
		} },
		{ u"claude_models_auth_version"_q, [] {
			const auto models = ParseModels(Parse(
				"{\"models\":[{\"value\":\"opus\",\"resolvedModel\":"
				"\"claude-opus-5-5\",\"displayName\":\"Opus 5.5\","
				"\"description\":\"d\",\"supportedEffortLevels\":"
				"[\"low\",\"max\"]},{\"displayName\":\"no value\"}]}"));
			if (models.size() != 1
				|| models[0].resolved != u"claude-opus-5-5"_q
				|| models[0].effortLevels.size() != 2) {
				return u"models"_q;
			}
			const auto status = ParseAuthStatus(
				"{\"loggedIn\":true,\"authMethod\":\"claude.ai\","
				"\"email\":\"a@b.c\",\"subscriptionType\":\"pro\"}\n");
			if (!status || !status->loggedIn
				|| status->subscription != u"pro"_q) {
				return u"auth status"_q;
			}
			if (ParseAuthStatus("{}") || ParseAuthStatus("oops")) {
				return u"bad auth status must fail"_q;
			}
			const auto version = ParseVersion("2.1.304 (Claude Code)\n");
			if (!version || !VersionSupported(*version)) {
				return u"version"_q;
			}
			const auto old = ParseVersion("2.0.99 (Claude Code)");
			return (old && !VersionSupported(*old))
				? QString()
				: u"old version must be unsupported"_q;
		} },
		{ u"claude_mcp_helpers"_q, [] {
			const auto list = McpToolsList({
				{ u"a"_q, u"A."_q, QJsonObject() },
			}).value(u"tools"_q).toArray();
			const auto meta = list[0].toObject().value(u"_meta"_q)
				.toObject();
			if (list.size() != 1
				|| meta.value(u"anthropic/maxResultSizeChars"_q).toInt()
					!= kMaxResultChars) {
				return u"tools list"_q;
			}
			auto result = ToolResultBlock{ u"t"_q, u"text"_q, true };
			auto image = MediaBlock();
			image.kind = MediaKind::Image;
			image.bytes = "png";
			image.mimeType = u"image/png"_q;
			auto pdf = MediaBlock();
			pdf.kind = MediaKind::File;
			pdf.bytes = "pdf";
			pdf.mimeType = u"application/pdf"_q;
			result.media = { image, pdf };
			const auto mcp = McpToolResult(result);
			const auto content = mcp.value(u"content"_q).toArray();
			if (content.size() != 2
				|| !mcp.value(u"isError"_q).toBool()
				|| content[1].toObject().value(u"type"_q).toString()
					!= u"image"_q
				|| !content[0].toObject().value(u"text"_q).toString()
					.contains(u"non-image"_q)) {
				return u"tool result: "_q + QString::fromUtf8(Json(mcp));
			}
			return QString();
		} },
		{ u"claude_result_errors"_q, [] {
			auto result = ResultLine();
			result.isError = true;
			result.result = u"Not logged in"_q;
			if (ResultError(result, u"authentication_failed"_q).kind
				!= ErrorKind::Auth
				|| ResultError(result, u"rate_limit"_q).kind
					!= ErrorKind::RateLimited
				|| ResultError(result, u"overloaded"_q).kind
					!= ErrorKind::Overloaded) {
				return u"assistant error kinds"_q;
			}
			result.apiErrorStatus = 400;
			const auto bad = ResultError(result, QString());
			return (bad.kind == ErrorKind::BadRequest
				&& bad.text == u"Not logged in"_q)
				? QString()
				: u"status kinds"_q;
		} },
		{ u"claude_engine_fresh_turn"_q, [] {
			auto launcher = FakeLauncher();
			auto pool = FakePool();
			auto tools = FakeTools();
			auto engine = Claude::Engine(&launcher, &pool, &tools, TestOptions(3), {
				UserText(u"first"_q),
				AssistantText(u"answer"_q),
				UserText(u"second"_q),
			});
			auto recorder = Recorder();
			recorder.attach(engine);
			engine.resume();
			if (launcher.specs.size() != 1
				|| launcher.writtenCount() != 1
				|| launcher.written(0).value(u"request"_q).toObject()
					.value(u"subtype"_q).toString() != u"initialize"_q) {
				return u"expected one process and initialize"_q;
			}
			Initialized(launcher);
			if (launcher.writtenCount() != 2
				|| launcher.written(1).value(u"shouldQuery"_q).toBool(true)) {
				return u"first frame must not query"_q;
			}
			Result(launcher, 0);
			if (launcher.writtenCount() != 4
				|| launcher.written(2).value(u"type"_q).toString()
					!= u"assistant"_q
				|| launcher.written(3).contains(u"shouldQuery"_q)) {
				return u"expected the assistant frame and the query"_q;
			}
			StreamText(launcher, u"Hello"_q);
			Result(launcher, 1);
			const auto finished = recorder.finished();
			if (!finished || finished->end != TurnEnd::Completed) {
				return u"turn must complete"_q;
			} else if (engine.history().size() != 4
				|| engine.history().back().role != Role::Assistant) {
				return u"history must get the answer"_q;
			} else if (recorder.count<TextDelta>() < 1
				|| recorder.count<UsageReported>() != 1) {
				return u"deltas and usage expected"_q;
			} else if (!pool.stored
				|| pool.stored->key() != ConnectionKey{
					u"session1"_q, 4, 0 }) {
				return u"connection must go to the pool with the key"_q;
			}
			auto usage = Usage();
			for (const auto &event : recorder.events) {
				if (const auto value = std::get_if<UsageReported>(&event)) {
					usage = value->usage;
				}
			}
			return (usage.inputTokens == 15
				&& usage.cachedInputTokens == 5
				&& usage.outputTokens == 7)
				? QString()
				: u"bad usage"_q;
		} },
		{ u"claude_engine_tool_call"_q, [] {
			auto launcher = FakeLauncher();
			auto pool = FakePool();
			auto tools = FakeTools();
			auto engine = Claude::Engine(&launcher, &pool, &tools, TestOptions(1), {
				UserText(u"time?"_q),
			});
			auto recorder = Recorder();
			recorder.attach(engine);
			engine.resume();
			Initialized(launcher);
			StreamToolUse(launcher, u"toolu_1"_q);
			launcher.deliver(Line(
				"{\"type\":\"control_request\",\"request_id\":\"p1\","
				"\"request\":{\"subtype\":\"can_use_tool\","
				"\"tool_name\":\"mcp__aigram__get_time\",\"input\":{}}}"));
			const auto permission = launcher.written(
				launcher.writtenCount() - 1).value(u"response"_q).toObject()
				.value(u"response"_q).toObject();
			if (permission.value(u"behavior"_q).toString() != u"allow"_q) {
				return u"own tool must be allowed"_q;
			}
			launcher.deliver(Line(
				"{\"type\":\"control_request\",\"request_id\":\"p2\","
				"\"request\":{\"subtype\":\"can_use_tool\","
				"\"tool_name\":\"Bash\",\"input\":{}}}"));
			const auto denied = launcher.written(
				launcher.writtenCount() - 1).value(u"response"_q).toObject()
				.value(u"response"_q).toObject();
			if (denied.value(u"behavior"_q).toString() != u"deny"_q) {
				return u"foreign tool must be denied"_q;
			}
			ToolCall(launcher, u"toolu_1"_q, 5);
			if (tools.calls.size() != 1
				|| tools.calls[0].name != u"get_time"_q
				|| tools.calls[0].id != u"toolu_1"_q) {
				return u"executor must get the call"_q;
			}
			tools.specs.push_back({ u"extra"_q, u"Loaded."_q, {} });
			const auto before = launcher.writtenCount();
			tools.pending[0](ToolResultBlock{ u"toolu_1"_q, u"10:00"_q });
			const auto answer = launcher.written(before)
				.value(u"response"_q).toObject()
				.value(u"response"_q).toObject()
				.value(u"mcp_response"_q).toObject();
			if (answer.value(u"id"_q).toInt() != 5
				|| answer.value(u"result"_q).toObject()
					.value(u"content"_q).toArray()[0].toObject()
					.value(u"text"_q).toString() != u"10:00"_q) {
				return u"MCP answer expected"_q;
			}
			const auto notice = launcher.written(before + 1)
				.value(u"request"_q).toObject()
				.value(u"message"_q).toObject();
			if (notice.value(u"method"_q).toString()
				!= u"notifications/tools/list_changed"_q) {
				return u"tools change must be announced"_q;
			}
			if (engine.history().size() != 3
				|| engine.history()[2].role != Role::User) {
				return u"results message expected"_q;
			}
			StreamText(launcher, u"It is ten."_q);
			Result(launcher, 2);
			const auto finished = recorder.finished();
			return (finished
				&& finished->end == TurnEnd::Completed
				&& engine.history().size() == 4
				&& recorder.count<ToolCallRequested>() == 1
				&& recorder.count<ToolCallFinished>() == 1)
				? QString()
				: u"turn must complete after the tool"_q;
		} },
		{ u"claude_engine_warm_reuse"_q, [] {
			auto launcher = FakeLauncher();
			auto pool = FakePool();
			auto tools = FakeTools();
			{
				auto engine = Claude::Engine(&launcher, &pool, &tools, TestOptions(1), {
					UserText(u"one"_q),
				});
				engine.resume();
				Initialized(launcher);
				StreamText(launcher, u"A"_q);
				Result(launcher, 1);
			}
			if (!pool.stored) {
				return u"first connection must be kept"_q;
			}
			const auto written = launcher.writtenCount();
			auto engine = Claude::Engine(&launcher, &pool, &tools, TestOptions(3), {
				UserText(u"one"_q),
				AssistantText(u"A"_q),
				UserText(u"two"_q),
			});
			auto recorder = Recorder();
			recorder.attach(engine);
			engine.resume();
			if (launcher.specs.size() != 1
				|| launcher.writtenCount() != written + 1
				|| launcher.written(written).value(u"type"_q).toString()
					!= u"user"_q) {
				return u"warm connection must get only the query"_q;
			}
			StreamText(launcher, u"B"_q);
			Result(launcher, 1);
			if (!recorder.finished()) {
				return u"turn must finish"_q;
			}
			auto other = Claude::Engine(&launcher, &pool, &tools, TestOptions(9), {
				UserText(u"mismatch"_q),
			});
			other.resume();
			return (launcher.specs.size() == 2)
				? QString()
				: u"a key mismatch must start a new process"_q;
		} },
		{ u"claude_engine_interrupt"_q, [] {
			auto launcher = FakeLauncher();
			auto pool = FakePool();
			auto tools = FakeTools();
			auto engine = Claude::Engine(&launcher, &pool, &tools, TestOptions(1), {
				UserText(u"time?"_q),
			});
			auto recorder = Recorder();
			recorder.attach(engine);
			engine.resume();
			Initialized(launcher);
			StreamToolUse(launcher, u"toolu_1"_q);
			ToolCall(launcher, u"toolu_1"_q, 2);
			const auto destroyed = launcher.destroyedFlags.back().get();
			engine.interrupt();
			const auto finished = recorder.finished();
			if (!finished || finished->end != TurnEnd::Interrupted) {
				return u"turn must be interrupted"_q;
			} else if (tools.cancelled != std::vector<QString>{ u"toolu_1"_q }) {
				return u"the running tool must be cancelled"_q;
			} else if (!*destroyed || pool.stored) {
				return u"the connection must be closed, not pooled"_q;
			}
			const auto &last = engine.history().back();
			const auto result = std::get_if<ToolResultBlock>(
				&last.blocks.front());
			return (result && result->error)
				? QString()
				: u"an interrupted result is expected"_q;
		} },
		{ u"claude_engine_errors"_q, [] {
			auto launcher = FakeLauncher();
			auto tools = FakeTools();
			auto engine = Claude::Engine(&launcher, nullptr, &tools, TestOptions(1), {
				UserText(u"hi"_q),
			});
			auto recorder = Recorder();
			recorder.attach(engine);
			engine.resume();
			Initialized(launcher);
			launcher.deliver(Line(
				"{\"type\":\"assistant\",\"error\":\"authentication_failed\","
				"\"message\":{\"content\":[{\"type\":\"text\","
				"\"text\":\"Not logged in\"}]}}"));
			Result(launcher, 1, true);
			const auto finished = recorder.finished();
			if (!finished
				|| finished->end != TurnEnd::Failed
				|| !finished->error
				|| finished->error->kind != ErrorKind::Auth) {
				return u"not logged in must fail with Auth"_q;
			}
			auto second = Claude::Engine(&launcher, nullptr, &tools, TestOptions(1), {
				UserText(u"hi"_q),
			});
			auto events = Recorder();
			events.attach(second);
			second.resume();
			launcher.last->handlers.finished(u"crashed"_q);
			const auto exit = events.finished();
			return (exit
				&& exit->end == TurnEnd::Failed
				&& exit->error
				&& exit->error->kind == ErrorKind::Network)
				? QString()
				: u"process exit must fail the turn"_q;
		} },
		{ u"claude_one_shot"_q, [] {
			auto launcher = FakeLauncher();
			auto response = std::optional<Response>();
			auto request = OneShot(
				&launcher,
				{ u"/usr/bin/claude"_q },
				{ .model = u"haiku"_q, .system = u"Summarize."_q },
				{ UserText(u"long text"_q) },
				[&](Response result) { response = std::move(result); });
			Initialized(launcher);
			StreamText(launcher, u"Summary."_q);
			Result(launcher, 1);
			if (!response || response->error) {
				return u"one shot must answer"_q;
			}
			const auto args = launcher.specs.front().arguments;
			const auto turns = args[args.indexOf(u"--max-turns"_q) + 1];
			return (ResponseTextOf(response->message) == u"Summary."_q
				&& turns == u"1"_q)
				? QString()
				: u"bad one shot response"_q;
		} },
	};
}

} // namespace Agent::Tests
