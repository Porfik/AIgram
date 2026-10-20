/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_openai.h"

#include "agent/core/agent_sse.h"
#include "base/algorithm.h"
#include "base/flat_map.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Agent::OpenAi {
namespace {

constexpr auto kErrorBodyLimit = 500;

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

[[nodiscard]] QString JoinTextBlocks(const std::vector<Block> &blocks) {
	auto result = QString();
	for (const auto &block : blocks) {
		if (const auto *text = std::get_if<TextBlock>(&block)) {
			if (!result.isEmpty()) {
				result += u"\n\n"_q;
			}
			result += text->text;
		}
	}
	return result;
}

[[nodiscard]] QString MediaDataUrl(const MediaBlock &media) {
	return u"data:%1;base64,%2"_q.arg(
		media.mimeType,
		QString::fromLatin1(media.bytes.toBase64()));
}

[[nodiscard]] QString AudioFormat(const QString &mimeType) {
	if (mimeType == u"audio/wav"_q) {
		return u"wav"_q;
	} else if (mimeType == u"audio/mpeg"_q) {
		return u"mp3"_q;
	}
	const auto slash = mimeType.indexOf('/');
	return (slash >= 0) ? mimeType.mid(slash + 1) : mimeType;
}

[[nodiscard]] std::optional<QJsonObject> SerializeMediaPart(
		const MediaBlock &media) {
	if (media.bytes.isEmpty()) {
		return std::nullopt;
	}
	auto part = QJsonObject();
	switch (media.kind) {
	case MediaKind::Image: {
		auto imageObject = QJsonObject();
		imageObject.insert(u"url"_q, MediaDataUrl(media));
		part.insert(u"type"_q, u"image_url"_q);
		part.insert(u"image_url"_q, imageObject);
		break;
	}
	case MediaKind::Audio: {
		auto audioObject = QJsonObject();
		audioObject.insert(
			u"data"_q,
			QString::fromLatin1(media.bytes.toBase64()));
		audioObject.insert(u"format"_q, AudioFormat(media.mimeType));
		part.insert(u"type"_q, u"input_audio"_q);
		part.insert(u"input_audio"_q, audioObject);
		break;
	}
	case MediaKind::Video: {
		auto videoObject = QJsonObject();
		videoObject.insert(u"url"_q, MediaDataUrl(media));
		part.insert(u"type"_q, u"video_url"_q);
		part.insert(u"video_url"_q, videoObject);
		break;
	}
	case MediaKind::File: {
		auto fileObject = QJsonObject();
		fileObject.insert(u"filename"_q, media.name);
		fileObject.insert(u"file_data"_q, MediaDataUrl(media));
		part.insert(u"type"_q, u"file"_q);
		part.insert(u"file"_q, fileObject);
		break;
	}
	}
	return part;
}

[[nodiscard]] QJsonArray SerializeUserParts(
		const std::vector<Block> &blocks) {
	auto parts = QJsonArray();
	for (const auto &block : blocks) {
		if (const auto *text = std::get_if<TextBlock>(&block)) {
			auto part = QJsonObject();
			part.insert(u"type"_q, u"text"_q);
			part.insert(u"text"_q, text->text);
			parts.append(part);
		} else if (const auto *media = std::get_if<MediaBlock>(&block)) {
			if (auto part = SerializeMediaPart(*media)) {
				parts.append(*part);
			}
		}
	}
	return parts;
}

[[nodiscard]] QJsonValue SerializeUserContent(
		const std::vector<Block> &blocks) {
	auto hasMedia = false;
	for (const auto &block : blocks) {
		if (const auto *media = std::get_if<MediaBlock>(&block)) {
			if (!media->bytes.isEmpty()) {
				hasMedia = true;
				break;
			}
		}
	}
	if (!hasMedia) {
		return JoinTextBlocks(blocks);
	}
	return SerializeUserParts(blocks);
}

[[nodiscard]] QString ArgumentsJson(const ToolUseBlock &use) {
	return use.inputError.isEmpty()
		? QString::fromUtf8(
			QJsonDocument(use.input).toJson(QJsonDocument::Compact))
		: QString::fromUtf8(use.rawInput);
}

[[nodiscard]] QJsonArray SerializeMessages(const Request &request) {
	auto result = QJsonArray();
	for (const auto &message : request.messages) {
		auto other = std::vector<Block>();
		auto toolMedia = std::vector<std::pair<
			QString,
			std::vector<MediaBlock>>>();
		for (const auto &block : message.blocks) {
			if (const auto *resultBlock
				= std::get_if<ToolResultBlock>(&block)) {
				auto tool = QJsonObject();
				tool.insert(u"role"_q, u"tool"_q);
				tool.insert(u"tool_call_id"_q, resultBlock->toolUseId);
				tool.insert(u"content"_q, resultBlock->content);
				result.append(tool);
				if (!resultBlock->media.empty()) {
					toolMedia.emplace_back(
						resultBlock->toolUseId,
						resultBlock->media);
				}
			} else {
				other.push_back(block);
			}
		}
		if (message.role == Role::User) {
			auto content = QJsonValue();
			auto hasContent = false;
			if (!toolMedia.empty()) {
				auto parts = QJsonArray();
				for (const auto &entry : toolMedia) {
					auto entryParts = QJsonArray();
					for (const auto &media : entry.second) {
						if (auto part = SerializeMediaPart(media)) {
							entryParts.append(*part);
						}
					}
					if (!entryParts.isEmpty()) {
						auto header = QJsonObject();
						header.insert(u"type"_q, u"text"_q);
						header.insert(
							u"text"_q,
							u"Media returned by tool call %1:"_q.arg(
								entry.first));
						parts.append(header);
						for (const auto &part : entryParts) {
							parts.append(part);
						}
					}
				}
				auto otherParts = SerializeUserParts(other);
				for (const auto &part : otherParts) {
					parts.append(part);
				}
				if (!parts.isEmpty()) {
					content = parts;
					hasContent = true;
				}
			} else if (!other.empty()) {
				content = SerializeUserContent(other);
				hasContent = true;
			}
			if (hasContent) {
				auto user = QJsonObject();
				user.insert(u"role"_q, u"user"_q);
				user.insert(u"content"_q, content);
				result.append(user);
			}
		} else {
			auto assistant = QJsonObject();
			assistant.insert(u"role"_q, u"assistant"_q);
			const auto text = JoinTextBlocks(other);
			assistant.insert(
				u"content"_q,
				text.isEmpty()
					? QJsonValue(QJsonValue::Null)
					: QJsonValue(text));
			auto toolCalls = QJsonArray();
			for (const auto &block : other) {
				if (const auto *use = std::get_if<ToolUseBlock>(&block)) {
					auto call = QJsonObject();
					call.insert(u"id"_q, use->id);
					call.insert(u"type"_q, u"function"_q);
					auto function = QJsonObject();
					function.insert(u"name"_q, use->name);
					function.insert(u"arguments"_q, ArgumentsJson(*use));
					call.insert(u"function"_q, function);
					toolCalls.append(call);
				}
			}
			if (!toolCalls.isEmpty()) {
				assistant.insert(u"tool_calls"_q, toolCalls);
			}
			result.append(assistant);
		}
	}
	return result;
}

[[nodiscard]] StopReason StopReasonFor(const QString &reason) {
	if (reason == u"tool_calls") {
		return StopReason::ToolUse;
	} else if (reason == u"length") {
		return StopReason::MaxTokens;
	} else if (reason == u"content_filter") {
		return StopReason::Refusal;
	}
	return StopReason::EndTurn;
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

class Parser final : public Agent::ResponseParser {
public:
	void head(const HttpHead &head) override;

	void feed(const QByteArray &chunk) override;

	[[nodiscard]] std::vector<StreamDelta> takeDeltas() override;

	[[nodiscard]] Response finish(const QString &networkError) override;

private:
	struct ToolCall {
		QString id;
		QString name;
		QByteArray arguments;
	};

	template <typename T>
	void addTextDelta(T delta);

	void collect(const std::vector<SseEvent> &events);
	void processData(const QByteArray &data);
	void processToolCallFragment(const QJsonObject &fragment);

	SseParser _sse;
	HttpHead _head;
	QByteArray _errorBody;
	std::vector<StreamDelta> _deltas;
	QString _text;
	QString _thinking;
	base::flat_map<int, ToolCall> _calls;
	std::optional<Error> _streamError;
	Usage _usage;
	bool _sawFinishReason = false;
	bool _sawDone = false;
	StopReason _finishReason = StopReason::EndTurn;

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

std::vector<StreamDelta> Parser::takeDeltas() {
	return base::take(_deltas);
}

template <typename T>
void Parser::addTextDelta(T delta) {
	if (delta.text.isEmpty()) {
		return;
	}
	if constexpr (std::is_same_v<T, TextDelta>) {
		_text += delta.text;
	} else {
		_thinking += delta.text;
	}
	if (!_deltas.empty()) {
		if (const auto last = std::get_if<T>(&_deltas.back())) {
			last->text += delta.text;
			return;
		}
	}
	_deltas.push_back(std::move(delta));
}

void Parser::collect(const std::vector<SseEvent> &events) {
	for (const auto &event : events) {
		if (event.data == "[DONE]") {
			_sawDone = true;
		} else if (!event.data.isEmpty()) {
			processData(event.data);
		}
	}
}

void Parser::processData(const QByteArray &data) {
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		return;
	}
	const auto root = document.object();
	if (const auto error = root.value(u"error"_q); error.isObject()) {
		const auto object = error.toObject();
		const auto code = object.value(u"code"_q);
		const auto status = code.isDouble()
			? int(code.toDouble())
			: _head.status;
		auto streamError = Error();
		streamError.kind = KindForStatus(status);
		streamError.httpStatus = status;
		streamError.text = object.value(u"message"_q).toString();
		streamError.retryAfter = RetryAfterMs(_head.retryAfter);
		if (!_streamError) {
			_streamError = streamError;
		}
		return;
	}
	const auto choices = root.value(u"choices"_q).toArray();
	if (!choices.isEmpty()) {
		const auto choice = choices.first().toObject();
		const auto delta = choice.value(u"delta"_q).toObject();
		if (const auto content = delta.value(u"content"_q)
			; content.isString()) {
			addTextDelta(TextDelta{ content.toString() });
		}
		if (const auto reasoning = delta.value(u"reasoning"_q)
			; reasoning.isString()) {
			addTextDelta(ThinkingDelta{ reasoning.toString() });
		} else if (const auto alt = delta.value(u"reasoning_content"_q)
			; alt.isString()) {
			addTextDelta(ThinkingDelta{ alt.toString() });
		}
		const auto calls = delta.value(u"tool_calls"_q).toArray();
		for (const auto &call : calls) {
			processToolCallFragment(call.toObject());
		}
		const auto finishReason = choice.value(u"finish_reason"_q).toString();
		if (!finishReason.isEmpty()) {
			_sawFinishReason = true;
			_finishReason = StopReasonFor(finishReason);
		}
	}
	if (const auto usage = root.value(u"usage"_q); usage.isObject()) {
		const auto object = usage.toObject();
		if (object.contains(u"prompt_tokens"_q)) {
			_usage.inputTokens = object.value(u"prompt_tokens"_q).toInt();
		}
		if (object.contains(u"completion_tokens"_q)) {
			_usage.outputTokens = object
				.value(u"completion_tokens"_q).toInt();
		}
		if (const auto prompt = object.value(u"prompt_tokens_details"_q)
			; prompt.isObject()) {
			const auto details = prompt.toObject();
			if (details.contains(u"cached_tokens"_q)) {
				_usage.cachedInputTokens = details
					.value(u"cached_tokens"_q).toInt();
			}
		}
		if (const auto cost = object.value(u"cost"_q); cost.isDouble()) {
			_usage.cost = cost.toDouble();
			_usage.hasCost = true;
		}
		if (object.value(u"is_byok"_q).toBool()) {
			const auto upstream = object.value(u"cost_details"_q).toObject()
				.value(u"upstream_inference_cost"_q);
			if (upstream.isDouble()) {
				_usage.cost += upstream.toDouble();
				_usage.hasCost = true;
			}
		}
	}
}

void Parser::processToolCallFragment(const QJsonObject &fragment) {
	const auto index = fragment.value(u"index"_q).toInt(-1);
	if (index < 0) {
		return;
	}
	const auto inserted = _calls.emplace(index, ToolCall()).second;
	const auto function = fragment.value(u"function"_q).toObject();
	auto &call = _calls[index];
	if (inserted) {
		auto id = fragment.value(u"id"_q).toString();
		if (id.isEmpty()) {
			id = u"call_%1"_q.arg(index);
		}
		call.id = id;
		call.name = function.value(u"name"_q).toString();
		_deltas.push_back(ToolUseStarted{ call.id, call.name });
	}
	if (const auto arguments = function.value(u"arguments"_q)
		; arguments.isString()) {
		call.arguments += arguments.toString().toUtf8();
	}
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
	if (_streamError) {
		response.error = *_streamError;
		return response;
	} else if (!_sawFinishReason && !_sawDone) {
		auto error = Error();
		error.kind = ErrorKind::BadResponse;
		error.httpStatus = _head.status;
		error.text = u"Stream ended without a finish reason"_q;
		response.error = std::move(error);
		return response;
	}
	auto blocks = std::vector<Block>();
	if (!_thinking.isEmpty()) {
		blocks.push_back(ThinkingBlock{ _thinking, QByteArray() });
	}
	if (!_text.isEmpty()) {
		blocks.push_back(TextBlock{ _text });
	}
	for (const auto &entry : _calls) {
		const auto &call = entry.second;
		auto use = ToolUseBlock();
		use.id = call.id;
		use.name = call.name;
		if (call.arguments.isEmpty()) {
			use.input = QJsonObject();
		} else {
			const auto document = QJsonDocument::fromJson(call.arguments);
			if (document.isObject()) {
				use.input = document.object();
			} else {
				use.inputError = u"Arguments are not a JSON object"_q;
				use.rawInput = call.arguments;
			}
		}
		blocks.push_back(std::move(use));
	}
	response.message = Message{ Role::Assistant, std::move(blocks) };
	response.stopReason = _calls.empty()
		? _finishReason
		: StopReason::ToolUse;
	response.usage = _usage;
	return response;
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
	const auto key = adjusted.cacheKey;
	const auto openRouter = !key.isEmpty() && IsOpenRouter(_settings);
	const auto openAi = !key.isEmpty()
		&& !openRouter
		&& (QUrl(_settings.baseUrl).host().toLower()
			== u"api.openai.com"_q);
	auto body = openAi
		? SerializeRequestWithCacheKey(adjusted)
		: SerializeRequest(adjusted);
	auto url = SlashJoinedUrl(_settings.baseUrl, u"chat/completions"_q);
	auto result = HttpRequest();
	result.url = std::move(url);
	result.body = std::move(body);
	result.post = true;
	result.headers.push_back({ "Content-Type", "application/json" });
	result.headers.push_back({ "Accept", "text/event-stream" });
	if (!_settings.apiKey.isEmpty()) {
		result.headers.push_back(
			{ "Authorization", "Bearer " + _settings.apiKey.toUtf8() });
	}
	if (openRouter) {
		result.headers.push_back({ "x-session-id", key.toUtf8() });
	}
	return result;
}

std::unique_ptr<ResponseParser> Adapter::parser() const {
	return std::make_unique<Parser>();
}

QByteArray SerializeRequest(const Request &request) {
	return QJsonDocument(RequestObject(request)).toJson(
		QJsonDocument::Compact);
}

QByteArray SerializeRequestWithCacheKey(const Request &request) {
	auto root = RequestObject(request);
	root.insert(u"prompt_cache_key"_q, request.cacheKey);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject RequestObject(const Request &request) {
	auto root = QJsonObject();
	root.insert(u"model"_q, request.model);
	root.insert(u"stream"_q, true);
	auto streamOptions = QJsonObject();
	streamOptions.insert(u"include_usage"_q, true);
	root.insert(u"stream_options"_q, streamOptions);
	if (request.maxOutputTokens > 0) {
		root.insert(u"max_tokens"_q, request.maxOutputTokens);
	}
	auto messages = QJsonArray();
	if (!request.system.isEmpty()) {
		auto system = QJsonObject();
		system.insert(u"role"_q, u"system"_q);
		system.insert(u"content"_q, request.system);
		messages.append(system);
	}
	for (const auto &message : SerializeMessages(request)) {
		messages.append(message);
	}
	root.insert(u"messages"_q, messages);
	if (!request.tools.empty()) {
		auto tools = QJsonArray();
		for (const auto &tool : request.tools) {
			auto toolObject = QJsonObject();
			toolObject.insert(u"type"_q, u"function"_q);
			auto function = QJsonObject();
			function.insert(u"name"_q, tool.name);
			function.insert(u"description"_q, tool.description);
			function.insert(u"parameters"_q, tool.inputSchema);
			toolObject.insert(u"function"_q, function);
			tools.append(toolObject);
		}
		root.insert(u"tools"_q, tools);
	}
	if (!request.pdfEngine.isEmpty()) {
		auto plugins = QJsonArray();
		auto plugin = QJsonObject();
		plugin.insert(u"id"_q, u"file-parser"_q);
		auto pdf = QJsonObject();
		pdf.insert(u"engine"_q, request.pdfEngine);
		plugin.insert(u"pdf"_q, pdf);
		plugins.append(plugin);
		root.insert(u"plugins"_q, plugins);
	}
	return root;
}

namespace {

[[nodiscard]] double ReadPrice(const QJsonValue &value) {
	auto ok = false;
	auto number = 0.;
	if (value.isString()) {
		number = value.toString().toDouble(&ok);
	} else if (value.isDouble()) {
		number = value.toDouble();
		ok = true;
	}
	return (ok && number > 0.) ? (number * 1'000'000.) : 0.;
}

[[nodiscard]] std::vector<QString> SplitWords(const QString &text) {
	auto result = std::vector<QString>();
	auto word = QString();
	for (const auto &character : text) {
		if (character.isLetterOrNumber()) {
			word += character;
		} else if (!word.isEmpty()) {
			result.push_back(word);
			word = QString();
		}
	}
	if (!word.isEmpty()) {
		result.push_back(word);
	}
	return result;
}

} // namespace

HttpRequest ModelsRequest(const ProviderSettings &settings) {
	auto result = HttpRequest();
	result.url = SlashJoinedUrl(settings.baseUrl, u"models"_q);
	result.post = false;
	if (!settings.apiKey.isEmpty()) {
		result.headers.push_back(
			{ "Authorization", "Bearer " + settings.apiKey.toUtf8() });
	}
	return result;
}

std::vector<ModelInfo> ParseModels(const QByteArray &json) {
	auto result = std::vector<ModelInfo>();
	const auto document = QJsonDocument::fromJson(json);
	const auto models = document.object().value(u"data"_q).toArray();
	for (const auto &model : models) {
		const auto object = model.toObject();
		auto info = ModelInfo();
		info.id = object.value(u"id"_q).toString();
		info.name = object.value(u"name"_q).toString();
		info.contextWindow = object.value(u"context_length"_q).toInt();
		const auto pricing = object.value(u"pricing"_q).toObject();
		info.priceInput = ReadPrice(pricing.value(u"prompt"_q));
		info.priceOutput = ReadPrice(pricing.value(u"completion"_q));
		info.priceCached = ReadPrice(pricing.value(u"input_cache_read"_q));
		if (const auto parameters = object.value(u"supported_parameters"_q)
			; parameters.isArray()) {
			auto has = false;
			for (const auto &parameter : parameters.toArray()) {
				if (parameter.toString() == u"tools") {
					has = true;
					break;
				}
			}
			info.tools = has;
		}
		const auto modalities = object.value(u"architecture"_q).toObject()
			.value(u"input_modalities"_q);
		if (modalities.isArray()) {
			auto hasImage = false;
			auto hasAudio = false;
			auto hasVideo = false;
			auto hasFile = false;
			for (const auto &modality : modalities.toArray()) {
				const auto value = modality.toString();
				if (value == u"image"_q) {
					hasImage = true;
				} else if (value == u"audio"_q) {
					hasAudio = true;
				} else if (value == u"video"_q) {
					hasVideo = true;
				} else if (value == u"file"_q) {
					hasFile = true;
				}
			}
			info.vision = hasImage;
			info.audio = hasAudio;
			info.video = hasVideo;
			info.files = hasFile;
		}
		result.push_back(std::move(info));
	}
	return result;
}

std::vector<int> SearchModels(
		const std::vector<ModelInfo> &models,
		const QString &query) {
	const auto words = SplitWords(query);
	if (words.empty()) {
		auto every = std::vector<int>(models.size(), 0);
		for (auto i = 1; i < int(models.size()); ++i) {
			every[i] = i;
		}
		return every;
	}
	auto result = std::vector<int>();
	for (auto i = 0, count = int(models.size()); i != count; ++i) {
		const auto &model = models[i];
		auto modelWords = SplitWords(model.name);
		for (const auto &word : SplitWords(model.id)) {
			modelWords.push_back(word);
		}
		const auto matches = [&](const QString &word) {
			for (const auto &candidate : modelWords) {
				if (candidate.startsWith(word, Qt::CaseInsensitive)) {
					return true;
				}
			}
			return false;
		};
		if (std::all_of(words.begin(), words.end(), matches)) {
			result.push_back(i);
		}
	}
	return result;
}

HttpRequest EndpointsRequest(const ProviderSettings &settings) {
	return HttpRequest();
}

std::vector<ModelRoute> ParseEndpoints(const QByteArray &json) {
	return {};
}

HttpRequest KeyRequest(const ProviderSettings &settings) {
	return HttpRequest();
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

} // namespace Agent::OpenAi
