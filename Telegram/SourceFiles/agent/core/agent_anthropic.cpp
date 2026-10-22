/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_anthropic.h"

#include "agent/core/agent_anthropic_stream.h"
#include "agent/core/agent_sse.h"
#include "base/flat_map.h"
#include "base/flat_set.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

#include <optional>
#include <utility>
#include <vector>

namespace Agent::Anthropic {
namespace {

constexpr auto kErrorBodyLimit = 500;
constexpr auto kDefaultMaxTokens = 32000;

[[nodiscard]] bool IsSuccessStatus(int status) {
	return status / 100 == 2;
}

[[nodiscard]] ErrorKind KindForStatus(int status) {
	switch (status) {
	case 401:
	case 403: return ErrorKind::Auth;
	case 429: return ErrorKind::RateLimited;
	case 408:
	case 500:
	case 502:
	case 503:
	case 504:
	case 529: return ErrorKind::Overloaded;
	}
	if (status / 100 == 4) {
		return ErrorKind::BadRequest;
	}
	return ErrorKind::BadResponse;
}

[[nodiscard]] crl::time RetryAfterMs(const QByteArray &headerValue) {
	auto ok = false;
	const auto seconds = headerValue.toLongLong(&ok);
	return ok ? crl::time(seconds * 1000) : crl::time(0);
}

[[nodiscard]] QString SlashJoinedUrl(
		const QString &baseUrl,
		QString &&path) {
	auto result = baseUrl;
	if (!result.endsWith('/')) {
		result += '/';
	}
	result += std::move(path);
	return result;
}

[[nodiscard]] std::optional<QJsonObject> SerializeMedia(
		const MediaBlock &media) {
	if (media.bytes.isEmpty()) {
		return std::nullopt;
	}
	auto type = QString();
	auto source = QJsonObject();
	source.insert(u"type"_q, u"base64"_q);
	if (media.kind == MediaKind::Image) {
		type = u"image"_q;
		source.insert(u"media_type"_q, media.mimeType);
	} else if (media.kind == MediaKind::File
		&& media.mimeType == u"application/pdf"_q) {
		type = u"document"_q;
		source.insert(u"media_type"_q, u"application/pdf"_q);
	} else {
		return std::nullopt;
	}
	source.insert(u"data"_q, QString::fromLatin1(media.bytes.toBase64()));
	auto block = QJsonObject();
	block.insert(u"type"_q, type);
	block.insert(u"source"_q, source);
	return block;
}

[[nodiscard]] QJsonObject SerializeText(const TextBlock &block) {
	auto object = QJsonObject();
	object.insert(u"type"_q, u"text"_q);
	object.insert(u"text"_q, block.text);
	return object;
}

[[nodiscard]] std::optional<QJsonObject> SerializeThinking(
		const ThinkingBlock &block) {
	if (block.signature.isEmpty()) {
		return std::nullopt;
	}
	auto object = QJsonObject();
	if (block.redacted) {
		object.insert(u"type"_q, u"redacted_thinking"_q);
		object.insert(u"data"_q, QString::fromUtf8(block.signature));
	} else {
		object.insert(u"type"_q, u"thinking"_q);
		object.insert(u"thinking"_q, block.text);
		object.insert(u"signature"_q, QString::fromUtf8(block.signature));
	}
	return object;
}

[[nodiscard]] QJsonObject SerializeToolUse(
		const ToolUseBlock &use,
		base::flat_map<QString, QString> &remap,
		base::flat_set<QString> &seen,
		int messageIndex,
		int blockIndex) {
	static const auto valid = QRegularExpression(
		u"^[A-Za-z0-9_-]+$"_q);
	auto id = use.id;
	if (id.isEmpty()
		|| !valid.match(id).hasMatch()
		|| seen.contains(id)) {
		id = u"toolu_aigram_%1_%2"_q.arg(messageIndex).arg(blockIndex);
	}
	seen.insert(id);
	remap.insert_or_assign(use.id, id);
	auto object = QJsonObject();
	object.insert(u"type"_q, u"tool_use"_q);
	object.insert(u"id"_q, id);
	object.insert(u"name"_q, use.name);
	object.insert(u"input"_q, use.inputError.isEmpty()
		? QJsonValue(use.input)
		: QJsonValue(QJsonObject()));
	return object;
}

[[nodiscard]] QJsonObject SerializeToolResult(
		const ToolResultBlock &result,
		const base::flat_map<QString, QString> &remap) {
	auto object = QJsonObject();
	object.insert(u"type"_q, u"tool_result"_q);
	const auto found = remap.find(result.toolUseId);
	object.insert(
		u"tool_use_id"_q,
		(found == remap.end()) ? result.toolUseId : found->second);
	if (result.error) {
		object.insert(u"is_error"_q, true);
	}
	auto content = QJsonArray();
	if (!result.content.isEmpty()) {
		content.append(SerializeText(TextBlock{ result.content }));
	}
	for (const auto &media : result.media) {
		if (auto block = SerializeMedia(media)) {
			content.append(*block);
		}
	}
	if (!content.isEmpty()) {
		object.insert(u"content"_q, content);
	}
	return object;
}

struct OutBlockRef {
	const Block *block = nullptr;
	int messageIndex = 0;
	int blockIndex = 0;
};

struct OutMessage {
	Role role = Role::User;
	std::vector<OutBlockRef> blocks;
};

[[nodiscard]] std::vector<OutMessage> MergeMessages(const Request &request) {
	auto result = std::vector<OutMessage>();
	for (auto m = 0; m < int(request.messages.size()); ++m) {
		const auto &message = request.messages[m];
		if (result.empty() || result.back().role != message.role) {
			result.push_back(OutMessage{ message.role, {} });
		}
		auto &out = result.back();
		for (auto b = 0; b < int(message.blocks.size()); ++b) {
			out.blocks.push_back({ &message.blocks[b], m, b });
		}
	}
	return result;
}

[[nodiscard]] QJsonArray SerializeMessages(const Request &request) {
	const auto merged = MergeMessages(request);
	auto remap = base::flat_map<QString, QString>();
	auto seen = base::flat_set<QString>();
	auto result = QJsonArray();
	for (const auto &out : merged) {
		auto content = QJsonArray();
		for (const auto &entry : out.blocks) {
			if (const auto *resultBlock
				= std::get_if<ToolResultBlock>(entry.block)) {
				content.append(SerializeToolResult(*resultBlock, remap));
			}
		}
		for (const auto &entry : out.blocks) {
			const auto *block = entry.block;
			if (std::get_if<ToolResultBlock>(block)) {
				continue;
			} else if (const auto *text = std::get_if<TextBlock>(block)) {
				if (!text->text.isEmpty()) {
					content.append(SerializeText(*text));
				}
			} else if (const auto *media = std::get_if<MediaBlock>(block)) {
				if (auto mapped = SerializeMedia(*media)) {
					content.append(*mapped);
				}
			} else if (const auto *thinking
				= std::get_if<ThinkingBlock>(block)) {
				if (thinking->signature.startsWith('{')) {
					continue;
				}
				if (auto mapped = SerializeThinking(*thinking)) {
					content.append(*mapped);
				}
			} else if (const auto *use = std::get_if<ToolUseBlock>(block)) {
				content.append(SerializeToolUse(
					*use,
					remap,
					seen,
					entry.messageIndex,
					entry.blockIndex));
			}
		}
		if (!content.isEmpty()) {
			auto message = QJsonObject();
			message.insert(
				u"role"_q,
				(out.role == Role::User) ? u"user"_q : u"assistant"_q);
			message.insert(u"content"_q, content);
			result.append(message);
		}
	}
	return result;
}

} // namespace

QJsonObject RequestObject(const Request &request) {
	auto root = QJsonObject();
	root.insert(u"model"_q, request.model);
	root.insert(u"stream"_q, true);
	root.insert(
		u"max_tokens"_q,
		(request.maxOutputTokens > 0)
			? request.maxOutputTokens
			: kDefaultMaxTokens);
	auto cacheControl = QJsonObject();
	cacheControl.insert(u"type"_q, u"ephemeral"_q);
	root.insert(u"cache_control"_q, cacheControl);
	if (!request.system.isEmpty()) {
		auto block = QJsonObject();
		block.insert(u"type"_q, u"text"_q);
		block.insert(u"text"_q, request.system);
		block.insert(u"cache_control"_q, cacheControl);
		auto system = QJsonArray();
		system.append(block);
		root.insert(u"system"_q, system);
	}
	if (!request.tools.empty()) {
		auto tools = QJsonArray();
		for (const auto &tool : request.tools) {
			auto object = QJsonObject();
			object.insert(u"name"_q, tool.name);
			object.insert(u"description"_q, tool.description);
			object.insert(u"input_schema"_q, tool.inputSchema);
			tools.append(object);
		}
		root.insert(u"tools"_q, tools);
	}
	if (SupportsAdaptiveThinking(request.model)) {
		auto binding = QJsonObject();
		binding.insert(u"prefix_mismatch_behavior"_q, u"drop_block"_q);
		auto thinking = QJsonObject();
		thinking.insert(u"type"_q, u"adaptive"_q);
		thinking.insert(u"display"_q, u"summarized"_q);
		thinking.insert(u"block_binding"_q, binding);
		root.insert(u"thinking"_q, thinking);
	}
	if (!request.effort.isEmpty()) {
		auto outputConfig = QJsonObject();
		outputConfig.insert(u"effort"_q, request.effort);
		root.insert(u"output_config"_q, outputConfig);
	}
	if (SupportsFallbacks(request.model)) {
		root.insert(u"fallbacks"_q, u"default"_q);
	}
	root.insert(u"messages"_q, SerializeMessages(request));
	return root;
}

namespace {

class Parser final : public Agent::ResponseParser {
public:
	void head(const HttpHead &head) override;
	void feed(const QByteArray &chunk) override;
	[[nodiscard]] std::vector<StreamDelta> takeDeltas() override;
	[[nodiscard]] Response finish(const QString &networkError) override;

private:
	void collect(const std::vector<SseEvent> &events);

