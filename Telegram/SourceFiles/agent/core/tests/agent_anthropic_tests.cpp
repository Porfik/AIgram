/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_anthropic.h"
#include "agent/core/agent_anthropic_stream.h"

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
	auto parser = Anthropic::Adapter(ProviderSettings()).parser();
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
		return (thinking->redacted)
			? u"redacted(%1)"_q.arg(QString::fromUtf8(thinking->signature))
			: u"thinking(%1, sig=%2)"_q.arg(
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
	result += u"stop(%1) usage(%2, %3, %4, %5) %6"_q
		.arg(int(response.stopReason))
		.arg(response.usage.inputTokens)
		.arg(response.usage.outputTokens)
		.arg(response.usage.cachedInputTokens)
		.arg(response.usage.cacheWriteTokens)
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
		int cached,
		int cacheWrite) {
	const auto ok = response.usage.inputTokens == input
		&& response.usage.outputTokens == output
		&& response.usage.cachedInputTokens == cached
		&& response.usage.cacheWriteTokens == cacheWrite;
	return ok
		? QString()
		: u"usage: got %1, %2, %3, %4, expected %5, %6, %7, %8"_q
			.arg(response.usage.inputTokens)
			.arg(response.usage.outputTokens)
			.arg(response.usage.cachedInputTokens)
			.arg(response.usage.cacheWriteTokens)
			.arg(input)
			.arg(output)
			.arg(cached)
			.arg(cacheWrite);
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

[[nodiscard]] QString CheckFixture(
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

[[nodiscard]] QString CheckBody(
		const HttpRequest &request,
		const QString &expected) {
	const auto got = QString::fromUtf8(request.body);
	return (got == expected)
		? QString()
		: u"body: got %1, expected %2"_q.arg(got, expected);
}

[[nodiscard]] QString CheckJsonField(
		const QJsonObject &object,
		const QString &key,
		const QString &expected) {
	const auto got = QString::fromUtf8(
		QJsonDocument(object.value(key).toObject())
			.toJson(QJsonDocument::Compact));
	return (got == expected)
		? QString()
		: u"%1: got %2, expected %3"_q.arg(key, got, expected);
}

[[nodiscard]] QString CheckJsonValue(
		const QJsonObject &object,
		const QString &key,
		const QJsonValue &expected) {
	const auto got = object.value(key);
	return (got == expected)
		? QString()
		: (u"%1: got %2, expected %3"_q
			.arg(key,
				QString::fromUtf8(QJsonDocument(
					QJsonObject{{ u"v"_q, got }}
				).toJson(QJsonDocument::Compact)),
				QString::fromUtf8(QJsonDocument(
					QJsonObject{{ u"v"_q, expected }}
				).toJson(QJsonDocument::Compact))));
}

[[nodiscard]] std::vector<Check> StreamChecks() {
	return {
		{ u"anthropic_text_stream"_q, [] {
			return CheckFixture(
				u"anthropic-text.sse"_q,
				[](const Response &response) {
					if (const auto failure = CheckNoError(response);
						!failure.isEmpty()) {
						return failure;
					} else if (const auto failure = CheckStopReason(
						response, StopReason::EndTurn); !failure.isEmpty()) {
						return failure;
					} else if (const auto failure = CheckUsage(
						response, 35, 12, 5, 20); !failure.isEmpty()) {
						return failure;
					}
					return CheckBlocks(response, {
						TextBlock{ u"Hello, world!"_q },
					});
				});
		} },
		{ u"anthropic_text_deltas"_q, [] {
			const auto bytes = ReadFixture(u"anthropic-text.sse"_q);
			auto parser = Anthropic::Adapter(ProviderSettings()).parser();
			parser->head(HttpHead{ 200, QByteArray() });
			parser->feed(bytes);
			const auto deltas = parser->takeDeltas();
			if (deltas.size() != 1) {
				return u"deltas: got %1, expected 1"_q.arg(deltas.size());
			}
			const auto *text = std::get_if<TextDelta>(&deltas[0]);
			return (text && text->text == u"Hello, world!"_q)
				? QString()
				: u"delta: got %1, expected one text delta"_q.arg(
					deltas.size());
		} },
		{ u"anthropic_tool_use_stream"_q, [] {
			const auto bytes = ReadFixture(u"anthropic-tool-use.sse"_q);
			auto parser = Anthropic::Adapter(ProviderSettings()).parser();
			parser->head(HttpHead{ 200, QByteArray() });
			parser->feed(bytes);
			const auto deltas = parser->takeDeltas();
			if (deltas.size() != 1) {
				return u"deltas: got %1, expected 1"_q.arg(deltas.size());
			}
			const auto *started = std::get_if<ToolUseStarted>(&deltas[0]);
			if (!started
				|| started->id != u"toolu_019Ud7R6nPQRiyK5KTfjDCoM"_q
				|| started->name != u"mcp__aigram__get_time"_q) {
				return u"delta: expected the tool use start"_q;
			}
			const auto response = parser->finish(QString());
			if (const auto failure = CheckNoError(response);
				!failure.isEmpty()) {
				return failure;
			} else if (const auto failure = CheckStopReason(
				response, StopReason::ToolUse); !failure.isEmpty()) {
				return failure;
			} else if (const auto failure = CheckUsage(
				response, 1318, 34, 0, 1316); !failure.isEmpty()) {
				return failure;
			}
			auto use = ToolUseBlock();
			use.id = u"toolu_019Ud7R6nPQRiyK5KTfjDCoM"_q;
			use.name = u"mcp__aigram__get_time"_q;
			use.input = QJsonObject();
			return CheckBlocks(response, { std::move(use) });
		} },
		{ u"anthropic_tool_args_stream"_q, [] {
			return CheckFixture(
				u"anthropic-tool-args.sse"_q,
				[](const Response &response) {
					if (const auto failure = CheckNoError(response);
						!failure.isEmpty()) {
						return failure;
					} else if (const auto failure = CheckStopReason(
						response, StopReason::ToolUse); !failure.isEmpty()) {
						return failure;
					} else if (const auto failure = CheckUsage(
						response, 3, 40, 0, 0); !failure.isEmpty()) {
						return failure;
					}
					auto first = ToolUseBlock();
					first.id = u"toolu_1"_q;
					first.name = u"read_history"_q;
					auto input = QJsonObject();
					input.insert(u"limit"_q, 10);
					input.insert(u"offset"_q, 2);
					first.input = input;
					auto second = ToolUseBlock();
					second.id = u"toolu_2"_q;
					second.name = u"broken"_q;
					second.inputError = u"Arguments are not a JSON object"_q;
					second.rawInput = "not a json";
					auto third = ToolUseBlock();
					third.id = u"toolu_3"_q;
					third.name = u"empty_args"_q;
					third.input = QJsonObject();
					return CheckBlocks(response, {
						std::move(first),
						std::move(second),
						std::move(third),
					});
				});
		} },
		{ u"anthropic_thinking_stream"_q, [] {
			return CheckFixture(
				u"anthropic-thinking.sse"_q,
				[](const Response &response) {
					if (const auto failure = CheckNoError(response);
						!failure.isEmpty()) {
						return failure;
					} else if (const auto failure = CheckUsage(
						response, 4, 30, 0, 0); !failure.isEmpty()) {
						return failure;
					}
					return CheckBlocks(response, {
						ThinkingBlock{
							u"Step one. Step two."_q,
							QByteArray("sig+/=="),
							false },
						TextBlock{ u"Answer."_q },
					});
				});
		} },
		{ u"anthropic_redacted_stream"_q, [] {
			return CheckFixture(
				u"anthropic-redacted.sse"_q,
				[](const Response &response) {
					if (const auto failure = CheckNoError(response);
						!failure.isEmpty()) {
						return failure;
					}
					return CheckBlocks(response, {
						ThinkingBlock{
							QString(),
							QByteArray("REDACTEDDATA"),
							true },
						TextBlock{ u"Done."_q },
					});
				});
		} },
		{ u"anthropic_stop_max_tokens"_q, [] {
			return CheckFixture(
				u"anthropic-stop-max-tokens.sse"_q,
				[](const Response &response) {
					if (const auto failure = CheckNoError(response);
						!failure.isEmpty()) {
						return failure;
					}
					return CheckBlocks(response, { TextBlock{ u"Hal"_q } });
				});
		} },
		{ u"anthropic_stop_refusal"_q, [] {
			return CheckFixture(
				u"anthropic-stop-refusal.sse"_q,
				[](const Response &response) {
					if (const auto failure = CheckNoError(response);
						!failure.isEmpty()) {
						return failure;
					}
					return CheckBlocks(response, {});
				});
		} },
		{ u"anthropic_stream_error"_q, [] {
			return CheckFixture(
				u"anthropic-stream-error.sse"_q,
				[](const Response &response) {
					if (!response.error) {
						return u"expected an error"_q;
					}
					const auto &error = *response.error;
					return (error.kind == ErrorKind::Overloaded
						&& error.httpStatus == 0
						&& error.text == u"Overloaded"_q)
						? QString()
						: u"error: got kind %1 status %2 text %3"_q
							.arg(int(error.kind))
							.arg(error.httpStatus)
							.arg(error.text);
				});
		} },
		{ u"anthropic_no_stop_reason"_q, [] {
			const auto bytes = ReadFixture(u"anthropic-no-stop.sse"_q);
			const auto response = ParseResponse(bytes, 0);
			if (!response.error) {
				return u"expected an error"_q;
			}
			return (response.error->kind == ErrorKind::BadResponse
				&& response.error->text
					== u"Stream ended without a stop reason"_q)
				? QString()
				: u"error: got kind %1 text %2"_q
					.arg(int(response.error->kind))
					.arg(response.error->text);
		} },
		{ u"anthropic_network_error"_q, [] {
			auto parser = Anthropic::Adapter(ProviderSettings()).parser();
			parser->head(HttpHead{ 200, QByteArray() });
			parser->feed("event: message_start\ndata: {}\n\n");
			const auto response = parser->finish(u"connection lost"_q);
			if (!response.error) {
				return u"expected an error"_q;
			}
			return (response.error->kind == ErrorKind::Network
				&& response.error->text == u"connection lost"_q)
				? QString()
				: u"error: got kind %1 text %2"_q
					.arg(int(response.error->kind))
					.arg(response.error->text);
		} },
	};
}

[[nodiscard]] std::vector<Check> UnitChecks() {
	return {
		{ u"anthropic_parse_usage"_q, [] {
			const auto usage = Anthropic::ParseUsage(QJsonObject{
				{ u"input_tokens"_q, 100 },
				{ u"output_tokens"_q, 40 },
				{ u"cache_creation_input_tokens"_q, 10 },
				{ u"cache_read_input_tokens"_q, 20 },
			});
			const auto ok = (usage.inputTokens == 130)
				&& (usage.outputTokens == 40)
				&& (usage.cachedInputTokens == 20)
				&& (usage.cacheWriteTokens == 10)
				&& !usage.hasCost;
			return ok ? QString() : u"usage fields are wrong"_q;
		} },
		{ u"anthropic_parse_usage_missing"_q, [] {
			const auto usage = Anthropic::ParseUsage(QJsonObject());
			const auto ok = (usage.inputTokens == 0)
				&& (usage.outputTokens == 0)
				&& (usage.cachedInputTokens == 0)
				&& (usage.cacheWriteTokens == 0);
			return ok ? QString() : u"missing fields must be zero"_q;
		} },
		{ u"anthropic_parse_stop_reasons"_q, [] {
			const auto ok = (Anthropic::ParseStopReason(
				u"end_turn"_q) == StopReason::EndTurn)
				&& (Anthropic::ParseStopReason(
					u"stop_sequence"_q) == StopReason::EndTurn)
				&& (Anthropic::ParseStopReason(
					u"pause_turn"_q) == StopReason::EndTurn)
				&& (Anthropic::ParseStopReason(
					u"tool_use"_q) == StopReason::ToolUse)
				&& (Anthropic::ParseStopReason(
					u"max_tokens"_q) == StopReason::MaxTokens)
				&& (Anthropic::ParseStopReason(
					u"model_context_window_exceeded"_q)
						== StopReason::MaxTokens)
				&& (Anthropic::ParseStopReason(
					u"refusal"_q) == StopReason::Refusal)
				&& (Anthropic::ParseStopReason(
					QString()) == StopReason::EndTurn);
			return ok ? QString() : u"stop reasons are wrong"_q;
		} },
		{ u"anthropic_error_kind_for_type"_q, [] {
			const auto ok = (Anthropic::ErrorKindForType(
				u"authentication_error"_q, 0) == ErrorKind::Auth)
				&& (Anthropic::ErrorKindForType(
					u"permission_error"_q, 0) == ErrorKind::Auth)
				&& (Anthropic::ErrorKindForType(
					u"billing_error"_q, 0) == ErrorKind::Auth)
				&& (Anthropic::ErrorKindForType(
					u"rate_limit_error"_q, 0) == ErrorKind::RateLimited)
				&& (Anthropic::ErrorKindForType(
					u"overloaded_error"_q, 0) == ErrorKind::Overloaded)
				&& (Anthropic::ErrorKindForType(
					u"api_error"_q, 0) == ErrorKind::Overloaded)
				&& (Anthropic::ErrorKindForType(
					u"timeout_error"_q, 0) == ErrorKind::Overloaded)
				&& (Anthropic::ErrorKindForType(
					u"invalid_request_error"_q, 0)
						== ErrorKind::BadRequest)
				&& (Anthropic::ErrorKindForType(
					u"not_found_error"_q, 0) == ErrorKind::BadRequest)
				&& (Anthropic::ErrorKindForType(
					u"request_too_large"_q, 0) == ErrorKind::BadRequest)
				&& (Anthropic::ErrorKindForType(
					u"unknown_error"_q, 401) == ErrorKind::Auth)
				&& (Anthropic::ErrorKindForType(
					u"unknown_error"_q, 429) == ErrorKind::RateLimited)
				&& (Anthropic::ErrorKindForType(
					u"unknown_error"_q, 500) == ErrorKind::Overloaded)
				&& (Anthropic::ErrorKindForType(
					u"unknown_error"_q, 400) == ErrorKind::BadRequest)
				&& (Anthropic::ErrorKindForType(
					u"unknown_error"_q, 0) == ErrorKind::BadResponse);
			return ok ? QString() : u"error kinds are wrong"_q;
		} },
		{ u"anthropic_parse_error_object"_q, [] {
			const auto error = Anthropic::ParseErrorObject(QJsonObject{
				{ u"type"_q, u"overloaded_error"_q },
				{ u"message"_q, u"Overloaded"_q },
			}, 0);
			const auto ok = (error.kind == ErrorKind::Overloaded)
				&& (error.httpStatus == 0)
				&& (error.text == u"Overloaded"_q);
			return ok ? QString() : u"error object fields are wrong"_q;
		} },
		{ u"anthropic_parse_error_body"_q, [] {
			const auto body = QByteArray(
				R"({"type":"error","error":{"type":"rate_limit_error","message":"Rate limited"},"request_id":"req_1"})");
			const auto error = Anthropic::ParseError(
				HttpHead{ 429, QByteArray("17") },
				body);
			const auto ok = (error.kind == ErrorKind::RateLimited)
				&& (error.httpStatus == 429)
				&& (error.retryAfter == 17000)
				&& (error.text == u"Rate limited"_q);
			return ok ? QString() : u"error fields are wrong"_q;
		} },
		{ u"anthropic_assembler_lifecycle"_q, [] {
			auto assembler = Anthropic::StreamAssembler();
			auto failure = QString();
			if (assembler.started()
				|| assembler.stopped()
				|| assembler.error()) {
				failure = u"fresh assembler must be empty"_q;
			} else if (const auto response = assembler.finish();
				!response.error
				|| response.error->kind != ErrorKind::BadResponse) {
				failure = u"empty stream must give a bad response"_q;
			}
			return failure;
		} },
	};
}

[[nodiscard]] QString CheckRequestJson(
		const Request &request,
		const QString &expected) {
	const auto got = QString::fromUtf8(QJsonDocument(
		Anthropic::RequestObject(request)).toJson(QJsonDocument::Compact));
	return (got == expected)
		? QString()
		: u"body: got %1, expected %2"_q.arg(got, expected);
}

[[nodiscard]] QString CheckRequestMessages(
		const Request &request,
		const QString &expected) {
	const auto got = QString::fromUtf8(QJsonDocument(
		Anthropic::RequestObject(request).value(u"messages"_q).toArray()
	).toJson(QJsonDocument::Compact));
	return (got == expected)
		? QString()
		: u"messages: got %1, expected %2"_q.arg(got, expected);
}

[[nodiscard]] std::vector<Check> AdapterChecks() {
	return {
		{ u"anthropic_prepare_request"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://api.anthropic.com/v1"_q;
			settings.apiKey = u"key-one"_q;
			const auto adapter = Anthropic::Adapter(settings);
			auto request = Request();
			request.model = u"claude-opus-5-5"_q;
			const auto prepared = adapter.prepare(request);
			if (prepared.url
				!= u"https://api.anthropic.com/v1/messages"_q) {
				return u"url: got %1"_q.arg(prepared.url);
			} else if (!prepared.post) {
				return u"prepare must be a POST request"_q;
			} else if (const auto failure = CheckHeader(
				prepared,
				u"x-api-key"_q,
				u"key-one"_q); !failure.isEmpty()) {
				return failure;
			} else if (const auto failure = CheckHeader(
				prepared,
				u"anthropic-version"_q,
				u"2023-06-01"_q); !failure.isEmpty()) {
				return failure;
			} else if (const auto failure = CheckHeader(
				prepared,
				u"content-type"_q,
				u"application/json"_q); !failure.isEmpty()) {
				return failure;
			} else if (const auto failure = CheckHeader(
				prepared,
				u"accept"_q,
				u"text/event-stream"_q); !failure.isEmpty()) {
				return failure;
			}
			return CheckHeader(
				prepared,
				u"anthropic-beta"_q,
				u"thinking-binding-controls-2026-08-01,"
				u"server-side-fallback-2026-07-01"_q);
		} },
		{ u"anthropic_prepare_no_beta"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://api.anthropic.com/v1"_q;
			const auto adapter = Anthropic::Adapter(settings);
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			const auto prepared = adapter.prepare(request);
			return HasHeader(prepared, u"anthropic-beta"_q)
				? u"unexpected anthropic-beta header"_q
				: QString();
		} },
		{ u"anthropic_prepare_profile_model_effort"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = u"https://api.anthropic.com/v1"_q;
			settings.model = u"claude-opus-5-5"_q;
			settings.effort = u"high"_q;
			const auto adapter = Anthropic::Adapter(settings);
			const auto prepared = adapter.prepare(Request());
			const auto object = QJsonDocument::fromJson(
				prepared.body).object();
			const auto model = object.value(u"model"_q).toString();
			const auto effort = object
				.value(u"output_config"_q).toObject()
				.value(u"effort"_q).toString();
			if (model != u"claude-opus-5-5"_q) {
				return u"model: got %1, expected claude-opus-5-5"_q
					.arg(model);
			} else if (effort != u"high"_q) {
				return u"effort: got %1, expected high"_q.arg(effort);
			} else if (!HasHeader(prepared, u"anthropic-beta"_q)) {
				return u"the profile model must get a beta header"_q;
			}
			return QString();
		} },
	};
}

