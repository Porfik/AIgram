/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_openai_responses.h"

#include "agent/core/agent_sse.h"
#include "base/algorithm.h"
#include "base/flat_map.h"
#include "base/flat_set.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/stable_sort.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

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

[[nodiscard]] QString MediaDataUrl(const MediaBlock &media) {
	return u"data:%1;base64,%2"_q.arg(
		media.mimeType,
		QString::fromLatin1(media.bytes.toBase64()));
}

[[nodiscard]] std::optional<QJsonObject> SerializeInputMediaPart(
		const MediaBlock &media) {
	if (media.bytes.isEmpty()) {
		return std::nullopt;
	}
	auto part = QJsonObject();
	if (media.kind == MediaKind::Image) {
		part.insert(u"type"_q, u"input_image"_q);
		part.insert(u"image_url"_q, MediaDataUrl(media));
		part.insert(u"detail"_q, u"auto"_q);
	} else if (media.kind == MediaKind::File) {
		part.insert(u"type"_q, u"input_file"_q);
		part.insert(u"filename"_q, media.name);
		part.insert(u"file_data"_q, MediaDataUrl(media));
	} else {
		return std::nullopt;
	}
	return part;
}

[[nodiscard]] QJsonValue SerializeToolOutput(const ToolResultBlock &result) {
	auto media = std::vector<QJsonObject>();
	auto hasMedia = false;
	for (const auto &block : result.media) {
		if (auto part = SerializeInputMediaPart(block)) {
			hasMedia = true;
			media.push_back(std::move(*part));
		}
	}
	if (!hasMedia) {
		return result.content;
	}
	auto array = QJsonArray();
	if (!result.content.isEmpty()) {
		auto text = QJsonObject();
		text.insert(u"type"_q, u"input_text"_q);
		text.insert(u"text"_q, result.content);
		array.append(text);
	}
	for (auto &part : media) {
		array.append(part);
	}
	return array;
}

[[nodiscard]] QString ArgumentsJson(const ToolUseBlock &use) {
	return use.inputError.isEmpty()
		? QString::fromUtf8(
			QJsonDocument(use.input).toJson(QJsonDocument::Compact))
		: QString::fromUtf8(use.rawInput);
}

[[nodiscard]] const std::vector<KnownModel> &KnownModels() {
	const auto lowToMax = std::vector<QString>{
		u"low"_q,
		u"medium"_q,
		u"high"_q,
		u"xhigh"_q,
		u"max"_q,
	};
	const auto noneToMax = std::vector<QString>{
		u"none"_q,
		u"low"_q,
		u"medium"_q,
		u"high"_q,
		u"xhigh"_q,
		u"max"_q,
	};
	const auto lowToHigh = std::vector<QString>{
		u"low"_q,
		u"medium"_q,
		u"high"_q,
	};
	static const auto result = std::vector<KnownModel>{
		{ u"gpt-6-astra"_q, u"GPT-6 Astra"_q, 10.0, 50.0, 1.0,
			ContextWindowLimit(), true, true, true, lowToMax },
		{ u"gpt-6.1-sol"_q, u"GPT-6.1 Sol"_q, 2.0, 10.0, 0.1,
			ContextWindowLimit(), true, true, true, lowToMax },
		{ u"gpt-6-sol"_q, u"GPT-6 Sol"_q, 2.0, 10.0, 0.2,
			ContextWindowLimit(), true, true, true, lowToMax },
		{ u"gpt-6-luna"_q, u"GPT-6 Luna"_q, 0.1, 0.5, 0.01,
			ContextWindowLimit(), true, true, true, noneToMax },
		{ u"gpt-5.6-sol"_q, u"GPT-5.6 Sol"_q, 4.0, 20.0, 0.4,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.6-terra"_q, u"GPT-5.6 Terra"_q, 2.0, 12.0, 0.2,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.6-luna"_q, u"GPT-5.6 Luna"_q, 0.2, 1.2, 0.02,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.5"_q, u"GPT-5.5"_q, 5.0, 30.0, 0.5,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.5-pro"_q, u"GPT-5.5 Pro"_q, 30.0, 180.0, 0.0,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.4"_q, u"GPT-5.4"_q, 2.5, 15.0, 0.25,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.4-pro"_q, u"GPT-5.4 Pro"_q, 30.0, 180.0, 0.0,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.4-mini"_q, u"GPT-5.4 Mini"_q, 0.75, 4.5, 0.075,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.4-nano"_q, u"GPT-5.4 Nano"_q, 0.2, 1.25, 0.02,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.2"_q, u"GPT-5.2"_q, 1.75, 14.0, 0.175,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5.1"_q, u"GPT-5.1"_q, 1.25, 10.0, 0.125,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5"_q, u"GPT-5"_q, 1.25, 10.0, 0.125,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5-mini"_q, u"GPT-5 Mini"_q, 0.25, 2.0, 0.025,
			ContextWindowLimit(), true, true, true, lowToHigh },
		{ u"gpt-5-nano"_q, u"GPT-5 Nano"_q, 0.05, 0.4, 0.005,
			ContextWindowLimit(), true, true, true, lowToHigh },
	};
	return result;
}

