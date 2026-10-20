/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_claude_protocol.h"

#include "base/flat_map.h"
#include "base/flat_set.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>

namespace Agent::Claude {
namespace {

constexpr auto kDefaultSystem = "You are a helpful assistant.";

[[nodiscard]] QByteArray Compact(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QJsonObject TextPart(const QString &text) {
	auto result = QJsonObject();
	result.insert(u"type"_q, u"text"_q);
	result.insert(u"text"_q, text);
	return result;
}

[[nodiscard]] QJsonObject Base64Source(const MediaBlock &media) {
	auto result = QJsonObject();
	result.insert(u"type"_q, u"base64"_q);
	result.insert(u"media_type"_q, media.mimeType);
	result.insert(
		u"data"_q,
		QString::fromLatin1(media.bytes.toBase64()));
	return result;
}

[[nodiscard]] std::optional<QJsonObject> MediaPart(
		const MediaBlock &media,
		bool documents) {
	if (media.bytes.isEmpty()) {
		return std::nullopt;
	} else if (media.kind == MediaKind::Image) {
		auto result = QJsonObject();
		result.insert(u"type"_q, u"image"_q);
		result.insert(u"source"_q, Base64Source(media));
		return result;
	} else if (documents
		&& media.kind == MediaKind::File
		&& media.mimeType == u"application/pdf"_q) {
		auto result = QJsonObject();
		result.insert(u"type"_q, u"document"_q);
		result.insert(u"source"_q, Base64Source(media));
		return result;
	}
	return std::nullopt;
}

class IdMapper final {
public:
	[[nodiscard]] QString use(
		const QString &id,
		int messageIndex,
		int blockIndex);
	[[nodiscard]] QString result(const QString &id) const;

private:
	base::flat_set<QString> _seen;
	base::flat_map<QString, QString> _current;

};

QString IdMapper::use(const QString &id, int messageIndex, int blockIndex) {
	static const auto valid = QRegularExpression(u"^[A-Za-z0-9_-]+$"_q);
	auto mapped = id;
	if (!valid.match(id).hasMatch() || _seen.contains(id)) {
		mapped = u"toolu_aigram_%1_%2"_q.arg(messageIndex).arg(blockIndex);
	}
	_seen.emplace(id);
	_seen.emplace(mapped);
	_current[id] = mapped;
	return mapped;
}

QString IdMapper::result(const QString &id) const {
	const auto i = _current.find(id);
	return (i != _current.end()) ? i->second : id;
}

[[nodiscard]] QJsonObject ToolResultPart(
		const ToolResultBlock &result,
		const QString &id) {
	auto content = QJsonArray();
	content.append(TextPart(result.content));
	for (const auto &media : result.media) {
		if (auto part = MediaPart(media, false)) {
			content.append(*part);
		}
	}
	auto part = QJsonObject();
	part.insert(u"type"_q, u"tool_result"_q);
	part.insert(u"tool_use_id"_q, id);
	part.insert(u"content"_q, content);
	if (result.error) {
		part.insert(u"is_error"_q, true);
	}
	return part;
}

[[nodiscard]] QJsonArray UserParts(
		const Message &message,
		const IdMapper *ids) {
	auto results = QJsonArray();
	auto other = QJsonArray();
	for (const auto &block : message.blocks) {
		if (const auto text = std::get_if<TextBlock>(&block)) {
			if (!text->text.isEmpty()) {
				other.append(TextPart(text->text));
			}
		} else if (const auto media = std::get_if<MediaBlock>(&block)) {
			if (auto part = MediaPart(*media, true)) {
				other.append(*part);
			}
		} else if (const auto result = std::get_if<ToolResultBlock>(&block)) {
			const auto id = ids
				? ids->result(result->toolUseId)
				: result->toolUseId;
			results.append(ToolResultPart(*result, id));
		}
	}
	for (const auto &part : other) {
		results.append(part);
	}
	return results;
}

[[nodiscard]] QJsonArray AssistantParts(
		const Message &message,
		int messageIndex,
		IdMapper &ids) {
	auto result = QJsonArray();
	for (auto i = 0, count = int(message.blocks.size()); i != count; ++i) {
		const auto &block = message.blocks[i];
		if (const auto text = std::get_if<TextBlock>(&block)) {
			if (!text->text.isEmpty()) {
				result.append(TextPart(text->text));
			}
		} else if (const auto use = std::get_if<ToolUseBlock>(&block)) {
			auto part = QJsonObject();
			part.insert(u"type"_q, u"tool_use"_q);
			part.insert(u"id"_q, ids.use(use->id, messageIndex, i));
			part.insert(u"name"_q, ToolName(use->name));
			part.insert(
				u"input"_q,
				use->inputError.isEmpty() ? use->input : QJsonObject());
			result.append(part);
		}
	}
	return result;
}

} // namespace

QString ServerName() {
	return u"aigram"_q;
}

QString ToolName(const QString &name) {
	return u"mcp__"_q + ServerName() + u"__"_q + name;
}

QString OwnToolName(const QString &qualified) {
	const auto prefix = u"mcp__"_q + ServerName() + u"__"_q;
	return qualified.startsWith(prefix)
		? qualified.mid(prefix.size())
		: QString();
}

QStringList LaunchArguments(const SessionOptions &options) {
	auto server = QJsonObject();
	server.insert(u"type"_q, u"sdk"_q);
	server.insert(u"name"_q, ServerName());
	server.insert(u"alwaysLoad"_q, true);
	auto servers = QJsonObject();
	servers.insert(ServerName(), server);
	auto config = QJsonObject();
	config.insert(u"mcpServers"_q, servers);

	auto result = QStringList{
		u"-p"_q,
		u"--input-format"_q,
		u"stream-json"_q,
		u"--output-format"_q,
		u"stream-json"_q,
		u"--verbose"_q,
		u"--include-partial-messages"_q,
		u"--tools"_q,
		QString(),
		u"--strict-mcp-config"_q,
		u"--mcp-config"_q,
		QString::fromUtf8(Compact(config)),
		u"--setting-sources"_q,
		QString(),
		u"--no-session-persistence"_q,
		u"--disable-slash-commands"_q,
		u"--permission-prompt-tool"_q,
		u"stdio"_q,
		u"--thinking-display"_q,
		u"summarized"_q,
		u"--max-turns"_q,
		QString::number(std::max(options.maxTurns, 1)),
	};
	if (!options.model.isEmpty()) {
		result.append({ u"--model"_q, options.model });
	}
	if (!options.effort.isEmpty()) {
		result.append({ u"--effort"_q, options.effort });
	}
	return result;
}

QJsonObject InitializeRequest(const QString &system) {
	auto config = QJsonObject();
	config.insert(u"timeout"_q, kToolTimeoutMs);
	config.insert(u"disableAutoBackground"_q, true);
	auto configs = QJsonObject();
	configs.insert(ServerName(), config);

	auto result = QJsonObject();
	result.insert(u"subtype"_q, u"initialize"_q);
	result.insert(u"sdkMcpServers"_q, QJsonArray{ ServerName() });
	result.insert(u"sdkMcpServerConfigs"_q, configs);
	result.insert(
		u"systemPrompt"_q,
		QJsonArray{
			system.trimmed().isEmpty()
				? QString::fromLatin1(kDefaultSystem)
				: system,
		});
	return result;
}

QByteArray ControlRequestLine(
		const QString &requestId,
		const QJsonObject &request) {
	auto result = QJsonObject();
	result.insert(u"type"_q, u"control_request"_q);
	result.insert(u"request_id"_q, requestId);
	result.insert(u"request"_q, request);
	return Compact(result);
}

QByteArray ControlResponseLine(
		const QString &requestId,
		const QJsonObject &response) {
	auto inner = QJsonObject();
	inner.insert(u"subtype"_q, u"success"_q);
	inner.insert(u"request_id"_q, requestId);
	inner.insert(u"response"_q, response);
	auto result = QJsonObject();
	result.insert(u"type"_q, u"control_response"_q);
	result.insert(u"response"_q, inner);
	return Compact(result);
}

QByteArray ControlErrorLine(const QString &requestId, const QString &error) {
	auto inner = QJsonObject();
	inner.insert(u"subtype"_q, u"error"_q);
	inner.insert(u"request_id"_q, requestId);
	inner.insert(u"error"_q, error);
	auto result = QJsonObject();
	result.insert(u"type"_q, u"control_response"_q);
	result.insert(u"response"_q, inner);
	return Compact(result);
}

QByteArray UserLine(const QJsonArray &content, bool query) {
	auto message = QJsonObject();
	message.insert(u"role"_q, u"user"_q);
	message.insert(u"content"_q, content);
	auto result = QJsonObject();
	result.insert(u"type"_q, u"user"_q);
	result.insert(u"message"_q, message);
	result.insert(u"parent_tool_use_id"_q, QJsonValue::Null);
	result.insert(u"session_id"_q, QString());
	if (!query) {
		result.insert(u"shouldQuery"_q, false);
	}
	return Compact(result);
}

QByteArray AssistantLine(const QJsonArray &content) {
	auto message = QJsonObject();
	message.insert(u"role"_q, u"assistant"_q);
	message.insert(u"content"_q, content);
	auto result = QJsonObject();
	result.insert(u"type"_q, u"assistant"_q);
	result.insert(u"message"_q, message);
	result.insert(u"parent_tool_use_id"_q, QJsonValue::Null);
	result.insert(u"session_id"_q, QString());
	return Compact(result);
}

std::vector<Frame> ReplayFrames(const std::vector<Message> &messages) {
	auto result = std::vector<Frame>();
	auto ids = IdMapper();
	for (auto i = 0, count = int(messages.size()); i != count; ++i) {
		const auto &message = messages[i];
		const auto content = (message.role == Role::User)
			? UserParts(message, &ids)
			: AssistantParts(message, i, ids);
		if (content.isEmpty()) {
			continue;
		} else if (!result.empty() && result.back().role == message.role) {
			for (const auto &part : content) {
				result.back().content.append(part);
			}
		} else {
			result.push_back({ message.role, content });
		}
	}
	return result;
}

QJsonArray UserContent(const Message &message) {
	return UserParts(message, nullptr);
}

QJsonObject McpResult(const QJsonValue &id, const QJsonObject &result) {
	auto object = QJsonObject();
	object.insert(u"jsonrpc"_q, u"2.0"_q);
	object.insert(u"id"_q, id);
	object.insert(u"result"_q, result);
	return object;
}

QJsonObject McpError(
		const QJsonValue &id,
		int code,
		const QString &message) {
	auto error = QJsonObject();
	error.insert(u"code"_q, code);
	error.insert(u"message"_q, message);
	auto object = QJsonObject();
	object.insert(u"jsonrpc"_q, u"2.0"_q);
	object.insert(u"id"_q, id);
	object.insert(u"error"_q, error);
	return object;
}

QJsonObject McpInitializeResult(const QJsonObject &params) {
	auto version = params.value(u"protocolVersion"_q).toString();
	if (version.isEmpty()) {
		version = u"2025-06-18"_q;
	}
	auto tools = QJsonObject();
	tools.insert(u"listChanged"_q, true);
	auto capabilities = QJsonObject();
	capabilities.insert(u"tools"_q, tools);
	auto info = QJsonObject();
	info.insert(u"name"_q, ServerName());
	info.insert(u"version"_q, u"1.0"_q);
	auto result = QJsonObject();
	result.insert(u"protocolVersion"_q, version);
	result.insert(u"capabilities"_q, capabilities);
	result.insert(u"serverInfo"_q, info);
	return result;
}

QJsonObject McpToolsList(const std::vector<ToolSpec> &tools) {
	auto meta = QJsonObject();
	meta.insert(u"anthropic/maxResultSizeChars"_q, kMaxResultChars);
	auto list = QJsonArray();
	for (const auto &tool : tools) {
		auto object = QJsonObject();
		object.insert(u"name"_q, tool.name);
		object.insert(u"description"_q, tool.description);
		object.insert(u"inputSchema"_q, tool.inputSchema);
		object.insert(u"_meta"_q, meta);
		list.append(object);
	}
	auto result = QJsonObject();
	result.insert(u"tools"_q, list);
	return result;
}

QJsonObject McpToolResult(const ToolResultBlock &result) {
	auto content = QJsonArray();
	auto omitted = 0;
	for (const auto &media : result.media) {
		if (media.bytes.isEmpty()) {
			continue;
		} else if (media.kind != MediaKind::Image) {
			++omitted;
			continue;
		}
		auto image = QJsonObject();
		image.insert(u"type"_q, u"image"_q);
		image.insert(
			u"data"_q,
			QString::fromLatin1(media.bytes.toBase64()));
		image.insert(u"mimeType"_q, media.mimeType);
		content.append(image);
	}
	auto text = result.content;
	if (omitted) {
		text += u"\n[%1 non-image media file(s) can't be shown here.]"_q.arg(
			omitted);
	}
	if (!text.isEmpty() || content.isEmpty()) {
		content.prepend(TextPart(text));
	}
	auto object = QJsonObject();
	object.insert(u"content"_q, content);
	if (result.error) {
		object.insert(u"isError"_q, true);
	}
	return object;
}

namespace {

[[nodiscard]] QString JoinedText(const QJsonValue &content) {
	if (content.isString()) {
		return content.toString();
	}
	auto result = QString();
	for (const auto &part : content.toArray()) {
		const auto object = part.toObject();
		if (object.value(u"type"_q).toString() == u"text"_q) {
			if (!result.isEmpty()) {
				result += '\n';
			}
			result += object.value(u"text"_q).toString();
		}
	}
	return result;
}

[[nodiscard]] bool Nested(const QJsonObject &root) {
	const auto parent = root.value(u"parent_tool_use_id"_q);
	return parent.isString() && !parent.toString().isEmpty();
}

[[nodiscard]] LimitWindow ParseWindow(const QJsonObject &window) {
	auto result = LimitWindow();
	if (const auto value = window.value(u"utilization"_q); value.isDouble()) {
		result.utilization = value.toDouble();
	}
	result.resetsAt = qint64(window.value(u"resetsAt"_q).toDouble());
	return result;
}

[[nodiscard]] Limits ParseLimits(const QJsonObject &root) {
	const auto info = root.value(u"rate_limit_info"_q).toObject();
	auto result = Limits();
	result.status = info.value(u"status"_q).toString();
	const auto windows = info.value(u"unifiedWindows"_q).toObject();
	result.fiveHour = ParseWindow(
		windows.value(u"five_hour"_q).toObject());
	result.sevenDay = ParseWindow(
		windows.value(u"seven_day"_q).toObject());
	if (result.fiveHour.resetsAt == 0
		&& info.value(u"rateLimitType"_q).toString() == u"five_hour"_q) {
		result.fiveHour.resetsAt = qint64(
			info.value(u"resetsAt"_q).toDouble());
	}
	return result;
}

[[nodiscard]] QStringList Strings(const QJsonValue &value) {
	auto result = QStringList();
	for (const auto &entry : value.toArray()) {
		if (entry.isString()) {
			result.push_back(entry.toString());
		}
	}
	return result;
}

} // namespace

Line ParseLine(const QByteArray &line) {
	const auto document = QJsonDocument::fromJson(line);
	if (!document.isObject()) {
		return std::monostate();
	}
	const auto root = document.object();
	const auto type = root.value(u"type"_q).toString();
	if (type == u"control_request"_q) {
		const auto request = root.value(u"request"_q).toObject();
		return ControlRequest{
			root.value(u"request_id"_q).toString(),
			request.value(u"subtype"_q).toString(),
			request,
		};
	} else if (type == u"control_cancel_request"_q) {
		return ControlRequest{
			root.value(u"request_id"_q).toString(),
			u"control_cancel_request"_q,
			QJsonObject(),
		};
	} else if (type == u"control_response"_q) {
		const auto response = root.value(u"response"_q).toObject();
		return ControlResponse{
			response.value(u"request_id"_q).toString(),
			(response.value(u"subtype"_q).toString() == u"success"_q),
			response.value(u"response"_q).toObject(),
			response.value(u"error"_q).toString(),
		};
	} else if (type == u"stream_event"_q) {
		return StreamEvent{
			root.value(u"event"_q).toObject(),
			Nested(root),
		};
	} else if (type == u"assistant"_q) {
		const auto message = root.value(u"message"_q).toObject();
		return AssistantEcho{
			root.value(u"error"_q).toString(),
			JoinedText(message.value(u"content"_q)),
			Nested(root),
		};
	} else if (type == u"user"_q) {
		auto echo = UserEcho();
		echo.nested = Nested(root);
		echo.replay = root.value(u"isReplay"_q).toBool();
		const auto content = root.value(u"message"_q).toObject()
			.value(u"content"_q).toArray();
		for (const auto &part : content) {
			const auto object = part.toObject();
			if (object.value(u"type"_q).toString() != u"tool_result"_q) {
				continue;
			}
			echo.results.push_back(ToolResultBlock{
				object.value(u"tool_use_id"_q).toString(),
				JoinedText(object.value(u"content"_q)),
				object.value(u"is_error"_q).toBool(),
			});
		}
		return echo;
	} else if (type == u"result"_q) {
		auto result = ResultLine();
		result.subtype = root.value(u"subtype"_q).toString();
		result.isError = root.value(u"is_error"_q).toBool();
		result.numTurns = root.value(u"num_turns"_q).toInt();
		result.result = root.value(u"result"_q).toString();
		result.terminalReason = root.value(u"terminal_reason"_q).toString();
		result.stopReason = root.value(u"stop_reason"_q).toString();
		if (const auto status = root.value(u"api_error_status"_q)
			; status.isDouble()) {
			result.apiErrorStatus = status.toInt();
		}
		result.errors = Strings(root.value(u"errors"_q));
		return result;
	} else if (type == u"rate_limit_event"_q) {
		return ParseLimits(root);
	} else if (type == u"system"_q
		&& root.value(u"subtype"_q).toString() == u"init"_q) {
		return SystemInit{
			root.value(u"model"_q).toString(),
			root.value(u"claude_code_version"_q).toString(),
			Strings(root.value(u"tools"_q)),
			Strings(root.value(u"capabilities"_q)),
		};
	}
	return std::monostate();
}

std::vector<ModelOption> ParseModels(const QJsonObject &initializeResponse) {
	auto result = std::vector<ModelOption>();
	const auto models = initializeResponse.value(u"models"_q).toArray();
	for (const auto &entry : models) {
		const auto object = entry.toObject();
		auto option = ModelOption();
		option.value = object.value(u"value"_q).toString();
		if (option.value.isEmpty()) {
			continue;
		}
		option.resolved = object.value(u"resolvedModel"_q).toString();
		option.name = object.value(u"displayName"_q).toString();
		option.description = object.value(u"description"_q).toString();
		for (const auto &level : object.value(
				u"supportedEffortLevels"_q).toArray()) {
			if (level.isString()) {
				option.effortLevels.push_back(level.toString());
			}
		}
		result.push_back(std::move(option));
	}
	return result;
}

std::optional<AuthStatus> ParseAuthStatus(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json.trimmed());
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto root = document.object();
	if (!root.value(u"loggedIn"_q).isBool()) {
		return std::nullopt;
	}
	return AuthStatus{
		root.value(u"loggedIn"_q).toBool(),
		root.value(u"authMethod"_q).toString(),
		root.value(u"email"_q).toString(),
		root.value(u"subscriptionType"_q).toString(),
	};
}

std::optional<std::array<int, 3>> ParseVersion(const QByteArray &output) {
	static const auto re = QRegularExpression(
		u"(\\d+)\\.(\\d+)\\.(\\d+)"_q);
	const auto match = re.match(QString::fromUtf8(output));
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	return std::array<int, 3>{
		match.captured(1).toInt(),
		match.captured(2).toInt(),
		match.captured(3).toInt(),
	};
}

bool VersionSupported(const std::array<int, 3> &version) {
	return (version >= kMinVersion);
}

Error ResultError(const ResultLine &result, const QString &assistantError) {
	auto error = Error();
	error.text = !result.result.isEmpty()
		? result.result
		: result.errors.join(u"; "_q);
	const auto &kind = assistantError;
	if (kind == u"authentication_failed"_q
		|| kind == u"oauth_org_not_allowed"_q
		|| kind == u"account_on_hold"_q
		|| kind == u"verification_required"_q
		|| kind == u"billing_error"_q
		|| kind == u"cloud_credential_error"_q) {
		error.kind = ErrorKind::Auth;
	} else if (kind == u"rate_limit"_q) {
		error.kind = ErrorKind::RateLimited;
	} else if (kind == u"overloaded"_q || kind == u"server_error"_q) {
		error.kind = ErrorKind::Overloaded;
	} else if (kind == u"invalid_request"_q
		|| kind == u"model_not_found"_q) {
		error.kind = ErrorKind::BadRequest;
	} else if (const auto status = result.apiErrorStatus.value_or(0)) {
		error.httpStatus = status;
		error.kind = (status == 401 || status == 403)
			? ErrorKind::Auth
			: (status == 429)
			? ErrorKind::RateLimited
			: (status == 408 || status == 529 || status / 100 == 5)
			? ErrorKind::Overloaded
			: (status / 100 == 4)
			? ErrorKind::BadRequest
			: ErrorKind::BadResponse;
	} else {
		error.kind = ErrorKind::BadResponse;
	}
	if (error.text.isEmpty()) {
		error.text = assistantError.isEmpty()
			? u"Claude Code ended the turn with an error."_q
			: assistantError;
	}
	return error;
}

} // namespace Agent::Claude