[[nodiscard]] std::vector<Check> AnthropicRequestChecks() {
	return {
		{ u"anthropic_request_basic"_q, [] {
			auto request = Request();
			request.model = u"claude-opus-5-5"_q;
			return CheckRequestJson(request,
				u"{\"cache_control\":{\"type\":\"ephemeral\"},"
				u"\"fallbacks\":\"default\",\"max_tokens\":32000,"
				u"\"messages\":[],\"model\":\"claude-opus-5-5\","
				u"\"stream\":true,\"thinking\":{\"block_binding\":"
				u"{\"prefix_mismatch_behavior\":\"drop_block\"},"
				u"\"display\":\"summarized\",\"type\":\"adaptive\"}}"_q);
		} },
		{ u"anthropic_request_no_thinking"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			return CheckRequestJson(request,
				u"{\"cache_control\":{\"type\":\"ephemeral\"},"
				u"\"max_tokens\":32000,\"messages\":[],"
				u"\"model\":\"claude-haiku-4-5-20251001\","
				u"\"stream\":true}"_q);
		} },
		{ u"anthropic_request_max_tokens"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.maxOutputTokens = 1000;
			return CheckRequestJson(request,
				u"{\"cache_control\":{\"type\":\"ephemeral\"},"
				u"\"max_tokens\":1000,\"messages\":[],"
				u"\"model\":\"claude-haiku-4-5-20251001\","
				u"\"stream\":true}"_q);
		} },
		{ u"anthropic_request_effort"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.effort = u"high"_q;
			return CheckRequestJson(request,
				u"{\"cache_control\":{\"type\":\"ephemeral\"},"
				u"\"max_tokens\":32000,\"messages\":[],"
				u"\"model\":\"claude-haiku-4-5-20251001\","
				u"\"output_config\":{\"effort\":\"high\"},"
				u"\"stream\":true}"_q);
		} },
		{ u"anthropic_request_tools"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			auto tool = ToolSpec();
			tool.name = u"read"_q;
			tool.description = u"Read a chat"_q;
			tool.inputSchema = QJsonObject{{ u"type"_q, u"object"_q }};
			request.tools = { tool };
			return CheckRequestJson(request,
				u"{\"cache_control\":{\"type\":\"ephemeral\"},"
				u"\"max_tokens\":32000,\"messages\":[],"
				u"\"model\":\"claude-haiku-4-5-20251001\","
				u"\"stream\":true,\"tools\":[{\"description\":"
				u"\"Read a chat\",\"input_schema\":{\"type\":\"object\"},"
				u"\"name\":\"read\"}]}"_q);
		} },
		{ u"anthropic_request_system"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.system = u"Be nice."_q;
			return CheckRequestJson(request,
				u"{\"cache_control\":{\"type\":\"ephemeral\"},"
				u"\"max_tokens\":32000,\"messages\":[],"
				u"\"model\":\"claude-haiku-4-5-20251001\","
				u"\"stream\":true,\"system\":[{\"cache_control\":"
				u"{\"type\":\"ephemeral\"},\"text\":\"Be nice.\","
				u"\"type\":\"text\"}]}"_q);
		} },
		{ u"anthropic_request_merge_roles"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.messages = {
				Message{ Role::User, { TextBlock{ u"First"_q } } },
				Message{ Role::User, { TextBlock{ u"Second"_q } } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"text\":\"First\",\"type\":\"text\"},"
				u"{\"text\":\"Second\",\"type\":\"text\"}],"
				u"\"role\":\"user\"}]"_q);
		} },
		{ u"anthropic_request_tool_result_first"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			auto result = ToolResultBlock();
			result.toolUseId = u"toolu_1"_q;
			result.content = u"Result"_q;
			result.error = true;
			auto media = MediaBlock();
			media.kind = MediaKind::Image;
			media.bytes = QByteArray("AB");
			media.mimeType = u"image/jpeg"_q;
			result.media = { media };
			request.messages = {
				Message{ Role::User, {
					TextBlock{ u"After"_q },
					result } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"content\":[{\"text\":\"Result\","
				u"\"type\":\"text\"},{\"source\":{\"data\":\"QUI=\","
				u"\"media_type\":\"image/jpeg\",\"type\":\"base64\"},"
				u"\"type\":\"image\"}],\"is_error\":true,"
				u"\"tool_use_id\":\"toolu_1\",\"type\":\"tool_result\"},"
				u"{\"text\":\"After\",\"type\":\"text\"}],"
				u"\"role\":\"user\"}]"_q);
		} },
		{ u"anthropic_request_media"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			auto image = MediaBlock();
			image.kind = MediaKind::Image;
			image.bytes = QByteArray("AB");
			image.mimeType = u"image/jpeg"_q;
			auto pdf = MediaBlock();
			pdf.kind = MediaKind::File;
			pdf.bytes = QByteArray("CD");
			pdf.mimeType = u"application/pdf"_q;
			auto audio = MediaBlock();
			audio.kind = MediaKind::Audio;
			audio.bytes = QByteArray("EF");
			audio.mimeType = u"audio/wav"_q;
			auto empty = MediaBlock();
			empty.kind = MediaKind::Image;
			empty.mimeType = u"image/jpeg"_q;
			request.messages = {
				Message{ Role::User, { image, pdf, audio, empty } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"source\":{\"data\":\"QUI=\","
				u"\"media_type\":\"image/jpeg\",\"type\":\"base64\"},"
				u"\"type\":\"image\"},{\"source\":{\"data\":\"Q0Q=\","
				u"\"media_type\":\"application/pdf\",\"type\":\"base64\"},"
				u"\"type\":\"document\"}],\"role\":\"user\"}]"_q);
		} },
		{ u"anthropic_request_thinking_roundtrip"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.messages = {
				Message{ Role::Assistant, {
					ThinkingBlock{ u"th"_q, QByteArray("sig") },
					ThinkingBlock{ QString(), QByteArray("DATA"), true },
					TextBlock{ u"Hey"_q } } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"signature\":\"sig\","
				u"\"thinking\":\"th\",\"type\":\"thinking\"},"
				u"{\"data\":\"DATA\",\"type\":\"redacted_thinking\"},"
				u"{\"text\":\"Hey\",\"type\":\"text\"}],"
				u"\"role\":\"assistant\"}]"_q);
		} },
		{ u"anthropic_request_thinking_dropped"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.messages = {
				Message{ Role::Assistant, {
					ThinkingBlock{ u"th"_q, QByteArray() } } },
			};
			return CheckRequestMessages(request, u"[]"_q);
		} },
		{ u"anthropic_request_openai_thinking_dropped"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.messages = {
				Message{ Role::Assistant, {
					ThinkingBlock{
						u"other"_q,
						QByteArray("{\"openai\":{\"model\":\"gpt-5\"}}") },
					ThinkingBlock{ u"th"_q, QByteArray("sig") },
					TextBlock{ u"Hey"_q } } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"signature\":\"sig\","
				u"\"thinking\":\"th\",\"type\":\"thinking\"},"
				u"{\"text\":\"Hey\",\"type\":\"text\"}],"
				u"\"role\":\"assistant\"}]"_q);
		} },
		{ u"anthropic_request_tool_input_error"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			auto use = ToolUseBlock();
			use.id = u"call_0"_q;
			use.name = u"read"_q;
			use.inputError = u"Arguments are not a JSON object"_q;
			use.rawInput = "{oops";
			request.messages = {
				Message{ Role::Assistant, { use } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"id\":\"call_0\",\"input\":{},"
				u"\"name\":\"read\",\"type\":\"tool_use\"}],"
				u"\"role\":\"assistant\"}]"_q);
		} },
		{ u"anthropic_request_tool_id_remap"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			auto use1 = ToolUseBlock();
			use1.id = u"call_0"_q;
			use1.name = u"read"_q;
			use1.input = QJsonObject{{ u"x"_q, 1 }};
			auto result1 = ToolResultBlock();
			result1.toolUseId = u"call_0"_q;
			result1.content = u"R1"_q;
			auto use2 = ToolUseBlock();
			use2.id = u"call_0"_q;
			use2.name = u"write"_q;
			auto result2 = ToolResultBlock();
			result2.toolUseId = u"call_0"_q;
			result2.content = u"R2"_q;
			request.messages = {
				Message{ Role::User, { TextBlock{ u"Go"_q } } },
				Message{ Role::Assistant, { use1 } },
				Message{ Role::User, { result1 } },
				Message{ Role::Assistant, { use2 } },
				Message{ Role::User, { result2 } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"text\":\"Go\",\"type\":\"text\"}],"
				u"\"role\":\"user\"},{\"content\":[{\"id\":\"call_0\","
				u"\"input\":{\"x\":1},\"name\":\"read\","
				u"\"type\":\"tool_use\"}],\"role\":\"assistant\"},"
				u"{\"content\":[{\"content\":[{\"text\":\"R1\","
				u"\"type\":\"text\"}],\"tool_use_id\":\"call_0\","
				u"\"type\":\"tool_result\"}],\"role\":\"user\"},"
				u"{\"content\":[{\"id\":\"toolu_aigram_3_0\","
				u"\"input\":{},\"name\":\"write\","
				u"\"type\":\"tool_use\"}],\"role\":\"assistant\"},"
				u"{\"content\":[{\"content\":[{\"text\":\"R2\","
				u"\"type\":\"text\"}],\"tool_use_id\":"
				u"\"toolu_aigram_3_0\",\"type\":\"tool_result\"}],"
				u"\"role\":\"user\"}]"_q);
		} },
		{ u"anthropic_request_hidden_text"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.messages = {
				Message{ Role::User, {
					TextBlock{ u"hint"_q, true },
					TextBlock{ u"Ask me"_q } } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"text\":\"hint\",\"type\":\"text\"},"
				u"{\"text\":\"Ask me\",\"type\":\"text\"}],"
				u"\"role\":\"user\"}]"_q);
		} },
		{ u"anthropic_request_skip_empty"_q, [] {
			auto request = Request();
			request.model = u"claude-haiku-4-5-20251001"_q;
			request.messages = {
				Message{ Role::User, { TextBlock{ u""_q } } },
				Message{ Role::Assistant, { TextBlock{ u"Hi"_q } } },
			};
			return CheckRequestMessages(request,
				u"[{\"content\":[{\"text\":\"Hi\",\"type\":\"text\"}],"
				u"\"role\":\"assistant\"}]"_q);
		} },
	};
}