[[nodiscard]] int KnownModelIndex(const QString &id) {
	const auto &models = KnownModels();
	const auto known = FindKnownModel(id);
	for (auto i = 0; i < int(models.size()); ++i) {
		if (known && known->id == models[i].id) {
			return i;
		}
	}
	return int(models.size());
}

template <typename Delta>
void PushDelta(std::vector<StreamDelta> &deltas, const QString &text) {
	if (text.isEmpty()) {
		return;
	} else if (!deltas.empty()) {
		if (const auto last = std::get_if<Delta>(&deltas.back())) {
			last->text += text;
			return;
		}
	}
	deltas.push_back(Delta{ text });
}

[[nodiscard]] QJsonArray SerializeInput(
		const Request &request,
		const QString &model,
		const QString &origin) {
	auto remap = base::flat_map<QString, QString>();
	auto seen = base::flat_set<QString>();
	static const auto valid = QRegularExpression(
		u"^[A-Za-z0-9_-]+$"_q);
	auto result = QJsonArray();
	auto currentTurn = int(request.messages.size());
	for (auto m = 0; m < int(request.messages.size()); ++m) {
		const auto &message = request.messages[m];
		if (message.role != Role::User) {
			continue;
		}
		auto hasResults = false;
		for (const auto &block : message.blocks) {
			if (std::get_if<ToolResultBlock>(&block)) {
				hasResults = true;
				break;
			}
		}
		if (!hasResults) {
			currentTurn = m;
		}
	}
	for (auto m = 0; m < int(request.messages.size()); ++m) {
		const auto &message = request.messages[m];
		auto other = std::vector<Block>();
		for (const auto &block : message.blocks) {
			if (const auto *resultBlock
				= std::get_if<ToolResultBlock>(&block)) {
				auto item = QJsonObject();
				item.insert(u"type"_q, u"function_call_output"_q);
				const auto found = remap.find(resultBlock->toolUseId);
				item.insert(
					u"call_id"_q,
					(found == remap.end())
						? resultBlock->toolUseId
						: found->second);
				item.insert(u"output"_q, SerializeToolOutput(*resultBlock));
				result.append(item);
			} else {
				other.push_back(block);
			}
		}
		if (message.role == Role::User) {
			auto content = QJsonArray();
			for (const auto &block : other) {
				if (const auto *text = std::get_if<TextBlock>(&block)) {
					auto part = QJsonObject();
					part.insert(u"type"_q, u"input_text"_q);
					part.insert(u"text"_q, text->text);
					content.append(part);
				} else if (const auto *media
					= std::get_if<MediaBlock>(&block)) {
					if (auto part = SerializeInputMediaPart(*media)) {
						content.append(*part);
					}
				}
			}
			if (!content.isEmpty()) {
				auto item = QJsonObject();
				item.insert(u"role"_q, u"user"_q);
				item.insert(u"content"_q, content);
				result.append(item);
			}
		} else {
			for (auto b = 0; b < int(other.size()); ++b) {
				const auto &block = other[b];
				if (const auto *thinking
					= std::get_if<ThinkingBlock>(&block)) {
					if (m <= currentTurn) {
						continue;
					}
					if (auto item = ReasoningItem(
						thinking->signature,
						model,
						origin)) {
						result.append(*item);
					}
				} else if (const auto *text = std::get_if<TextBlock>(&block)) {
					if (text->text.isEmpty()) {
						continue;
					}
					auto part = QJsonObject();
					part.insert(u"type"_q, u"output_text"_q);
					part.insert(u"text"_q, text->text);
					auto content = QJsonArray();
					content.append(part);
					auto item = QJsonObject();
					item.insert(u"type"_q, u"message"_q);
					item.insert(u"role"_q, u"assistant"_q);
					item.insert(u"content"_q, content);
					result.append(item);
				} else if (const auto *use = std::get_if<ToolUseBlock>(&block)) {
					auto id = use->id;
					if (id.isEmpty()
						|| !valid.match(id).hasMatch()
						|| seen.contains(id)) {
						id = u"call_aigram_%1_%2"_q.arg(m).arg(b);
					}
					seen.insert(id);
					remap.insert_or_assign(use->id, id);
					auto item = QJsonObject();
					item.insert(u"type"_q, u"function_call"_q);
					item.insert(u"call_id"_q, id);
					item.insert(u"name"_q, use->name);
					item.insert(u"arguments"_q, ArgumentsJson(*use));
					result.append(item);
				}
			}
		}
	}
	return result;
}

} // namespace

