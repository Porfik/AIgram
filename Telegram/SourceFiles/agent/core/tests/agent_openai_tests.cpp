/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_openai.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <cmath>
#include <random>

namespace Agent::Tests {
namespace {

[[nodiscard]] std::vector<QByteArray> SplitChunks(
		const QByteArray &bytes,
		int mode) {
	auto result = std::vector<QByteArray>();
	if (mode == 0) {
		result.push_back(bytes);
	} else {
		auto generator = std::mt19937(mode);
		auto position = qsizetype(0);
		while (position < bytes.size()) {
			const auto size = (mode == 1)
				? 1
				: int(1 + generator() % 7);
			result.push_back(bytes.mid(position, size));
			position += size;
		}
	}
	return result;
}

[[nodiscard]] Response ParseResponse(const QByteArray &bytes, int mode) {
	auto parser = OpenAi::Adapter(ProviderSettings()).parser();
	parser->head(HttpHead{ 200, QByteArray() });
	for (const auto &chunk : SplitChunks(bytes, mode)) {
		parser->feed(chunk);
	}
	return parser->finish(QString());
}

[[nodiscard]] QString DescribeBlock(const Block &block) {
	if (const auto *text = std::get_if<TextBlock>(&block)) {
		return u"text(%1)"_q.arg(text->text);
	} else if (const auto *thinking = std::get_if<ThinkingBlock>(&block)) {
		return u"thinking(%1, sig=%2)"_q.arg(
			thinking->text,
			QString::fromUtf8(thinking->signature));
	} else if (const auto *use = std::get_if<ToolUseBlock>(&block)) {
		return u"use(%1, %2, input=%3, errorSet=%4, raw=%5)"_q
			.arg(use->id, use->name)
			.arg(QString::fromUtf8(
				QJsonDocument(use->input).toJson(QJsonDocument::Compact)))
			.arg(use->inputError.isEmpty() ? u"no"_q : u"yes"_q)
			.arg(QString::fromUtf8(use->rawInput));
	}
	return u"other"_q;
}

[[nodiscard]] QString DescribeMessage(const Message &message) {
	auto result = (message.role == Role::Assistant)
		? u"assistant["_q
		: u"user["_q;
	for (const auto &block : message.blocks) {
		result += DescribeBlock(block) + u"; "_q;
	}
	return result + u"]"_q;
}

[[nodiscard]] QString DescribeResponse(const Response &response) {
	auto result = QString();
	if (response.error) {
		result += u"error(%1, %2, %3, %4) "_q
			.arg(ErrorKindName(response.error->kind))
			.arg(response.error->httpStatus)
			.arg(response.error->text)
			.arg(qint64(response.error->retryAfter));
	}
	result += u"stop(%1) usage(%2, %3, %4) %5"_q
		.arg(int(response.stopReason))
		.arg(response.usage.inputTokens)
		.arg(response.usage.outputTokens)
		.arg(response.usage.cachedInputTokens)
		.arg(DescribeMessage(response.message));
	return result;
}

[[nodiscard]] QString CheckNoError(const Response &response) {
	return response.error
		? u"unexpected error: %1"_q.arg(response.error->text)
		: QString();
}

[[nodiscard]] QString CheckStopReason(
		const Response &response,
		StopReason stop) {
	return (response.stopReason == stop)
		? QString()
		: u"stop reason: got %1, expected %2"_q
			.arg(int(response.stopReason))
			.arg(int(stop));
}

[[nodiscard]] QString CheckUsage(
		const Response &response,
		int input,
		int output,
		int cached) {
	const auto ok = response.usage.inputTokens == input
		&& response.usage.outputTokens == output
		&& response.usage.cachedInputTokens == cached;
	return ok
		? QString()
		: u"usage: got %1, %2, %3, expected %4, %5, %6"_q
			.arg(response.usage.inputTokens)
			.arg(response.usage.outputTokens)
			.arg(response.usage.cachedInputTokens)
			.arg(input)
			.arg(output)
			.arg(cached);
}

[[nodiscard]] QString CheckCost(const Response &response, double cost) {
	if (response.usage.hasCost == (cost != 0.)
		&& qFuzzyCompare(response.usage.cost, cost)) {
		return QString();
	}
	return u"cost: got %1 (hasCost %2), expected %3"_q
		.arg(response.usage.cost)
		.arg(response.usage.hasCost ? u"yes"_q : u"no"_q)
		.arg(cost);
}

[[nodiscard]] QString CheckBlocks(
		const Response &response,
		const std::vector<Block> &expected) {
	const auto &blocks = response.message.blocks;
	if (response.message.role != Role::Assistant) {
		return u"message is not assistant"_q;
	} else if (blocks.size() != expected.size()) {
		return u"blocks: got %1 (%2), expected %3 (%4)"_q
			.arg(blocks.size())
			.arg(DescribeMessage(response.message))
			.arg(expected.size())
			.arg(DescribeMessage(Message{ Role::Assistant, expected }));
	}
	auto index = 0;
	for (const auto &block : expected) {
		const auto description = DescribeBlock(blocks[index]);
		if (blocks[index].index() != block.index()) {
			return u"block %1: got %2, expected %3"_q
				.arg(index)
				.arg(description)
				.arg(DescribeBlock(block));
		}
		if (description != DescribeBlock(block)) {
			return u"block %1: got %2, expected %3"_q
				.arg(index)
				.arg(description)
				.arg(DescribeBlock(block));
		}
		++index;
	}
	return QString();
}

[[nodiscard]] QString CheckFixtureResponse(
		const QString &name,
		Fn<QString(const Response &)> check) {
	const auto bytes = ReadFixture(name);
	if (bytes.isEmpty()) {
		return u"fixture is empty or missing: "_q + name;
	}
	auto reference = QString();
	for (const auto mode : { 0, 1, 2 }) {
		const auto response = ParseResponse(bytes, mode);
		const auto described = DescribeResponse(response);
		if (reference.isEmpty()) {
			reference = described;
		} else if (described != reference) {
			return u"%1 (mode %2): got %3, expected %4"_q
				.arg(name)
				.arg(mode)
				.arg(described, reference);
		}
		const auto failure = check(response);
		if (!failure.isEmpty()) {
			return u"%1: %2"_q.arg(name, failure);
		}
	}
	return QString();
}

[[nodiscard]] QString CheckHeader(
		const HttpRequest &request,
		const QString &name,
		const QString &value) {
	for (const auto &header : request.headers) {
		if (header.first == name.toUtf8()) {
			return (header.second == value.toUtf8())
				? QString()
				: (u"%1: got %2, expected %3"_q.arg(
					name,
					QString::fromUtf8(header.second),
					value));
		}
	}
	return u"%1 header missing"_q.arg(name);
}

[[nodiscard]] bool HasHeader(const HttpRequest &request, const QString &name) {
	for (const auto &header : request.headers) {
		if (header.first == name.toUtf8()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] std::vector<Check> FixtureChecks() {
	const auto kReasoning = (u"The user wants me to reply with exactly "_q
		+ u"\"Привет, мир!\" which is Russian for "_q
		+ u"\"Hello, world!\"\n\nThey said "_q
		+ u"\"Reply with exactly\" so I should just output "_q
		+ u"that text without any additional content."_q);
	const auto kParallelThinking = (u"The user wants to know the weather "_q
		+ u"in two cities: Paris and Tokyo.\nI need to use the "_q
		+ u"`weather_api` tool.\nThe request explicitly asks to "_q
		+ u"\"Call the tool for both cities at once.\"\n\nI will "_q
		+ u"structure the tool call to fetch the weather for both "_q
		+ u"\"Paris\" and \"Tokyo\" simultaneously."_q);
	const auto kWeatherText = (u"The weather in Paris is currently "_q
		+ u"**18°C with light rain**. You might want to bring an "_q
		+ u"umbrella if you're heading out! 🌧️"_q);
	return {
		{ u"openai_text_fixture"_q, [kReasoning] {
			return CheckFixtureResponse(u"openai-text.sse"_q,
				[&](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckBlocks(response, {
						ThinkingBlock{ kReasoning, QByteArray() },
						TextBlock{ u"Привет, мир!"_q },
					});
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckStopReason(response, StopReason::EndTurn);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckUsage(response, 21, 49, 0);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckCost(response, 0.00001634535);
				});
		} },
		{ u"openai_text_deltas_merge"_q, [kReasoning] {
			const auto response = ParseResponse(
				ReadFixture(u"openai-text.sse"_q), 0);
			const auto failure = CheckNoError(response);
			if (!failure.isEmpty()) {
				return failure;
			}
			auto parser = OpenAi::Adapter(ProviderSettings()).parser();
			parser->head(HttpHead{ 200, QByteArray() });
			parser->feed(ReadFixture(u"openai-text.sse"_q));
			const auto deltas = parser->takeDeltas();
			auto more = parser->takeDeltas();
			if (deltas.size() != 2 || more.size() != 0) {
				return u"deltas: got %1 then %2, expected 2 then 0"_q
					.arg(deltas.size())
					.arg(more.size());
			}
			const auto *thinking = std::get_if<ThinkingDelta>(&deltas[0]);
			const auto *text = std::get_if<TextDelta>(&deltas[1]);
			if (!thinking || !text) {
				return u"delta kinds wrong"_q;
			} else if (thinking->text != kReasoning) {
				return u"thinking delta: got %1"_q.arg(thinking->text);
			} else if (text->text != u"Привет, мир!"_q) {
				return u"text delta: got %1"_q.arg(text->text);
			}
			return QString();
		} },
		{ u"openai_tool_call_fixture"_q, [] {
			return CheckFixtureResponse(u"openai-tool-call.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckBlocks(response, {
						ToolUseBlock{
							u"chatcmpl-tool-f87e92f6ee59446d9de647f1e44e1ee6"_q,
							u"get_weather"_q,
							QJsonObject{ { u"city"_q, u"Paris"_q } },
							QString(),
							QByteArray(),
						},
					});
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckStopReason(response, StopReason::ToolUse);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckUsage(response, 166, 12, 0);
				});
		} },
		{ u"openai_parallel_tools_fixture"_q, [kParallelThinking] {
			return CheckFixtureResponse(u"openai-parallel-tools.sse"_q,
				[&](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckBlocks(response, {
						ThinkingBlock{ kParallelThinking, QByteArray() },
						ToolUseBlock{
							u"chatcmpl-tool-9875d8d0a93845bb"_q,
							u"get_weather"_q,
							QJsonObject{ { u"city"_q, u"Paris"_q } },
							QString(),
							QByteArray(),
						},
						ToolUseBlock{
							u"chatcmpl-tool-9d79b3faa3625cfd"_q,
							u"get_weather"_q,
							QJsonObject{ { u"city"_q, u"Tokyo"_q } },
							QString(),
							QByteArray(),
						},
					});
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckStopReason(response, StopReason::ToolUse);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckUsage(response, 202, 108, 0);
				});
		} },
		{ u"openai_tool_result_fixture"_q, [kWeatherText] {
			return CheckFixtureResponse(u"openai-tool-result.sse"_q,
				[&](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckBlocks(response, {
						TextBlock{ kWeatherText },
					});
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckStopReason(response, StopReason::EndTurn);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckUsage(response, 187, 33, 0);
				});
		} },
		{ u"openai_tool_use_started_deltas"_q, [] {
			const auto bytes = ReadFixture(u"openai-parallel-tools.sse"_q);
			auto parser = OpenAi::Adapter(ProviderSettings()).parser();
			parser->head(HttpHead{ 200, QByteArray() });
			parser->feed(bytes);
			const auto deltas = parser->takeDeltas();
			auto started = 0;
			auto index = 0;
			for (const auto &delta : deltas) {
				if (const auto *use = std::get_if<ToolUseStarted>(&delta)) {
					const auto expected = (index == 0)
						? u"chatcmpl-tool-9875d8d0a93845bb"_q
						: u"chatcmpl-tool-9d79b3faa3625cfd"_q;
					if (use->id != expected || use->name != u"get_weather"_q) {
						return u"started %1: got %2, %3"_q
							.arg(index)
							.arg(use->id, use->name);
					}
					++started;
					++index;
				}
			}
			return (started == 2)
				? QString()
				: u"started: got %1, expected 2"_q.arg(started);
		} },
		{ u"openai_stream_error_fixture"_q, [] {
			return CheckFixtureResponse(u"openai-stream-error.sse"_q,
				[](const Response &response) {
					if (!response.error) {
						return u"expected a stream error"_q;
					} else if (response.error->kind != ErrorKind::RateLimited
						|| response.error->httpStatus != 429
						|| response.error->text
							!= u"Provider returned an error"_q
						|| response.error->retryAfter != 0) {
						return u"wrong error: %1, %2, %3"_q
							.arg(ErrorKindName(response.error->kind))
							.arg(response.error->httpStatus)
							.arg(response.error->text);
					}
					return QString();
				});
		} },
		{ u"openai_truncated_stream_fixture"_q, [] {
			return CheckFixtureResponse(u"openai-truncated.sse"_q,
				[](const Response &response) {
					if (!response.error) {
						return u"expected a bad response error"_q;
					} else if (response.error->kind != ErrorKind::BadResponse) {
						return u"wrong kind: %1"_q
							.arg(ErrorKindName(response.error->kind));
					}
					return QString();
				});
		} },
		{ u"openai_invalid_tool_arguments_fixture"_q, [] {
			return CheckFixtureResponse(u"openai-invalid-tool-args.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckBlocks(response, {
						ToolUseBlock{
							u"chatcmpl-tool-0011aa22bb33"_q,
							u"get_weather"_q,
							QJsonObject(),
							u"not empty"_q,
							QByteArray("not-json{"),
						},
					});
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckStopReason(response, StopReason::ToolUse);
				});
		} },
		{ u"openai_tool_call_without_id_fixture"_q, [] {
			return CheckFixtureResponse(u"openai-tool-call-no-id.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckBlocks(response, {
						ToolUseBlock{
							u"call_0"_q,
							u"get_weather"_q,
							QJsonObject{ { u"city"_q, u"Paris"_q } },
							QString(),
							QByteArray(),
						},
					});
				});
		} },
		{ u"openai_length_finish_fixture"_q, [] {
			return CheckFixtureResponse(u"openai-length-finish.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckBlocks(response, {
						TextBlock{ u"Truncated answer"_q },
					});
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckStopReason(response, StopReason::MaxTokens);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckUsage(response, 10, 20, 4);
				});
		} },
		{ u"openai_text_cost"_q, [] {
			return CheckFixtureResponse(u"openai-text.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckCost(response, 0.00001634535);
				});
		} },
		{ u"openai_length_finish_no_cost"_q, [] {
			return CheckFixtureResponse(u"openai-length-finish.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckCost(response, 0.);
				});
		} },
		{ u"openai_byok_cost"_q, [] {
			return CheckFixtureResponse(u"openai-byok.sse"_q,
				[](const Response &response) {
					auto failure = CheckNoError(response);
					if (!failure.isEmpty()) {
						return failure;
					}
					failure = CheckUsage(response, 100, 50, 0);
					if (!failure.isEmpty()) {
						return failure;
					}
					return CheckCost(response, 0.00007);
				});
		} },
	};
}