[[nodiscard]] std::vector<Check> ModelsChecks() {
	using namespace Agent::Anthropic;

	return {
		{ u"anthropic_parse_models"_q, [] {
			const auto models = ParseModels(
				ReadFixture(u"anthropic-models.json"_q));
			if (models.size() != 2) {
				return u"expected 2 models, got %1"_q.arg(models.size());
			}
			const auto &opus = models[0];
			if (opus.id != u"claude-opus-5-5"_q
				|| opus.name != u"Claude Opus 5.5"_q
				|| opus.contextWindow != 1'000'000
				|| opus.maxOutput != 128'000
				|| !opus.vision
				|| !opus.pdf
				|| opus.effortLevels.size() != 5
				|| !opus.prices
				|| opus.prices->input != 4.
				|| opus.prices->cached != 0.2
				|| opus.prices->cacheWrite != 5.) {
				return u"bad opus entry"_q;
			}
			const auto &haiku = models[1];
			if (haiku.pdf
				|| !haiku.effortLevels.empty()
				|| !haiku.prices
				|| haiku.prices->input != 1.) {
				return u"bad haiku entry"_q;
			}
			return ParseModels("not json").empty()
				? QString()
				: u"garbage must give no models"_q;
		} },
		{ u"anthropic_known_prices"_q, [] {
			const auto exact = KnownPrices(u"claude-sonnet-5-5"_q);
			const auto dated = KnownPrices(u"claude-haiku-4-5-20251001"_q);
			if (!exact || exact->input != 2. || exact->output != 10.) {
				return u"sonnet 5.5 prices"_q;
			} else if (!dated || dated->cacheWrite != 1.25) {
				return u"dated haiku 4.5 prices"_q;
			} else if (KnownPrices(u"claude-unknown-9"_q)) {
				return u"unknown model must have no prices"_q;
			}
			const auto fable = KnownPrices(u"claude-fable-5-1"_q);
			const auto fable5 = KnownPrices(u"claude-fable-5"_q);
			return (fable && fable->cached == 0.25 && fable5
				&& fable5->cached == 1.)
				? QString()
				: u"fable prices must not mix"_q;
		} },
		{ u"anthropic_model_features"_q, [] {
			if (!SupportsAdaptiveThinking(u"claude-opus-5-5"_q)
				|| !SupportsAdaptiveThinking(u"claude-new-model"_q)
				|| SupportsAdaptiveThinking(u"claude-haiku-4-5-20251001"_q)
				|| SupportsAdaptiveThinking(u"claude-3-haiku-20240307"_q)) {
				return u"adaptive thinking support"_q;
			} else if (!SupportsFallbacks(u"claude-opus-5-5"_q)
				|| !SupportsFallbacks(u"claude-sonnet-5-5"_q)
				|| SupportsFallbacks(u"claude-haiku-5-5"_q)
				|| SupportsFallbacks(u"claude-sonnet-5"_q)) {
				return u"fallbacks support"_q;
			}
			return (EffortLevels().size() == 5)
				? QString()
				: u"effort levels"_q;
		} },
		{ u"anthropic_request_empty_tool_result"_q, [] {
			auto use = ToolUseBlock();
			use.id = u"toolu_1"_q;
			use.name = u"tool"_q;
			auto request = Request();
			request.model = u"claude-opus-5-5"_q;
			request.messages = {
				Message{ Role::User, { TextBlock{ u"go"_q } } },
				Message{ Role::Assistant, { use } },
				Message{ Role::User, {
					ToolResultBlock{ u"toolu_1"_q, QString(), false },
				} },
			};
			const auto messages = RequestObject(request)
				.value(u"messages"_q).toArray();
			const auto result = messages[2].toObject()
				.value(u"content"_q).toArray()[0].toObject();
			return (result.value(u"type"_q).toString() == u"tool_result"_q
				&& !result.contains(u"content"_q))
				? QString()
				: u"an empty result must have no content"_q;
		} },
	};
}

} // namespace

std::vector<Check> AnthropicChecks() {
	auto checks = StreamChecks();
	for (auto &check : ModelsChecks()) {
		checks.push_back(std::move(check));
	}
	for (auto &check : UnitChecks()) {
		checks.push_back(std::move(check));
	}
	for (auto &check : AdapterChecks()) {
		checks.push_back(std::move(check));
	}
	for (auto &check : AnthropicRequestChecks()) {
		checks.push_back(std::move(check));
	}
	return checks;
}

} // namespace Agent::Tests