ResponsesAdapter::ResponsesAdapter(ProviderSettings settings)
: _settings(std::move(settings)) {
}

HttpRequest ResponsesAdapter::prepare(const Request &request) const {
	auto adjusted = request;
	if (adjusted.model.isEmpty()) {
		adjusted.model = _settings.model;
	}
	if (adjusted.effort.isEmpty()) {
		adjusted.effort = _settings.effort;
	}
	const auto origin = _settings.id;
	const auto options = ResponsesOptions{
		.origin = origin,
		.plan = UsesChatGptPlan(_settings),
	};
	auto result = HttpRequest();
	result.url = SlashJoinedUrl(_settings.baseUrl, u"responses"_q);
	result.body = QJsonDocument(ResponsesRequestObject(adjusted, options))
		.toJson(QJsonDocument::Compact);
	result.post = true;
	result.headers.push_back({ "content-type", "application/json" });
	result.headers.push_back({ "accept", "text/event-stream" });
	if (!_settings.apiKey.isEmpty()) {
		result.headers.push_back(
			{ "Authorization", "Bearer " + _settings.apiKey.toUtf8() });
	}
	return result;
}

std::unique_ptr<ResponseParser> ResponsesAdapter::parser() const {
	return ResponsesParser(_settings.model, _settings.id);
}

QJsonObject ResponsesRequestObject(
		const Request &request,
		const ResponsesOptions &options) {
	auto root = QJsonObject();
	root.insert(u"model"_q, request.model);
	root.insert(u"stream"_q, true);
	root.insert(u"store"_q, false);
	if (!request.system.isEmpty()) {
		root.insert(u"instructions"_q, request.system);
	}
	if (!request.tools.empty()) {
		auto tools = QJsonArray();
		for (const auto &tool : request.tools) {
			auto object = QJsonObject();
			object.insert(u"type"_q, u"function"_q);
			object.insert(u"name"_q, tool.name);
			object.insert(u"description"_q, tool.description);
			object.insert(u"parameters"_q, tool.inputSchema);
			object.insert(u"strict"_q, false);
			tools.append(object);
		}
		root.insert(u"tools"_q, tools);
		root.insert(u"tool_choice"_q, u"auto"_q);
		root.insert(u"parallel_tool_calls"_q, true);
	}
	if (SupportsReasoning(request.model)) {
		auto reasoning = QJsonObject();
		reasoning.insert(u"summary"_q, u"auto"_q);
		if (!request.effort.isEmpty()) {
			reasoning.insert(u"effort"_q, request.effort);
		}
		root.insert(u"reasoning"_q, reasoning);
		root.insert(
			u"include"_q,
			QJsonArray{ u"reasoning.encrypted_content"_q });
	}
	if (!request.cacheKey.isEmpty()) {
		root.insert(u"prompt_cache_key"_q, request.cacheKey);
	}
	if (request.maxOutputTokens > 0 && !options.plan) {
		root.insert(u"max_output_tokens"_q, request.maxOutputTokens);
	}
	root.insert(
		u"input"_q,
		SerializeInput(request, request.model, options.origin));
	return root;
}