[[nodiscard]] std::vector<Check> ResponseChecks() {
	return {
		{ u"openai_parse_error_statuses"_q, [] {
			struct Case {
				int status;
				QByteArray retryAfter;
				QByteArray body;
				ErrorKind kind;
				QString text;
				crl::time retryAfterMs;
			};
			const auto cases = std::vector<Case>{
				{ 401, "", ReadFixture(u"openai-error-401.json"_q),
					ErrorKind::Auth, u"User not found."_q, 0 },
				{ 400, "", ReadFixture(u"openai-error-400.json"_q),
					ErrorKind::BadRequest,
					u"no-such/model-xyz is not a valid model ID"_q, 0 },
				{ 500, "", "boom",
					ErrorKind::Overloaded, "boom", 0 },
				{ 429, "12", "boom",
					ErrorKind::RateLimited, "boom", 12000 },
				{ 429, "Wed, 21 Oct 2015 07:28:00 GMT", "boom",
					ErrorKind::RateLimited, "boom", 0 },
				{ 404, "", "not found",
					ErrorKind::BadRequest, "not found", 0 },
				{ 503, "", "unavailable",
					ErrorKind::Overloaded, "unavailable", 0 },
				{ 418, "", "teapot",
					ErrorKind::BadRequest, "teapot", 0 },
			};
			for (const auto &item : cases) {
				const auto error = OpenAi::ParseError(
					HttpHead{ item.status, item.retryAfter },
					item.body);
				if (error.kind != item.kind
					|| error.httpStatus != item.status
					|| error.text != item.text
					|| error.retryAfter != item.retryAfterMs) {
					return u"status %1: got %2, %3, %4, %5"_q
						.arg(item.status)
						.arg(ErrorKindName(error.kind))
						.arg(error.httpStatus)
						.arg(error.text)
						.arg(qint64(error.retryAfter));
				}
			}
			return QString();
		} },
		{ u"openai_parse_models_fixture"_q, [] {
			const auto models = OpenAi::ParseModels(
				ReadFixture(u"openai-models.json"_q));
			if (models.size() != 3) {
				return u"models: got %1, expected 3"_q.arg(models.size());
			}
			struct Expected {
				QString id;
				int contextWindow;
				bool tools;
				bool vision;
			};
			const auto expected = std::vector<Expected>{
				{ u"z-ai/glm-5.3-flash"_q, 1048576, true, true },
				{ u"google/gemini-2.5-flash"_q, 1048576, true, true },
				{ u"openai/gpt-4o-mini"_q, 128000, true, true },
			};
			auto index = 0;
			for (const auto &item : expected) {
				const auto &model = models[index];
				if (model.id != item.id
					|| model.contextWindow != item.contextWindow
					|| model.tools != item.tools
					|| model.vision != item.vision) {
					return u"model %1: got %2, %3, tools %4, vision %5"_q
						.arg(index)
						.arg(model.id)
						.arg(model.contextWindow)
						.arg(model.tools.has_value() ? *model.tools : false)
						.arg(model.vision.has_value() ? *model.vision : false);
				}
				++index;
			}
			return QString();
		} },
		{ u"openai_parse_models_flags"_q, [] {
			const auto json = QByteArray(R"({"data":[
				{"id":"a","context_length":8192},
				{"id":"b","supported_parameters":["max_tokens"],
					"architecture":{"input_modalities":["text"]}},
				{"id":"c","supported_parameters":["tools","max_tokens"],
					"architecture":{"input_modalities":["text","image"]}}
			]})");
			const auto models = OpenAi::ParseModels(json);
			if (models.size() != 3) {
				return u"models: got %1, expected 3"_q.arg(models.size());
			} else if (models[0].tools.has_value()
				|| models[0].vision.has_value()) {
				return u"model a: flags must be absent"_q;
			} else if (models[1].tools.value_or(true)
				|| models[1].vision.value_or(true)) {
				return u"model b: flags must be false"_q;
			} else if (!models[2].tools.value_or(false)
				|| !models[2].vision.value_or(false)) {
				return u"model c: flags must be true"_q;
			} else if (models[0].contextWindow != 8192) {
				return u"model a: context window %1"_q
					.arg(models[0].contextWindow);
			}
			return QString();
		} },
		{ u"openai_parse_models_modalities"_q, [] {
			const auto json = QByteArray(R"({"data":[
				{"id":"a","supported_parameters":["tools"],
					"architecture":{"input_modalities":[
						"text","audio","video","file"]}},
				{"id":"b","supported_parameters":["tools"],
					"architecture":{"input_modalities":["text"]}}
			]})");
			const auto models = OpenAi::ParseModels(json);
			if (models.size() != 2) {
				return u"models: got %1, expected 2"_q.arg(models.size());
			}
			const auto &full = models[0];
			const auto &text = models[1];
			if (!full.audio.value_or(false)
				|| !full.video.value_or(false)
				|| !full.files.value_or(false)) {
				return u"audio, video and files must be set"_q;
			}
			if (text.audio.value_or(true)
				|| text.video.value_or(true)
				|| text.files.value_or(true)) {
				return u"modalities must be false"_q;
			}
			return QString();
		} },
		{ u"openai_parse_models_prices_and_names"_q, [] {
			const auto models = OpenAi::ParseModels(
				ReadFixture(u"openrouter-models.json"_q));
			if (models.size() != 6) {
				return u"models: got %1, expected 6"_q.arg(models.size());
			}
			struct Expected {
				QString id;
				QString name;
				double priceInput;
				double priceOutput;
				double priceCached;
			};
			const auto expected = std::vector<Expected>{
				{ u"z-ai/glm-5.3-flash"_q,
					u"Z.ai: GLM 5.3 Flash"_q, 2.8, 8.0, 0.2 },
				{ u"z-ai/glm-4.6"_q,
					u"Z.ai: GLM 4.6"_q, 3.0, 12.0, 0.3 },
				{ u"openai/gpt-4o-mini"_q,
					u"OpenAI: GPT-4o Mini"_q, 0., 0., 0. },
				{ u"google/gemini-2.5-flash"_q,
					u"Google: Gemini 2.5 Flash"_q, 0., 0., 0. },
				{ u"qwen/qwen3-235b"_q,
					u"Qwen: Qwen3 235B"_q, 1.4, 7.0, 0.1 },
				{ u"deepseek/deepseek-chat-v4"_q,
					u"DeepSeek: DeepSeek V4"_q, 0.19, 0.97, 0.02 },
			};
			auto index = 0;
			for (const auto &item : expected) {
				const auto &model = models[index];
				const auto close = [](double a, double b) {
					return std::abs(a - b) < 1e-9;
				};
				if (model.id != item.id
					|| model.name != item.name
					|| !close(model.priceInput, item.priceInput)
					|| !close(model.priceOutput, item.priceOutput)
					|| !close(model.priceCached, item.priceCached)) {
					return u"model %1: got %2, %3, prices %4, %5, %6"_q
						.arg(index)
						.arg(model.id, model.name)
						.arg(model.priceInput)
						.arg(model.priceOutput)
						.arg(model.priceCached);
				}
				++index;
			}
			return QString();
		} },
		{ u"openai_search_models"_q, [] {
			const auto models = OpenAi::ParseModels(
				ReadFixture(u"openrouter-models.json"_q));
			const auto search = [&](const QString &query) {
				auto text = QString();
				for (const auto index : OpenAi::SearchModels(models, query)) {
					text += QString::number(index) + u","_q;
				}
				return text;
			};
			if (search(QString()) != u"0,1,2,3,4,5,"_q) {
				return u"an empty query must match everything"_q;
			} else if (search(u"   \t "_q) != u"0,1,2,3,4,5,"_q) {
				return u"a blank query must match everything"_q;
			} else if (search(u"glm"_q) != u"0,1,"_q) {
				return u"glm: got %1, expected 0,1"_q.arg(search(u"glm"_q));
			} else if (search(u"GLM"_q) != u"0,1,"_q) {
				return u"the search must ignore case"_q;
			} else if (search(u"glm 5.3 fl"_q) != u"0,"_q) {
				return u"glm 5.3 fl: got %1, expected 0"_q
					.arg(search(u"glm 5.3 fl"_q));
			} else if (search(u"z-ai"_q) != u"0,1,"_q) {
				return u"z-ai: got %1, expected 0,1"_q.arg(search(u"z-ai"_q));
			} else if (search(u"flash glm"_q) != u"0,"_q) {
				return u"the word order must not matter"_q;
			} else if (!search(u"lash"_q).isEmpty()) {
				return u"only prefixes must match"_q;
			}
			return QString();
		} },
	};
}