	SseParser _sse;
	StreamAssembler _assembler;
	HttpHead _head;
	QByteArray _errorBody;

};

void Parser::head(const HttpHead &head) {
	_head = head;
}

void Parser::feed(const QByteArray &chunk) {
	if (!IsSuccessStatus(_head.status)) {
		_errorBody += chunk;
		return;
	}
	collect(_sse.feed(chunk));
}

void Parser::collect(const std::vector<SseEvent> &events) {
	for (const auto &event : events) {
		if (event.data.isEmpty()) {
			continue;
		}
		const auto document = QJsonDocument::fromJson(event.data);
		if (document.isObject()) {
			_assembler.event(document.object());
		}
	}
}

std::vector<StreamDelta> Parser::takeDeltas() {
	return _assembler.takeDeltas();
}

Response Parser::finish(const QString &networkError) {
	auto response = Response();
	if (!networkError.isEmpty()) {
		auto error = Error();
		error.kind = ErrorKind::Network;
		error.text = networkError;
		response.error = std::move(error);
		return response;
	} else if (!IsSuccessStatus(_head.status)) {
		response.error = ParseError(_head, _errorBody);
		return response;
	}
	collect(_sse.finish());
	return _assembler.finish();
}

} // namespace

Adapter::Adapter(ProviderSettings settings)
: _settings(std::move(settings)) {
}

HttpRequest Adapter::prepare(const Request &request) const {
	auto adjusted = request;
	if (adjusted.model.isEmpty()) {
		adjusted.model = _settings.model;
	}
	if (adjusted.effort.isEmpty()) {
		adjusted.effort = _settings.effort;
	}
	auto betas = std::vector<QByteArray>();
	if (SupportsAdaptiveThinking(adjusted.model)) {
		betas.push_back("thinking-binding-controls-2026-08-01");
	}
	if (SupportsFallbacks(adjusted.model)) {
		betas.push_back("server-side-fallback-2026-07-01");
	}
	auto beta = QByteArray();
	for (const auto &name : betas) {
		if (!beta.isEmpty()) {
			beta += ',';
		}
		beta += name;
	}
	auto result = HttpRequest();
	result.url = SlashJoinedUrl(_settings.baseUrl, u"messages"_q);
	result.body = QJsonDocument(RequestObject(adjusted)).toJson(
		QJsonDocument::Compact);
	result.post = true;
	result.headers.push_back({ "x-api-key", _settings.apiKey.toUtf8() });
	result.headers.push_back({ "anthropic-version", "2023-06-01" });
	result.headers.push_back({ "content-type", "application/json" });
	result.headers.push_back({ "accept", "text/event-stream" });
	if (!beta.isEmpty()) {
		result.headers.push_back({ "anthropic-beta", beta });
	}
	return result;
}

std::unique_ptr<ResponseParser> Adapter::parser() const {
	return std::make_unique<Parser>();
}

std::vector<QString> EffortLevels() {
	return {
		u"low"_q,
		u"medium"_q,
		u"high"_q,
		u"xhigh"_q,
		u"max"_q,
	};
}

bool SupportsAdaptiveThinking(const QString &model) {
	if (model.startsWith(u"claude-3"_q)
		|| model.startsWith(u"claude-opus-4-2025"_q)
		|| model.startsWith(u"claude-sonnet-4-2025"_q)) {
		return false;
	}
	static const auto kNoAdaptive = std::vector<QString>{
		u"claude-haiku-4-5"_q,
		u"claude-sonnet-4-5"_q,
		u"claude-opus-4-5"_q,
		u"claude-opus-4-1"_q,
		u"claude-opus-4-0"_q,
		u"claude-sonnet-4-0"_q,
	};
	for (const auto &id : kNoAdaptive) {
		if (model.startsWith(id)) {
			return false;
		}
	}
	return true;
}

bool SupportsFallbacks(const QString &model) {
	static const auto kFallbacks = std::vector<QString>{
		u"claude-fable-5-1"_q,
		u"claude-opus-5-5"_q,
		u"claude-opus-5"_q,
		u"claude-sonnet-5-5"_q,
	};
	for (const auto &id : kFallbacks) {
		if (model.startsWith(id)) {
			return true;
		}
	}
	return false;
}

std::optional<ModelPrices> KnownPrices(const QString &model) {
	struct Entry {
		QString id;
		double input;
		double output;
		double cached;
	};
	static const auto kEntries = std::vector<Entry>{
		{ u"claude-fable-5-1"_q, 10., 50., 0.25 },
		{ u"claude-fable-5"_q, 10., 50., 1. },
		{ u"claude-opus-5-5"_q, 4., 20., 0.2 },
		{ u"claude-opus-5"_q, 5., 25., 0.5 },
		{ u"claude-opus-4-8"_q, 5., 25., 0.5 },
		{ u"claude-opus-4-7"_q, 5., 25., 0.5 },
		{ u"claude-opus-4-6"_q, 5., 25., 0.5 },
		{ u"claude-opus-4-5"_q, 5., 25., 0.5 },
		{ u"claude-sonnet-5-5"_q, 2., 10., 0.2 },
		{ u"claude-sonnet-5"_q, 2., 10., 0.2 },
		{ u"claude-sonnet-4-6"_q, 3., 15., 0.3 },
		{ u"claude-sonnet-4-5"_q, 3., 15., 0.3 },
		{ u"claude-haiku-5-5"_q, 0.1, 0.5, 0.01 },
		{ u"claude-haiku-4-5"_q, 1., 5., 0.1 },
	};
	const Entry *best = nullptr;
	for (const auto &entry : kEntries) {
		const auto matches = (model == entry.id)
			|| (model.size() > entry.id.size()
				&& model.startsWith(entry.id)
				&& model[entry.id.size()] == '-');
		if (matches && (!best || entry.id.size() > best->id.size())) {
			best = &entry;
		}
	}
	if (!best) {
		return std::nullopt;
	}
	auto result = ModelPrices();
	result.input = best->input;
	result.output = best->output;
	result.cached = best->cached;
	result.cacheWrite = 1.25 * best->input;
	return result;
}

HttpRequest ModelsRequest(const ProviderSettings &settings) {
	auto result = HttpRequest();
	result.url = SlashJoinedUrl(settings.baseUrl, u"models?limit=1000"_q);
	result.post = false;
	result.headers.push_back({ "x-api-key", settings.apiKey.toUtf8() });
	result.headers.push_back({ "anthropic-version", "2023-06-01" });
	return result;
}

std::vector<ModelInfo> ParseModels(const QByteArray &json) {
	auto result = std::vector<ModelInfo>();
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return result;
	}
	const auto models = document.object().value(u"data"_q).toArray();
	for (const auto &model : models) {
		const auto object = model.toObject();
		auto info = ModelInfo();
		info.id = object.value(u"id"_q).toString();
		info.name = object.value(u"display_name"_q).toString();
		info.contextWindow = object.value(u"max_input_tokens"_q).toInt();
		info.maxOutput = object.value(u"max_tokens"_q).toInt();
		const auto capabilities = object.value(u"capabilities"_q).toObject();
		info.vision = capabilities.value(u"image_input"_q).toObject()
			.value(u"supported"_q).toBool();
		info.pdf = capabilities.value(u"pdf_input"_q).toObject()
			.value(u"supported"_q).toBool();
		const auto effort = capabilities.value(u"effort"_q).toObject();
		for (const auto &level : EffortLevels()) {
			if (effort.value(level).toObject()
					.value(u"supported"_q).toBool()) {
				info.effortLevels.push_back(level);
			}
		}
		info.prices = KnownPrices(info.id);
		result.push_back(std::move(info));
	}
	return result;
}

Error ParseError(const HttpHead &head, const QByteArray &body) {
	auto result = Error();
	result.kind = KindForStatus(head.status);
	result.httpStatus = head.status;
	result.retryAfter = RetryAfterMs(head.retryAfter);
	auto text = QString();
	const auto document = QJsonDocument::fromJson(body);
	if (document.isObject()) {
		text = document.object()
			.value(u"error"_q).toObject()
			.value(u"message"_q).toString();
	}
	if (text.isEmpty()) {
		text = QString::fromUtf8(body.mid(0, kErrorBodyLimit)).trimmed();
	}
	result.text = text;
	return result;
}

} // namespace Agent::Anthropic