QByteArray ReasoningSignature(
		const QString &model,
		const QString &origin,
		const QJsonObject &item) {
	auto clean = QJsonObject(item);
	clean.remove(u"id"_q);
	clean.remove(u"status"_q);
	auto inner = QJsonObject();
	inner.insert(u"model"_q, model);
	inner.insert(u"origin"_q, origin);
	inner.insert(u"item"_q, clean);
	auto root = QJsonObject();
	root.insert(u"openai"_q, inner);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<QJsonObject> ReasoningItem(
		const QByteArray &signature,
		const QString &model,
		const QString &origin) {
	if (signature.isEmpty() || !IsReasoningSignature(signature)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(signature);
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto inner = document.object().value(u"openai"_q).toObject();
	if (inner.value(u"model"_q).toString() != model
		|| inner.value(u"origin"_q).toString() != origin) {
		return std::nullopt;
	}
	auto item = inner.value(u"item"_q).toObject();
	if (item.value(u"type"_q).toString() != u"reasoning"_q
		|| item.value(u"encrypted_content"_q).toString().isEmpty()) {
		return std::nullopt;
	}
	item.remove(u"id"_q);
	item.remove(u"status"_q);
	return item;
}

bool IsReasoningSignature(const QByteArray &signature) {
	return signature.startsWith('{');
}

std::vector<QString> ResponsesEffortLevels() {
	return {
		u"none"_q,
		u"low"_q,
		u"medium"_q,
		u"high"_q,
		u"xhigh"_q,
		u"max"_q,
	};
}

bool SupportsReasoning(const QString &model) {
	return !(model.startsWith(u"gpt-4"_q)
		|| model.startsWith(u"gpt-3"_q)
		|| model.startsWith(u"chatgpt-4o"_q));
}

int ContextWindowLimit() {
	return 272'000;
}

std::optional<KnownModel> FindKnownModel(const QString &id) {
	auto result = std::optional<KnownModel>();
	for (const auto &model : KnownModels()) {
		if (id == model.id
			|| (id.size() > model.id.size()
				&& id.startsWith(model.id)
				&& id[model.id.size()] == '-')) {
			if (!result || model.id.size() > result->id.size()) {
				result = model;
			}
		}
	}
	return result;
}

HttpRequest ResponsesModelsRequest(const ProviderSettings &settings) {
	auto result = HttpRequest();
	result.url = SlashJoinedUrl(settings.baseUrl, u"models"_q);
	result.post = false;
	if (!settings.apiKey.isEmpty()) {
		result.headers.push_back(
			{ "Authorization", "Bearer " + settings.apiKey.toUtf8() });
	}
	return result;
}

std::vector<ModelInfo> ParseResponsesModels(const QByteArray &json) {
	auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return {};
	}
	const auto root = document.object();
	auto list = QJsonArray();
	auto plan = false;
	if (root.value(u"models"_q).isArray()) {
		plan = true;
		list = root.value(u"models"_q).toArray();
	} else if (root.value(u"data"_q).isArray()) {
		list = root.value(u"data"_q).toArray();
	} else {
		return {};
	}
	auto result = std::vector<ModelInfo>();
	result.reserve(list.size());
	for (const auto &value : list) {
		const auto object = value.toObject();
		if (object.isEmpty()) {
			continue;
		}
		auto info = ModelInfo();
		if (plan) {
			const auto visibility = object.value(u"visibility"_q).toString();
			if (!visibility.isEmpty() && visibility != u"list"_q) {
				continue;
			}
			info.id = object.value(u"slug"_q).toString();
			info.name = object.value(u"display_name"_q).toString();
		} else {
			info.id = object.value(u"id"_q).toString();
		}
		if (info.id.isEmpty()) {
			continue;
		}
		if (plan) {
			if (const auto window = object.value(u"context_window"_q)
				; window.isDouble()) {
				info.contextWindow = std::min(
					int(window.toDouble()),
					ContextWindowLimit());
			}
			if (const auto modalities = object.value(u"input_modalities"_q)
				; modalities.isArray()) {
				auto names = QStringList();
				for (const auto &modality : modalities.toArray()) {
					names.push_back(modality.toString());
				}
				info.vision = names.contains(u"image"_q);
				info.files = names.contains(u"file"_q);
			}
			auto levels = std::vector<QString>();
			const auto supported = object
				.value(u"supported_reasoning_levels"_q).toArray();
			for (const auto &level : supported) {
				auto name = QString();
				if (level.isString()) {
					name = level.toString();
				} else if (level.isObject()) {
					name = level.toObject().value(u"effort"_q).toString();
				}
				if (!name.isEmpty()) {
					levels.push_back(name);
				}
			}
			const auto all = ResponsesEffortLevels();
			info.effortLevels.reserve(levels.size());
			for (const auto &name : levels) {
				if (ranges::contains(all, name)) {
					info.effortLevels.push_back(name);
				}
			}
		} else {
			const auto id = info.id.toLower();
			auto skip = !(id.startsWith(u"gpt-"_q)
				|| (id.startsWith('o') && id.size() > 1 && id[1].isDigit()));
			for (const auto &word : std::array{
					u"audio"_q,
					u"realtime"_q,
					u"transcribe"_q,
					u"tts"_q,
					u"image"_q,
					u"embedding"_q,
					u"moderation"_q,
					u"search"_q,
					u"instruct"_q,
					u"codex"_q,
				}) {
				if (id.contains(word)) {
					skip = true;
					break;
				}
			}
			if (skip) {
				continue;
			}
		}
		if (const auto known = FindKnownModel(info.id)) {
			if (info.name.isEmpty()) {
				info.name = known->name;
			}
			info.contextWindow = info.contextWindow > 0
				? info.contextWindow
				: known->contextWindow;
			if (!info.vision) {
				info.vision = known->vision;
			}
			if (!info.files) {
				info.files = known->files;
			}
			if (info.effortLevels.empty()) {
				info.effortLevels = known->effortLevels;
			}
			info.priceInput = known->priceInput;
			info.priceOutput = known->priceOutput;
			info.priceCached = known->priceCached;
		} else {
			if (info.name.isEmpty()) {
				info.name = info.id;
			}
			info.contextWindow = info.contextWindow > 0
				? info.contextWindow
				: ContextWindowLimit();
			if (!info.vision) {
				info.vision = true;
			}
			if (!info.files) {
				info.files = true;
			}
		}
		result.push_back(std::move(info));
	}
	if (!plan) {
		ranges::stable_sort(
			result,
			[](const ModelInfo &a, const ModelInfo &b) {
				const auto left = KnownModelIndex(a.id);
				const auto right = KnownModelIndex(b.id);
				return (left != right) ? (left < right) : (a.id < b.id);
			});
	}
	for (auto &info : result) {
		info.tools = true;
	}
	return result;
}

namespace {

class Parser final : public Agent::ResponseParser {
public:
	Parser(QString model, QString origin)
	: _model(std::move(model)), _origin(std::move(origin)) {
	}

	void head(const HttpHead &head) override;
	void feed(const QByteArray &chunk) override;
	[[nodiscard]] std::vector<StreamDelta> takeDeltas() override;
	[[nodiscard]] Response finish(const QString &networkError) override;

private:
	void collect(const std::vector<SseEvent> &events);
	void processEvent(const QJsonObject &root);

	const QString _model;
	const QString _origin;
	SseParser _sse;
	HttpHead _head;
	QByteArray _errorBody;
	std::vector<StreamDelta> _deltas;
	base::flat_map<int, QJsonObject> _doneItems;
	std::optional<Error> _streamError;
	Usage _usage;
	QString _incompleteReason;
	bool _sawRefusal = false;
	bool _sawFinal = false;

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

void Parser::collect(const std::vector<SseEvent> &events) {
	for (const auto &event : events) {
		if (event.data.isEmpty()) {
			continue;
		}
		const auto document = QJsonDocument::fromJson(event.data);
		if (document.isObject()) {
			processEvent(document.object());
		}
	}
}

void Parser::processEvent(const QJsonObject &root) {
	const auto type = root.value(u"type"_q).toString();
	if (type == u"response.output_item.added"_q) {
		const auto item = root.value(u"item"_q).toObject();
		if (item.value(u"type"_q).toString() == u"function_call"_q) {
			_deltas.push_back(ToolUseStarted{
				item.value(u"call_id"_q).toString(),
				item.value(u"name"_q).toString(),
			});
		}
	} else if (type == u"response.output_text.delta"_q
		|| type == u"response.refusal.delta"_q) {
		if (type == u"response.refusal.delta"_q) {
			_sawRefusal = true;
		}
		PushDelta<TextDelta>(_deltas, root.value(u"delta"_q).toString());
	} else if (type == u"response.reasoning_summary_text.delta"_q) {
		PushDelta<ThinkingDelta>(
			_deltas,
			root.value(u"delta"_q).toString());
	} else if (type == u"response.reasoning_summary_part.added"_q) {
		if (root.value(u"summary_index"_q).toInt(0) > 0) {
			PushDelta<ThinkingDelta>(_deltas, u"\n\n"_q);
		}
	} else if (type == u"response.output_item.done"_q) {
		const auto index = root.value(u"output_index"_q).toInt(-1);
		const auto item = root.value(u"item"_q).toObject();
		if (index >= 0 && !item.isEmpty()) {
			_doneItems.insert_or_assign(index, item);
		}
	} else if (type == u"response.completed"_q
		|| type == u"response.incomplete"_q) {
		_sawFinal = true;
		const auto response = root.value(u"response"_q).toObject();
		if (const auto usage = response.value(u"usage"_q); usage.isObject()) {
			const auto object = usage.toObject();
			_usage.inputTokens = object.value(u"input_tokens"_q).toInt();
			_usage.outputTokens = object.value(u"output_tokens"_q).toInt();
			_usage.cachedInputTokens = object
				.value(u"input_tokens_details"_q).toObject()
				.value(u"cached_tokens"_q).toInt();
		}
		if (type == u"response.incomplete"_q) {
			_incompleteReason = response
				.value(u"incomplete_details"_q).toObject()
				.value(u"reason"_q).toString();
		}
		const auto output = response.value(u"output"_q).toArray();
		for (auto i = 0; i < output.size(); ++i) {
			const auto item = output.at(i).toObject();
			if (!item.isEmpty() && !_doneItems.contains(i)) {
				_doneItems.insert_or_assign(i, item);
			}
		}
	} else if (type == u"response.failed"_q) {
		const auto error = root.value(u"response"_q).toObject()
			.value(u"error"_q).toObject();
		auto streamError = Error();
		streamError.code = error.value(u"code"_q).toString();
		streamError.text = error.value(u"message"_q).toString();
		const auto fallback = (streamError.code == u"rate_limit_exceeded"_q)
			? ErrorKind::RateLimited
			: (streamError.code == u"server_error"_q)
			? ErrorKind::Overloaded
			: ErrorKind::BadResponse;
		streamError.kind = ErrorKindForCode(streamError.code, fallback);
		if (!_streamError) {
			_streamError = streamError;
		}
	} else if (type == u"error"_q) {
		auto error = root.value(u"error"_q).toObject();
		if (error.isEmpty()) {
			error = root;
		}
		auto streamError = Error();
		streamError.code = error.value(u"code"_q).toString();
		streamError.text = error.value(u"message"_q).toString();
		const auto fallback = (streamError.code == u"rate_limit_exceeded"_q)
			? ErrorKind::RateLimited
			: (streamError.code == u"server_error"_q)
			? ErrorKind::Overloaded
			: ErrorKind::BadResponse;
		streamError.kind = ErrorKindForCode(streamError.code, fallback);
		if (!_streamError) {
			_streamError = streamError;
		}
	}
}

} // namespace

std::unique_ptr<ResponseParser> ResponsesParser(
		const QString &model,
		const QString &origin) {
	return std::make_unique<Parser>(model, origin);
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
		response.error = ParseResponsesError(_head, _errorBody);
		return response;
	}
	collect(_sse.finish());
	if (_streamError) {
		response.error = *_streamError;
		return response;
	} else if (!_sawFinal) {
		auto error = Error();
		error.kind = ErrorKind::BadResponse;
		error.httpStatus = _head.status;
		error.text = u"Stream ended without a final response event"_q;
		response.error = std::move(error);
		return response;
	}
	auto blocks = std::vector<Block>();
	auto toolUses = false;
	for (const auto &entry : _doneItems) {
		const auto &item = entry.second;
		const auto type = item.value(u"type"_q).toString();
		if (type == u"reasoning"_q) {
			auto texts = QStringList();
			const auto summary = item.value(u"summary"_q).toArray();
			for (const auto &part : summary) {
				const auto text = part.toObject().value(u"text"_q).toString();
				if (!text.isEmpty()) {
					texts.push_back(text);
				}
			}
			auto block = ThinkingBlock();
			block.text = texts.join(u"\n\n"_q);
			const auto encrypted = item
				.value(u"encrypted_content"_q).toString();
			block.signature = encrypted.isEmpty()
				? QByteArray()
				: ReasoningSignature(_model, _origin, item);
			if (!block.text.isEmpty() || !block.signature.isEmpty()) {
				blocks.push_back(std::move(block));
			}
		} else if (type == u"message"_q) {
			auto text = QString();
			const auto content = item.value(u"content"_q).toArray();
			for (const auto &part : content) {
				const auto object = part.toObject();
				const auto partType = object.value(u"type"_q).toString();
				if (partType == u"output_text"_q) {
					text += object.value(u"text"_q).toString();
				} else if (partType == u"refusal"_q) {
					_sawRefusal = true;
					text += object.value(u"refusal"_q).toString();
				}
			}
			if (!text.isEmpty()) {
				blocks.push_back(TextBlock{ text });
			}
		} else if (type == u"function_call"_q) {
			auto use = ToolUseBlock();
			use.id = item.value(u"call_id"_q).toString();
			use.name = item.value(u"name"_q).toString();
			const auto arguments = item
				.value(u"arguments"_q).toString().toUtf8();
			if (arguments.isEmpty()) {
				use.input = QJsonObject();
			} else {
				const auto document = QJsonDocument::fromJson(arguments);
				if (document.isObject()) {
					use.input = document.object();
				} else {
					use.inputError = u"Arguments are not a JSON object"_q;
					use.rawInput = arguments;
				}
			}
			blocks.push_back(std::move(use));
			toolUses = true;
		}
	}
	response.message = Message{ Role::Assistant, std::move(blocks) };
	response.stopReason = toolUses
		? StopReason::ToolUse
		: (_incompleteReason == u"max_output_tokens"_q)
		? StopReason::MaxTokens
		: (_sawRefusal || _incompleteReason == u"content_filter"_q)
		? StopReason::Refusal
		: StopReason::EndTurn;
	response.usage = _usage;
	return response;
}

ErrorKind ErrorKindForCode(const QString &code, ErrorKind fallback) {
	if (code == u"subscription_sharing_usage_limit_exceeded"_q) {
		return ErrorKind::RateLimited;
	} else if (code == u"subscription_sharing_usage_unavailable"_q
		|| code == u"subscription_sharing_user_unavailable"_q) {
		return ErrorKind::Overloaded;
	} else if (code == u"subscription_sharing_user_not_eligible"_q
		|| code == u"subscription_sharing_invalid_user"_q
		|| code == u"chatpass_v2_scope_not_authorized"_q
		|| code == u"chatpass_v2_invalid_authorization_context"_q
		|| code == u"invalid_api_key"_q
		|| code == u"chatgpt_signed_out"_q) {
		return ErrorKind::Auth;
	} else if (code == u"subscription_sharing_unsupported_capability"_q
		|| code == u"subscription_sharing_route_not_supported"_q) {
		return ErrorKind::BadRequest;
	}
	return fallback;
}

Error ParseResponsesError(const HttpHead &head, const QByteArray &body) {
	auto result = Error();
	result.kind = KindForStatus(head.status);
	result.httpStatus = head.status;
	result.retryAfter = RetryAfterMs(head.retryAfter);
	auto text = QString();
	auto code = QString();
	const auto document = QJsonDocument::fromJson(body);
	if (document.isObject()) {
		const auto root = document.object();
		const auto error = root.value(u"error"_q).toObject();
		if (!error.isEmpty()) {
			text = error.value(u"message"_q).toString();
			code = error.value(u"code"_q).toString();
			if (code.isEmpty()) {
				code = error.value(u"type"_q).toString();
			}
		} else if (root.value(u"detail"_q).isString()) {
			text = root.value(u"detail"_q).toString();
		}
	}
	if (text.isEmpty()) {
		text = QString::fromUtf8(body.mid(0, kErrorBodyLimit)).trimmed();
	}
	result.text = text;
	result.code = code;
	result.kind = code.isEmpty()
		? result.kind
		: ErrorKindForCode(code, result.kind);
	return result;
}

} // namespace Agent::OpenAi