[[nodiscard]] Request GoldenRequest() {
	auto request = Request();
	request.model = u"test-model"_q;
	request.system = u"You are helpful."_q;
	request.maxOutputTokens = 1024;
	request.messages.push_back(Message{
		Role::User,
		{ TextBlock{ u"Hello"_q } },
	});
	request.messages.push_back(Message{
		Role::Assistant,
		{
			TextBlock{ u"Weather it is."_q },
			ThinkingBlock{ u"thinking"_q, QByteArray("signature") },
			ToolUseBlock{
				u"toolu_1"_q,
				u"get_weather"_q,
				QJsonObject{ { u"city"_q, u"Paris"_q } },
				QString(),
				QByteArray(),
			},
			ToolUseBlock{
				u"toolu_2"_q,
				u"get_time"_q,
				QJsonObject(),
				u"broken"_q,
				QByteArray("raw\"bits"),
			},
		},
	});
	request.messages.push_back(Message{
		Role::User,
		{
			ToolResultBlock{ u"toolu_1"_q, u"18°C"_q, false },
			ToolResultBlock{ u"toolu_2"_q, u"failed"_q, true },
			TextBlock{ u"What now?"_q },
			MediaBlock{
				.bytes = QByteArray("\x89" "PN", 3),
				.mimeType = u"image/png"_q,
			},
		},
	});
	request.messages.push_back(Message{
		Role::User,
		{ ToolResultBlock{ u"toolu_3"_q, u"done"_q, false } },
	});
	request.tools.push_back(ToolSpec{
		u"get_weather"_q,
		u"Get weather"_q,
		QJsonObject{ { u"type"_q, u"object"_q } },
	});
	return request;
}

[[nodiscard]] std::vector<Check> OpenAiRequestChecks() {
	return {
		{ u"openai_serialize_request_golden"_q, [] {
			const auto serialized = OpenAi::SerializeRequest(GoldenRequest());
			const auto document = QJsonDocument::fromJson(serialized);
			const auto expected = QByteArray(R"json({
"max_tokens":1024,
"messages":[
{"content":"You are helpful.","role":"system"},
{"content":"Hello","role":"user"},
{"content":"Weather it is.","role":"assistant","tool_calls":[
{"function":{
"arguments":"{\"city\":\"Paris\"}",
"name":"get_weather"},
"id":"toolu_1","type":"function"},
{"function":{
"arguments":"raw\"bits",
"name":"get_time"},
"id":"toolu_2","type":"function"}]},
{"content":"18°C","role":"tool","tool_call_id":"toolu_1"},
{"content":"failed","role":"tool","tool_call_id":"toolu_2"},
{"content":[
{"text":"What now?","type":"text"},
{"image_url":{
"url":"data:image/png;base64,iVBO"},
"type":"image_url"}],
"role":"user"},
{"content":"done","role":"tool","tool_call_id":"toolu_3"}
],
"model":"test-model",
"stream":true,
"stream_options":{"include_usage":true},
"tools":[{"function":{
"description":"Get weather",
"name":"get_weather",
"parameters":{"type":"object"}},
"type":"function"}]})json");
			if (serialized.contains('\n') || serialized.contains('\r')) {
				return u"serialized JSON is not compact"_q;
			} else if (document != QJsonDocument::fromJson(expected)) {
				return u"document: got %1, expected %2"_q
					.arg(QString::fromUtf8(serialized))
					.arg(QString::fromUtf8(expected));
			}
			return QString();
		} },
		{ u"openai_serialize_request_minimal"_q, [] {
			auto request = Request();
			request.model = u"m"_q;
			const auto document = QJsonDocument::fromJson(
				OpenAi::SerializeRequest(request));
			const auto object = document.object();
			if (!object.contains("stream")
				|| !object.value("stream").toBool()) {
				return u"stream must be true"_q;
			} else if (object.value("stream_options")
					.toObject().value("include_usage").toBool() != true) {
				return u"include_usage must be true"_q;
			} else if (object.contains("max_tokens")
				|| object.contains("tools")
				|| object.contains("system")) {
				return u"empty request has optional keys"_q;
			}
			return QString();
		} },
		{ u"openai_prepare_request"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://openrouter.ai/api/v1"_q;
			settings.apiKey = u"sk-test"_q;
			settings.model = u"z-ai/glm-5.3-flash"_q;
			const auto prepared = OpenAi::Adapter(settings).prepare(
				Request());
			const auto url = u"https://openrouter.ai/api/v1/chat/completions"_q;
			auto failure = (prepared.url == url)
				? QString()
				: u"url: got %1, expected %2"_q.arg(prepared.url, url);
			if (!failure.isEmpty()) {
				return failure;
			}
			failure = CheckHeader(
				prepared, "Content-Type", "application/json");
			if (!failure.isEmpty()) {
				return failure;
			}
			failure = CheckHeader(
				prepared, "Accept", "text/event-stream");
			if (!failure.isEmpty()) {
				return failure;
			}
			failure = CheckHeader(prepared, "Authorization", "Bearer sk-test");
			if (!failure.isEmpty()) {
				return failure;
			} else if (!prepared.post) {
				return u"prepare must post"_q;
			}
			const auto document = QJsonDocument::fromJson(prepared.body);
			const auto model = document.object().value("model").toString();
			return (model == u"z-ai/glm-5.3-flash"_q)
				? QString()
				: u"model fallback: got %1"_q.arg(model);
		} },
		{ u"openai_prepare_cache_key"_q, [] {
			auto request = Request();
			request.cacheKey = u"0123456789abcdef"_q;
			const auto body = [](const HttpRequest &prepared) {
				return QJsonDocument::fromJson(prepared.body).object();
			};
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://openrouter.ai/api/v1"_q;
			const auto router = OpenAi::Adapter(settings).prepare(request);
			if (const auto failure = CheckHeader(
					router,
					"x-session-id",
					"0123456789abcdef");
				!failure.isEmpty()) {
				return failure;
			} else if (body(router).contains(u"prompt_cache_key"_q)) {
				return u"openrouter must not get prompt_cache_key"_q;
			}
			settings.baseUrl = u"https://API.openai.com/v1"_q;
			const auto openai = OpenAi::Adapter(settings).prepare(request);
			if (HasHeader(openai, "x-session-id")) {
				return u"openai must not get x-session-id"_q;
			} else if (body(openai).value(u"prompt_cache_key"_q).toString()
					!= u"0123456789abcdef"_q) {
				return u"openai misses prompt_cache_key"_q;
			}
			settings.baseUrl = u"http://localhost:1234/v1"_q;
			const auto local = OpenAi::Adapter(settings).prepare(request);
			if (HasHeader(local, "x-session-id")
				|| body(local).contains(u"prompt_cache_key"_q)) {
				return u"other hosts must get no cache key"_q;
			}
			settings.baseUrl = u"https://openrouter.ai/api/v1"_q;
			const auto empty = OpenAi::Adapter(settings).prepare(Request());
			if (HasHeader(empty, "x-session-id")) {
				return u"an empty key must send nothing"_q;
			}
			return QString();
		} },
		{ u"openai_prepare_request_trailing_slash_no_key"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://localhost:8000/v1/"_q;
			const auto prepared = OpenAi::Adapter(settings).prepare(
				Request());
			const auto url = u"https://localhost:8000/v1/chat/completions"_q;
			if (prepared.url != url) {
				return u"url: got %1, expected %2"_q.arg(prepared.url, url);
			} else if (HasHeader(prepared, "Authorization")) {
				return u"empty key must omit Authorization"_q;
			}
			return QString();
		} },
		{ u"openai_serialize_request_media_kinds"_q, [] {
			auto request = Request();
			request.model = u"m"_q;
			request.messages.push_back(Message{
				Role::User,
				{
					TextBlock{ u"see"_q },
					MediaBlock{
						.kind = MediaKind::Image,
						.bytes = QByteArray("img"),
						.mimeType = u"image/png"_q,
					},
					MediaBlock{
						.kind = MediaKind::Audio,
						.bytes = QByteArray("snd"),
						.mimeType = u"audio/wav"_q,
					},
					MediaBlock{
						.kind = MediaKind::Video,
						.bytes = QByteArray("vid"),
						.mimeType = u"video/mp4"_q,
					},
					MediaBlock{
						.kind = MediaKind::File,
						.bytes = QByteArray("doc"),
						.mimeType = u"application/pdf"_q,
						.name = u"doc.pdf"_q,
					},
				},
			});
			const auto document = QJsonDocument::fromJson(
				OpenAi::SerializeRequest(request));
			const auto content = document.object().value("messages")
				.toArray()[0].toObject().value("content").toArray();
			if (content.size() != 5) {
				return u"expected 5 parts, got %1"_q.arg(content.size());
			}
			if (content[0].toObject().value("type") != u"text"_q) {
				return u"first part must be text"_q;
			}
			const auto kinds = std::vector<QString>{
				u"image_url"_q,
				u"input_audio"_q,
				u"video_url"_q,
				u"file"_q,
			};
			for (auto i = 0; i != int(kinds.size()); ++i) {
				const auto part = content[i + 1].toObject();
				if (part.value("type") != kinds[i]) {
					return u"part %1 kind: got %2, expected %3"_q
						.arg(i)
						.arg(part.value("type").toString(), kinds[i]);
				}
			}
			const auto audio = content[2].toObject().value("input_audio")
				.toObject();
			if (audio.value("format") != u"wav"_q) {
				return u"audio format must be wav"_q;
			}
			const auto file = content[4].toObject().value("file").toObject();
			if (file.value("filename") != u"doc.pdf"_q) {
				return u"file name must be kept"_q;
			}
			return QString();
		} },
		{ u"openai_serialize_request_tool_media_order"_q, [] {
			auto request = Request();
			request.model = u"m"_q;
			auto media = MediaBlock{
				.kind = MediaKind::Image,
				.bytes = QByteArray("img"),
				.mimeType = u"image/png"_q,
			};
			request.messages.push_back(Message{
				Role::User,
				{
					ToolResultBlock{ u"t1"_q, u"result"_q, false, { media } },
					ToolResultBlock{ u"t2"_q, u"plain"_q, false },
					TextBlock{ u"after"_q },
				},
			});
			const auto document = QJsonDocument::fromJson(
				OpenAi::SerializeRequest(request));
			const auto messages = document.object().value("messages")
				.toArray();
			if (messages.size() != 3) {
				return u"expected 3 messages, got %1"_q.arg(messages.size());
			}
			const auto first = messages[0].toObject();
			if (first.value("role") != u"tool"_q
				|| first.value("tool_call_id") != u"t1"_q) {
				return u"first message must be tool t1"_q;
			}
			const auto second = messages[1].toObject();
			if (second.value("role") != u"tool"_q
				|| second.value("tool_call_id") != u"t2"_q) {
				return u"second message must be tool t2"_q;
			}
			const auto third = messages[2].toObject();
			if (third.value("role") != u"user"_q) {
				return u"third message must be user"_q;
			}
			const auto parts = third.value("content").toArray();
			if (parts.size() != 3) {
				return u"expected 3 parts, got %1"_q.arg(parts.size());
			}
			if (parts[0].toObject().value("type") != u"text"_q
				|| !parts[0].toObject().value("text").toString().contains(
					u"t1"_q)) {
				return u"tool media must get a label part"_q;
			}
			if (parts[1].toObject().value("type") != u"image_url"_q) {
				return u"tool media part must follow its label"_q;
			}
			if (parts[2].toObject().value("type") != u"text"_q
				|| parts[2].toObject().value("text") != u"after"_q) {
				return u"the message blocks must follow the tool media"_q;
			}
			return QString();
		} },
		{ u"openai_serialize_request_pdf_engine"_q, [] {
			auto request = Request();
			request.model = u"m"_q;
			request.pdfEngine = u"pdf-text"_q;
			const auto document = QJsonDocument::fromJson(
				OpenAi::SerializeRequest(request));
			const auto object = document.object();
			const auto plugins = object.value("plugins").toArray();
			if (plugins.size() != 1) {
				return u"expected one plugin, got %1"_q.arg(plugins.size());
			}
			const auto plugin = plugins[0].toObject();
			if (plugin.value("id") != u"file-parser"_q) {
				return u"plugin id must be file-parser"_q;
			}
			const auto pdf = plugin.value("pdf").toObject();
			if (pdf.value("engine") != u"pdf-text"_q) {
				return u"pdf engine must be pdf-text"_q;
			}
			auto plain = request;
			plain.pdfEngine = QString();
			const auto other = QJsonDocument::fromJson(
				OpenAi::SerializeRequest(plain)).object();
			if (other.contains("plugins")) {
				return u"no plugins without pdfEngine"_q;
			}
			return QString();
		} },
		{ u"openai_models_request"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://openrouter.ai/api/v1/"_q;
			settings.apiKey = u"sk-test"_q;
			const auto request = OpenAi::ModelsRequest(settings);
			const auto url = u"https://openrouter.ai/api/v1/models"_q;
			if (request.url != url) {
				return u"url: got %1, expected %2"_q.arg(request.url, url);
			} else if (request.post) {
				return u"models must be a GET request"_q;
			} else if (request.body.size() != 0) {
				return u"models request must have no body"_q;
			}
			return CheckHeader(request, "Authorization", "Bearer sk-test");
		} },
		{ u"openai_endpoints_request"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://openrouter.ai/api/v1"_q;
			settings.model = u"z-ai/glm-5.3-flash"_q;
			const auto url = u"https://openrouter.ai/api/v1/models"
				u"/z-ai/glm-5.3-flash/endpoints"_q;
			const auto withKey = OpenAi::EndpointsRequest(settings);
			if (withKey.url != url) {
				return u"url: got %1, expected %2"_q.arg(withKey.url, url);
			} else if (withKey.post) {
				return u"endpoints must be a GET request"_q;
			} else if (HasHeader(withKey, "Authorization")) {
				return u"a missing key must omit Authorization"_q;
			}
			settings.apiKey = u"sk-test"_q;
			const auto keyed = OpenAi::EndpointsRequest(settings);
			if (keyed.url != url) {
				return u"url with key: got %1"_q.arg(keyed.url);
			}
			return CheckHeader(keyed, "Authorization", "Bearer sk-test");
		} },
		{ u"openai_endpoints_request_trailing_slash"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://openrouter.ai/api/v1/"_q;
			settings.model = u"z-ai/glm-5.3-flash"_q;
			const auto request = OpenAi::EndpointsRequest(settings);
			const auto url = u"https://openrouter.ai/api/v1/models"
				u"/z-ai/glm-5.3-flash/endpoints"_q;
			return (request.url == url)
				? QString()
				: u"url: got %1, expected %2"_q.arg(request.url, url);
		} },
		{ u"openai_key_request"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://openrouter.ai/api/v1"_q;
			const auto request = OpenAi::KeyRequest(settings);
			const auto url = u"https://openrouter.ai/api/v1/key"_q;
			if (request.url != url) {
				return u"url: got %1, expected %2"_q.arg(request.url, url);
			} else if (request.post) {
				return u"key must be a GET request"_q;
			} else if (HasHeader(request, "Authorization")) {
				return u"an empty key must omit Authorization"_q;
			}
			settings.apiKey = u"sk-test"_q;
			return CheckHeader(
				OpenAi::KeyRequest(settings),
				"Authorization",
				"Bearer sk-test");
		} },
		{ u"openai_parse_endpoints_fixture"_q, [] {
			const auto routes = OpenAi::ParseEndpoints(
				ReadFixture(u"openrouter-endpoints.json"_q));
			if (routes.size() != 3) {
				return u"routes: got %1, expected 3"_q.arg(routes.size());
			}
			struct Expected {
				QString provider;
				bool tools;
			};
			const auto expected = std::vector<Expected>{
				{ u"BaseTen"_q, true },
				{ u"InferenceNet"_q, false },
				{ u"Novita"_q, false },
			};
			auto index = 0;
			for (const auto &item : expected) {
				const auto &route = routes[index];
				if (route.provider != item.provider
					|| route.tools != item.tools) {
					return u"route %1: got %2 (tools %3), expected %4 (tools %5)"_q
						.arg(index)
						.arg(route.provider)
						.arg(route.tools ? u"yes"_q : u"no"_q)
						.arg(item.provider)
						.arg(item.tools ? u"yes"_q : u"no"_q);
				}
				++index;
			}
			return QString();
		} },
		{ u"openai_parse_endpoints_bad"_q, [] {
			if (!OpenAi::ParseEndpoints(QByteArray("not json")).empty()) {
				return u"malformed JSON must give an empty list"_q;
			} else if (!OpenAi::ParseEndpoints(
					QByteArray(R"({"data":{}})"_q)).empty()) {
				return u"a missing data must give an empty list"_q;
			} else if (!OpenAi::ParseEndpoints(QByteArray()).empty()) {
				return u"empty bytes must give an empty list"_q;
			}
			return QString();
		} },
	};
}

} // namespace

std::vector<Check> OpenAiChecks() {
	auto checks = FixtureChecks();
	for (const auto &check : ResponseChecks()) {
		checks.push_back(check);
	}
	for (const auto &check : OpenAiRequestChecks()) {
		checks.push_back(check);
	}
	return checks;
}

} // namespace Agent::Tests
