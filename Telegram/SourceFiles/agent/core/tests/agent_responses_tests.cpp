/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_openai_responses.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <range/v3/algorithm/all_of.hpp>

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

[[nodiscard]] QByteArray Sse(std::initializer_list<const char*> events) {
	auto result = QByteArray();
	for (const auto event : events) {
		const auto object = QJsonDocument::fromJson(event).object();
		result += "event: " + object.value(u"type"_q).toString().toUtf8()
			+ "\ndata: " + QByteArray(event) + "\n\n";
	}
	return result;
}

struct Parsed {
	Response response;
	std::vector<StreamDelta> deltas;
};

[[nodiscard]] Parsed Parse(
		const QByteArray &bytes,
		int mode = 0,
		int status = 200) {
	auto parser = OpenAi::ResponsesParser(u"gpt-5"_q, u"origin"_q);
	parser->head(HttpHead{ status, QByteArray() });
	auto result = Parsed();
	for (const auto &chunk : SplitChunks(bytes, mode)) {
		parser->feed(chunk);
		for (auto &delta : parser->takeDeltas()) {
			result.deltas.push_back(std::move(delta));
		}
	}
	result.response = parser->finish(QString());
	return result;
}

[[nodiscard]] QString DescribeBlock(const Block &block) {
	if (const auto text = std::get_if<TextBlock>(&block)) {
		return u"text(%1)"_q.arg(text->text);
	} else if (const auto thinking = std::get_if<ThinkingBlock>(&block)) {
		return u"thinking(%1)"_q.arg(thinking->text);
	} else if (const auto use = std::get_if<ToolUseBlock>(&block)) {
		return u"use(%1, %2, %3%4)"_q
			.arg(use->id, use->name)
			.arg(QString::fromUtf8(
				QJsonDocument(use->input).toJson(QJsonDocument::Compact)))
			.arg(use->inputError.isEmpty()
				? QString()
				: (u", raw="_q + QString::fromUtf8(use->rawInput)));
	}
	return u"other"_q;
}

[[nodiscard]] QString DescribeBlocks(const Response &response) {
	auto parts = QStringList();
	for (const auto &block : response.message.blocks) {
		parts.push_back(DescribeBlock(block));
	}
	return parts.join(u" "_q);
}

[[nodiscard]] QString DescribeDeltas(const std::vector<StreamDelta> &list) {
	auto parts = QStringList();
	for (const auto &delta : list) {
		if (const auto text = std::get_if<TextDelta>(&delta)) {
			parts.push_back(u"T[%1]"_q.arg(text->text));
		} else if (const auto thinking = std::get_if<ThinkingDelta>(&delta)) {
			parts.push_back(u"R[%1]"_q.arg(thinking->text));
		} else if (const auto use = std::get_if<ToolUseStarted>(&delta)) {
			parts.push_back(u"U[%1 %2]"_q.arg(use->id, use->name));
		}
	}
	return parts.join(u" "_q);
}

[[nodiscard]] QString Expect(const QString &got, const QString &expected) {
	return (got == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(got, expected);
}

[[nodiscard]] QString Compact(const QJsonValue &value) {
	return value.isArray()
		? QString::fromUtf8(QJsonDocument(value.toArray()).toJson(
			QJsonDocument::Compact))
		: value.isObject()
		? QString::fromUtf8(QJsonDocument(value.toObject()).toJson(
			QJsonDocument::Compact))
		: value.toVariant().toString();
}

[[nodiscard]] Message UserText(const QString &text) {
	return Message{ Role::User, { TextBlock{ text } } };
}

[[nodiscard]] QJsonArray InputOf(const Request &request, bool plan = false) {
	return OpenAi::ResponsesRequestObject(
		request,
		OpenAi::ResponsesOptions{ .origin = u"origin"_q, .plan = plan }
	).value(u"input"_q).toArray();
}

[[nodiscard]] QByteArray Signature(
		const QString &model = u"gpt-5"_q,
		const QString &origin = u"origin"_q) {
	return OpenAi::ReasoningSignature(model, origin, QJsonObject{
		{ u"id"_q, u"rs_1"_q },
		{ u"type"_q, u"reasoning"_q },
		{ u"summary"_q, QJsonArray() },
		{ u"encrypted_content"_q, u"blob"_q },
		{ u"status"_q, u"completed"_q },
	});
}

[[nodiscard]] int CountType(const QJsonArray &input, const QString &type) {
	auto result = 0;
	for (const auto &value : input) {
		result += (value.toObject().value(u"type"_q).toString() == type)
			? 1
			: 0;
	}
	return result;
}

[[nodiscard]] QString ErrorText(const std::optional<Error> &error) {
	return error
		? u"%1/%2/%3/%4"_q
			.arg(ErrorKindName(error->kind))
			.arg(error->httpStatus)
			.arg(error->code)
			.arg(error->text)
		: u"none"_q;
}

[[nodiscard]] QString DescribeModels(const std::vector<OpenAi::ModelInfo> &list) {
	auto parts = QStringList();
	for (const auto &model : list) {
		parts.push_back(u"%1|%2|%3|%4%5|%6|%7"_q
			.arg(model.id, model.name)
			.arg(model.contextWindow)
			.arg((model.vision && *model.vision) ? u"v"_q : QString())
			.arg((model.files && *model.files) ? u"f"_q : QString())
			.arg(QStringList(
				model.effortLevels.begin(),
				model.effortLevels.end()).join(','))
			.arg(model.priceInput));
	}
	return parts.join(u" ; "_q);
}

std::vector<Check> RequestBodyChecks() {
	return {
		{ u"responses_request_basic"_q, [] {
			const auto object = OpenAi::ResponsesRequestObject(
				Request{
					.model = u"gpt-5"_q,
					.messages = { UserText(u"hi"_q) },
				},
				OpenAi::ResponsesOptions{ .origin = u"origin"_q });
			auto keys = object.keys();
			keys.sort();
			if (const auto error = Expect(
					keys.join(','),
					u"include,input,model,reasoning,store,stream"_q);
				!error.isEmpty()) {
				return error;
			}
			return Expect(
				Compact(object),
				u"{\"include\":[\"reasoning.encrypted_content\"],"
				u"\"input\":[{\"content\":[{\"text\":\"hi\","
				u"\"type\":\"input_text\"}],\"role\":\"user\"}],"
				u"\"model\":\"gpt-5\",\"reasoning\":{\"summary\":\"auto\"},"
				u"\"store\":false,\"stream\":true}"_q);
		} },
		{ u"responses_request_fields"_q, [] {
			const auto request = Request{
				.model = u"gpt-6.1-sol"_q,
				.system = u"be nice"_q,
				.messages = { UserText(u"hi"_q) },
				.tools = { ToolSpec{
					u"get_time"_q,
					u"Returns the time."_q,
					QJsonObject{ { u"type"_q, u"object"_q } },
				} },
				.maxOutputTokens = 100,
				.cacheKey = u"key-1"_q,
				.effort = u"high"_q,
			};
			const auto object = OpenAi::ResponsesRequestObject(
				request,
				OpenAi::ResponsesOptions{});
			if (const auto error = Expect(
					Compact(object.value(u"tools"_q)),
					u"[{\"description\":\"Returns the time.\","
					u"\"name\":\"get_time\",\"parameters\":{\"type\":"
					u"\"object\"},\"strict\":false,\"type\":\"function\"}]"_q);
				!error.isEmpty()) {
				return error;
			}
			const auto fields = u"%1|%2|%3|%4|%5|%6"_q
				.arg(object.value(u"instructions"_q).toString())
				.arg(object.value(u"tool_choice"_q).toString())
				.arg(object.value(u"parallel_tool_calls"_q).toBool()
					? u"true"_q
					: u"false"_q)
				.arg(Compact(object.value(u"reasoning"_q)))
				.arg(object.value(u"prompt_cache_key"_q).toString())
				.arg(object.value(u"max_output_tokens"_q).toInt());
			return Expect(
				fields,
				u"be nice|auto|true|{\"effort\":\"high\",\"summary\":"
				u"\"auto\"}|key-1|100"_q);
		} },
		{ u"responses_request_plan"_q, [] {
			const auto object = OpenAi::ResponsesRequestObject(
				Request{
					.model = u"gpt-6.1-sol"_q,
					.messages = { UserText(u"hi"_q) },
					.maxOutputTokens = 100,
				},
				OpenAi::ResponsesOptions{ .plan = true });
			return object.contains(u"max_output_tokens"_q)
				? u"the plan must not get max_output_tokens"_q
				: QString();
		} },
		{ u"responses_request_no_reasoning"_q, [] {
			for (const auto &model : {
					u"gpt-4o"_q,
					u"gpt-4.1"_q,
					u"chatgpt-4o-latest"_q }) {
				const auto object = OpenAi::ResponsesRequestObject(
					Request{
						.model = model,
						.messages = { UserText(u"hi"_q) },
						.effort = u"high"_q,
					},
					OpenAi::ResponsesOptions{});
				if (object.contains(u"reasoning"_q)
					|| object.contains(u"include"_q)) {
					return model + u" must not get reasoning"_q;
				}
			}
			return QString();
		} },
		{ u"responses_request_user_media"_q, [] {
			auto message = Message{ Role::User };
			message.blocks.push_back(TextBlock{ u"look"_q });
			message.blocks.push_back(TextBlock{ u"[hint]"_q, true });
			message.blocks.push_back(MediaBlock{
				.kind = MediaKind::Image,
				.bytes = "AB",
				.mimeType = u"image/png"_q,
				.name = u"a.png"_q,
			});
			message.blocks.push_back(MediaBlock{
				.kind = MediaKind::File,
				.bytes = "CD",
				.mimeType = u"application/pdf"_q,
				.name = u"r.pdf"_q,
			});
			message.blocks.push_back(MediaBlock{
				.kind = MediaKind::Audio,
				.bytes = "EF",
				.mimeType = u"audio/wav"_q,
			});
			message.blocks.push_back(MediaBlock{
				.kind = MediaKind::Image,
				.mimeType = u"image/png"_q,
			});
			return Expect(
				Compact(InputOf(Request{
					.model = u"gpt-5"_q,
					.messages = { message },
				})),
				u"[{\"content\":[{\"text\":\"look\",\"type\":\"input_text\"},"
				u"{\"text\":\"[hint]\",\"type\":\"input_text\"},"
				u"{\"detail\":\"auto\",\"image_url\":\"data:image/png;base64,"
				u"QUI=\",\"type\":\"input_image\"},{\"file_data\":"
				u"\"data:application/pdf;base64,Q0Q=\",\"filename\":"
				u"\"r.pdf\",\"type\":\"input_file\"}],\"role\":\"user\"}]"_q);
		} },
		{ u"responses_request_tools_round"_q, [] {
			auto first = ToolUseBlock{
				.id = u"call_a"_q,
				.name = u"read"_q,
				.input = QJsonObject{ { u"chat"_q, u"me"_q } },
			};
			auto second = ToolUseBlock{
				.id = u"call_b"_q,
				.name = u"look"_q,
			};
			auto results = Message{ Role::User };
			results.blocks.push_back(ToolResultBlock{
				.toolUseId = u"call_a"_q,
				.content = u"text"_q,
			});
			results.blocks.push_back(ToolResultBlock{
				.toolUseId = u"call_b"_q,
				.content = u"photo"_q,
				.media = { MediaBlock{
					.kind = MediaKind::Image,
					.bytes = "AB",
					.mimeType = u"image/jpeg"_q,
				} },
			});
			results.blocks.push_back(TextBlock{ u"[ctx]"_q, true });
			return Expect(
				Compact(InputOf(Request{
					.model = u"gpt-5"_q,
					.messages = {
						UserText(u"go"_q),
						Message{ Role::Assistant, {
							TextBlock{ u"Sure."_q },
							first,
							second,
						} },
						results,
					},
				})),
				u"[{\"content\":[{\"text\":\"go\",\"type\":\"input_text\"}],"
				u"\"role\":\"user\"},{\"content\":[{\"text\":\"Sure.\","
				u"\"type\":\"output_text\"}],\"role\":\"assistant\","
				u"\"type\":\"message\"},{\"arguments\":\"{\\\"chat\\\":"
				u"\\\"me\\\"}\",\"call_id\":\"call_a\",\"name\":\"read\","
				u"\"type\":\"function_call\"},{\"arguments\":\"{}\","
				u"\"call_id\":\"call_b\",\"name\":\"look\",\"type\":"
				u"\"function_call\"},{\"call_id\":\"call_a\",\"output\":"
				u"\"text\",\"type\":\"function_call_output\"},{\"call_id\":"
				u"\"call_b\",\"output\":[{\"text\":\"photo\",\"type\":"
				u"\"input_text\"},{\"detail\":\"auto\",\"image_url\":"
				u"\"data:image/jpeg;base64,QUI=\",\"type\":\"input_image\"}],"
				u"\"type\":\"function_call_output\"},{\"content\":[{\"text\":"
				u"\"[ctx]\",\"type\":\"input_text\"}],\"role\":\"user\"}]"_q);
		} },
		{ u"responses_request_media_only_result"_q, [] {
			auto results = Message{ Role::User };
			results.blocks.push_back(ToolResultBlock{
				.toolUseId = u"call_a"_q,
				.media = { MediaBlock{
					.kind = MediaKind::File,
					.bytes = "AB",
					.mimeType = u"application/pdf"_q,
					.name = u"x.pdf"_q,
				} },
			});
			const auto input = InputOf(Request{
				.model = u"gpt-5"_q,
				.messages = {
					UserText(u"go"_q),
					Message{ Role::Assistant, {
						ToolUseBlock{ .id = u"call_a"_q, .name = u"get"_q },
					} },
					results,
				},
			});
			return Expect(
				Compact(input.at(2).toObject().value(u"output"_q)),
				u"[{\"file_data\":\"data:application/pdf;base64,QUI=\","
				u"\"filename\":\"x.pdf\",\"type\":\"input_file\"}]"_q);
		} },
		{ u"responses_request_id_remap"_q, [] {
			const auto call = [](const QString &id) {
				return Message{ Role::Assistant, {
					ToolUseBlock{ .id = id, .name = u"get"_q },
				} };
			};
			const auto result = [](const QString &id, const QString &text) {
				return Message{ Role::User, {
					ToolResultBlock{ .toolUseId = id, .content = text },
				} };
			};
			const auto input = InputOf(Request{
				.model = u"gpt-5"_q,
				.messages = {
					UserText(u"go"_q),
					call(u"call_0"_q),
					result(u"call_0"_q, u"one"_q),
					call(u"call_0"_q),
					result(u"call_0"_q, u"two"_q),
					call(u"bad id!"_q),
					result(u"bad id!"_q, u"three"_q),
				},
			});
			auto ids = QStringList();
			for (const auto &value : input) {
				const auto object = value.toObject();
				if (object.contains(u"call_id"_q)) {
					ids.push_back(object.value(u"call_id"_q).toString());
				}
			}
			return Expect(
				ids.join(','),
				u"call_0,call_0,call_aigram_3_0,call_aigram_3_0,"
				u"call_aigram_5_0,call_aigram_5_0"_q);
		} },
		{ u"responses_request_raw_arguments"_q, [] {
			auto use = ToolUseBlock{ .id = u"call_0"_q, .name = u"get"_q };
			use.inputError = u"Arguments are not a JSON object"_q;
			use.rawInput = "{oops";
			const auto input = InputOf(Request{
				.model = u"gpt-5"_q,
				.messages = { UserText(u"go"_q), Message{
					Role::Assistant,
					{ use },
				} },
			});
			return Expect(
				input.at(1).toObject().value(u"arguments"_q).toString(),
				u"{oops"_q);
		} },
		{ u"responses_request_reasoning_turn"_q, [] {
			const auto input = InputOf(Request{
				.model = u"gpt-5"_q,
				.messages = {
					UserText(u"first"_q),
					Message{ Role::Assistant, {
						ThinkingBlock{ u"old"_q, Signature() },
						TextBlock{ u"answer"_q },
					} },
					UserText(u"second"_q),
					Message{ Role::Assistant, {
						ThinkingBlock{ u"now"_q, Signature() },
						ToolUseBlock{ .id = u"c1"_q, .name = u"get"_q },
					} },
					Message{ Role::User, {
						ToolResultBlock{ .toolUseId = u"c1"_q, .content = u"x"_q },
					} },
				},
			});
			if (CountType(input, u"reasoning"_q) != 1) {
				return u"expected one reasoning item, got %1"_q.arg(
					CountType(input, u"reasoning"_q));
			}
			return Expect(
				Compact(input.at(3)),
				u"{\"encrypted_content\":\"blob\",\"summary\":[],"
				u"\"type\":\"reasoning\"}"_q);
		} },
		{ u"responses_request_reasoning_foreign"_q, [] {
			const auto input = InputOf(Request{
				.model = u"gpt-5"_q,
				.messages = {
					UserText(u"go"_q),
					Message{ Role::Assistant, {
						ThinkingBlock{ u"a"_q, Signature(u"gpt-6.1-sol"_q) },
						ThinkingBlock{ u"b"_q, Signature(u"gpt-5"_q, u"x"_q) },
						ThinkingBlock{ u"c"_q, QByteArray("EqQBCkYIBR") },
						ThinkingBlock{ u"d"_q, QByteArray() },
						ToolUseBlock{ .id = u"c1"_q, .name = u"get"_q },
					} },
				},
			});
			return (CountType(input, u"reasoning"_q) == 0)
				? QString()
				: u"foreign reasoning must be dropped"_q;
		} },
		{ u"responses_request_no_item_ids"_q, [] {
			const auto input = InputOf(Request{
				.model = u"gpt-5"_q,
				.messages = {
					UserText(u"go"_q),
					Message{ Role::Assistant, {
						ThinkingBlock{ u"t"_q, Signature() },
						TextBlock{ u"a"_q },
						ToolUseBlock{ .id = u"c1"_q, .name = u"get"_q },
					} },
				},
			});
			for (const auto &value : input) {
				if (value.toObject().contains(u"id"_q)) {
					return u"items must not carry ids"_q;
				}
			}
			return QString();
		} },
		{ u"responses_adapter_prepare"_q, [] {
			auto settings = ProviderSettings();
			settings.id = u"p1"_q;
			settings.kind = ProviderKind::OpenAiResponses;
			settings.service = ProviderService::ChatGptSubscription;
			settings.baseUrl = OpenAiBaseUrl();
			settings.model = u"gpt-6.1-sol"_q;
			settings.effort = u"low"_q;
			const auto plan = OpenAi::ResponsesAdapter(settings).prepare(
				Request{
					.messages = { UserText(u"hi"_q) },
					.maxOutputTokens = 10,
				});
			auto names = QStringList();
			for (const auto &[name, value] : plan.headers) {
				names.push_back(QString::fromUtf8(name));
			}
			const auto body = QJsonDocument::fromJson(plan.body).object();
			if (const auto error = Expect(
					u"%1 %2 %3 %4 %5 %6"_q
						.arg(plan.url)
						.arg(plan.post ? u"true"_q : u"false"_q)
						.arg(names.join(','))
						.arg(body.value(u"model"_q).toString())
						.arg(Compact(body.value(u"reasoning"_q)))
						.arg(body.contains(u"max_output_tokens"_q)
							? u"true"_q
							: u"false"_q),
					u"https://api.openai.com/v1/responses true content-type,"
					u"accept gpt-6.1-sol {\"effort\":\"low\",\"summary\":"
					u"\"auto\"} false"_q);
				!error.isEmpty()) {
				return error;
			}
			settings.service = ProviderService::OpenAi;
			settings.apiKey = u"sk-1"_q;
			const auto key = OpenAi::ResponsesAdapter(settings).prepare(
				Request{
					.model = u"gpt-5"_q,
					.messages = { UserText(u"hi"_q) },
					.maxOutputTokens = 10,
					.effort = u"high"_q,
				});
			const auto keyBody = QJsonDocument::fromJson(key.body).object();
			auto auth = QByteArray();
			for (const auto &[name, value] : key.headers) {
				if (name == "Authorization") {
					auth = value;
				}
			}
			return Expect(
				u"%1 %2 %3 %4"_q
					.arg(QString::fromUtf8(auth))
					.arg(keyBody.value(u"model"_q).toString())
					.arg(keyBody.value(u"reasoning"_q).toObject()
						.value(u"effort"_q).toString())
					.arg(keyBody.value(u"max_output_tokens"_q).toInt()),
				u"Bearer sk-1 gpt-5 high 10"_q);
		} },
	};
}

[[nodiscard]] QByteArray ReasoningAndTextStream() {
	return Sse({
		R"({"type":"response.created","response":{"id":"resp_1"}})",
		R"({"type":"response.output_item.added","output_index":0,"item":{"id":"rs_1","type":"reasoning","summary":[]}})",
		R"({"type":"response.reasoning_summary_part.added","output_index":0,"summary_index":0})",
		R"({"type":"response.reasoning_summary_text.delta","output_index":0,"summary_index":0,"delta":"Plan"})",
		R"({"type":"response.reasoning_summary_text.delta","output_index":0,"summary_index":0,"delta":" it."})",
		R"({"type":"response.reasoning_summary_part.added","output_index":0,"summary_index":1})",
		R"({"type":"response.reasoning_summary_text.delta","output_index":0,"summary_index":1,"delta":"Ще"})",
		R"({"type":"response.output_item.done","output_index":0,"item":{"id":"rs_1","type":"reasoning","status":"completed","encrypted_content":"ENC","summary":[{"type":"summary_text","text":"Plan it."},{"type":"summary_text","text":"Ще"}]}})",
		R"({"type":"response.output_item.added","output_index":1,"item":{"id":"msg_1","type":"message","role":"assistant","content":[]}})",
		R"({"type":"response.output_text.delta","output_index":1,"content_index":0,"delta":"Hel"})",
		R"({"type":"response.output_text.delta","output_index":1,"content_index":0,"delta":"lo"})",
		R"({"type":"response.output_item.done","output_index":1,"item":{"id":"msg_1","type":"message","role":"assistant","content":[{"type":"output_text","text":"Hello","annotations":[]}]}})",
		R"({"type":"response.completed","response":{"id":"resp_1","status":"completed","usage":{"input_tokens":120,"input_tokens_details":{"cached_tokens":100},"output_tokens":30,"output_tokens_details":{"reasoning_tokens":20}}}})",
	});
}

[[nodiscard]] QByteArray ParallelCallsStream() {
	return Sse({
		R"({"type":"response.output_item.added","output_index":0,"item":{"id":"fc_1","type":"function_call","call_id":"call_A","name":"read","arguments":""}})",
		R"({"type":"response.function_call_arguments.delta","output_index":0,"delta":"{\"chat\":"})",
		R"({"type":"response.output_item.added","output_index":1,"item":{"id":"fc_2","type":"function_call","call_id":"call_B","name":"look","arguments":""}})",
		R"({"type":"response.function_call_arguments.delta","output_index":0,"delta":"\"me\"}"})",
		R"({"type":"response.output_item.done","output_index":1,"item":{"id":"fc_2","type":"function_call","call_id":"call_B","name":"look","arguments":""}})",
		R"({"type":"response.output_item.done","output_index":0,"item":{"id":"fc_1","type":"function_call","call_id":"call_A","name":"read","arguments":"{\"chat\":\"me\"}"}})",
		R"({"type":"response.completed","response":{"usage":{"input_tokens":5,"output_tokens":7}}})",
	});
}

std::vector<Check> StreamParseChecks() {
	return {
		{ u"responses_parse_reasoning_text"_q, [] {
			const auto parsed = Parse(ReasoningAndTextStream());
			const auto &response = parsed.response;
			if (response.error) {
				return ErrorText(response.error);
			}
			const auto got = u"%1 | %2 | %3 %4 %5 %6"_q
				.arg(DescribeDeltas(parsed.deltas))
				.arg(DescribeBlocks(response))
				.arg(int(response.stopReason))
				.arg(response.usage.inputTokens)
				.arg(response.usage.cachedInputTokens)
				.arg(response.usage.outputTokens);
			if (const auto error = Expect(
					got,
					u"R[Plan it.\n\nЩе] T[Hello] | thinking(Plan it.\n\nЩе) "
					u"text(Hello) | %1 120 100 30"_q.arg(
						int(StopReason::EndTurn)));
				!error.isEmpty()) {
				return error;
			}
			const auto &thinking = std::get<ThinkingBlock>(
				response.message.blocks[0]);
			if (!OpenAi::IsReasoningSignature(thinking.signature)) {
				return u"expected an envelope signature"_q;
			}
			const auto item = OpenAi::ReasoningItem(
				thinking.signature,
				u"gpt-5"_q,
				u"origin"_q);
			return item
				? Expect(
					Compact(*item),
					u"{\"encrypted_content\":\"ENC\",\"summary\":[{\"text\":"
					u"\"Plan it.\",\"type\":\"summary_text\"},{\"text\":"
					u"\"Ще\",\"type\":\"summary_text\"}],"
					u"\"type\":\"reasoning\"}"_q)
				: u"the envelope must give the item back"_q;
		} },
		{ u"responses_parse_parallel_calls"_q, [] {
			const auto parsed = Parse(ParallelCallsStream());
			const auto &response = parsed.response;
			if (response.error) {
				return ErrorText(response.error);
			}
			return Expect(
				u"%1 | %2 | %3"_q
					.arg(DescribeDeltas(parsed.deltas))
					.arg(DescribeBlocks(response))
					.arg(int(response.stopReason)),
				u"U[call_A read] U[call_B look] | use(call_A, read, "
				u"{\"chat\":\"me\"}) use(call_B, look, {}) | %1"_q.arg(
					int(StopReason::ToolUse)));
		} },
		{ u"responses_parse_chunked"_q, [] {
			for (const auto &bytes : {
					ReasoningAndTextStream(),
					ParallelCallsStream() }) {
				const auto whole = Parse(bytes);
				const auto expected = DescribeBlocks(whole.response)
					+ DescribeDeltas(whole.deltas);
				for (auto mode = 1; mode != 6; ++mode) {
					const auto split = Parse(bytes, mode);
					auto text = QString();
					auto thinking = QString();
					for (const auto &delta : split.deltas) {
						if (const auto t = std::get_if<TextDelta>(&delta)) {
							text += t->text;
						} else if (const auto r = std::get_if<ThinkingDelta>(
								&delta)) {
							thinking += r->text;
						}
					}
					auto wholeText = QString();
					auto wholeThinking = QString();
					for (const auto &delta : whole.deltas) {
						if (const auto t = std::get_if<TextDelta>(&delta)) {
							wholeText += t->text;
						} else if (const auto r = std::get_if<ThinkingDelta>(
								&delta)) {
							wholeThinking += r->text;
						}
					}
					if (split.response.error
						|| DescribeBlocks(split.response)
							!= DescribeBlocks(whole.response)
						|| text != wholeText
						|| thinking != wholeThinking) {
						return u"mode %1 differs from %2"_q.arg(mode).arg(
							expected);
					}
				}
			}
			return QString();
		} },
		{ u"responses_parse_final_output"_q, [] {
			const auto parsed = Parse(Sse({
				R"({"type":"response.output_text.delta","delta":"Hi"})",
				R"({"type":"response.completed","response":{"output":[{"type":"message","content":[{"type":"output_text","text":"Hi"}]},{"type":"web_search_call","id":"ws_1"}],"usage":{"input_tokens":1,"output_tokens":1}}})",
			}));
			return parsed.response.error
				? ErrorText(parsed.response.error)
				: Expect(DescribeBlocks(parsed.response), u"text(Hi)"_q);
		} },
		{ u"responses_parse_bad_arguments"_q, [] {
			const auto parsed = Parse(Sse({
				R"({"type":"response.output_item.done","output_index":0,"item":{"type":"function_call","call_id":"c1","name":"get","arguments":"{oops"}})",
				R"({"type":"response.output_item.done","output_index":1,"item":{"type":"function_call","call_id":"c2","name":"get","arguments":"[1]"}})",
				R"({"type":"response.completed","response":{}})",
			}));
			return Expect(
				DescribeBlocks(parsed.response),
				u"use(c1, get, {}, raw={oops) use(c2, get, {}, raw=[1])"_q);
		} },
		{ u"responses_parse_stop_reasons"_q, [] {
			const auto stop = [](const char *last, const char *item) {
				const auto bytes = Sse({ item, last });
				const auto parsed = Parse(bytes);
				return parsed.response.error
					? -1
					: int(parsed.response.stopReason);
			};
			const auto message = R"({"type":"response.output_item.done","output_index":0,"item":{"type":"message","content":[{"type":"output_text","text":"x"}]}})";
			const auto refusal = R"({"type":"response.output_item.done","output_index":0,"item":{"type":"message","content":[{"type":"refusal","refusal":"No."}]}})";
			const auto got = u"%1 %2 %3 %4"_q
				.arg(stop(R"({"type":"response.incomplete","response":{"incomplete_details":{"reason":"max_output_tokens"}}})", message))
				.arg(stop(R"({"type":"response.incomplete","response":{"incomplete_details":{"reason":"content_filter"}}})", message))
				.arg(stop(R"({"type":"response.completed","response":{}})", refusal))
				.arg(stop(R"({"type":"response.completed","response":{}})", message));
			return Expect(got, u"%1 %2 %2 %3"_q
				.arg(int(StopReason::MaxTokens))
				.arg(int(StopReason::Refusal))
				.arg(int(StopReason::EndTurn)));
		} },
		{ u"responses_parse_stream_errors"_q, [] {
			const auto failed = Parse(Sse({
				R"({"type":"response.output_text.delta","delta":"par"})",
				R"({"type":"response.failed","response":{"status":"failed","error":{"code":"subscription_sharing_usage_limit_exceeded","message":"Limit reached."}}})",
			})).response;
			const auto event = Parse(Sse({
				R"({"type":"error","code":"server_error","message":"Oops","param":null})",
			})).response;
			const auto nested = Parse(Sse({
				R"({"type":"error","error":{"code":"rate_limit_exceeded","message":"Slow"}})",
			})).response;
			const auto none = Parse(Sse({
				R"({"type":"response.output_text.delta","delta":"x"})",
			})).response;
			return Expect(
				ErrorText(failed.error) + u" | "_q
					+ ErrorText(event.error) + u" | "_q
					+ ErrorText(nested.error) + u" | "_q
					+ ErrorText(none.error),
				u"rate_limited/0/subscription_sharing_usage_limit_exceeded/"
				u"Limit reached. | overloaded/0/server_error/Oops | "
				u"rate_limited/0/rate_limit_exceeded/Slow | bad_response/200/"
				u"/Stream ended without a final response event"_q);
		} },
		{ u"responses_parse_transport_errors"_q, [] {
			auto parser = OpenAi::ResponsesParser(u"gpt-5"_q, u"o"_q);
			parser->head(HttpHead{ 200, QByteArray() });
			const auto network = parser->finish(u"timed out"_q);
			const auto http = Parse(
				R"({"error":{"message":"Bad key","type":"invalid_request_error","param":null,"code":"invalid_api_key"}})",
				0,
				401).response;
			return Expect(
				ErrorText(network.error) + u" | "_q + ErrorText(http.error),
				u"network/0//timed out | auth/401/invalid_api_key/Bad key"_q);
		} },
		{ u"responses_error_bodies"_q, [] {
			const auto parse = [](int status, const QByteArray &body) {
				return ErrorText(OpenAi::ParseResponsesError(
					HttpHead{ status, "7" },
					body));
			};
			const auto got = QStringList{
				parse(400, R"({"error":{"message":"Bad","type":"invalid_request_error","param":null,"code":null}})"),
				parse(403, R"({"detail":"Unsupported country."})"),
				parse(502, "Bad gateway"),
				parse(503, R"({"error":{"message":"Later","code":"subscription_sharing_usage_unavailable"}})"),
				parse(429, R"({"error":{"message":"Limit","code":"subscription_sharing_usage_limit_exceeded"}})"),
				parse(403, R"({"error":{"message":"Plan","code":"subscription_sharing_user_not_eligible"}})"),
				parse(400, R"({"error":{"message":"Cap","code":"subscription_sharing_unsupported_capability","param":"tools"}})"),
				parse(401, R"({"error":{"message":"Out","code":"chatgpt_signed_out"}})"),
			}.join(u" | "_q);
			const auto retry = OpenAi::ParseResponsesError(
				HttpHead{ 429, "7" },
				QByteArray()).retryAfter;
			return Expect(
				got + u" | "_q + QString::number(retry),
				u"bad_request/400/invalid_request_error/Bad | "
				u"auth/403//Unsupported country. | "
				u"overloaded/502//Bad gateway | "
				u"overloaded/503/subscription_sharing_usage_unavailable/Later | "
				u"rate_limited/429/subscription_sharing_usage_limit_exceeded/"
				u"Limit | auth/403/subscription_sharing_user_not_eligible/Plan | "
				u"bad_request/400/subscription_sharing_unsupported_capability/"
				u"Cap | auth/401/chatgpt_signed_out/Out | 7000"_q);
		} },
		{ u"responses_reasoning_signature"_q, [] {
			const auto signature = Signature();
			const auto item = OpenAi::ReasoningItem(
				signature,
				u"gpt-5"_q,
				u"origin"_q);
			const auto noBlob = OpenAi::ReasoningSignature(
				u"gpt-5"_q,
				u"origin"_q,
				QJsonObject{ { u"type"_q, u"reasoning"_q } });
			const auto got = u"%1 %2 %3 %4 %5 %6"_q
				.arg(item ? Compact(*item) : u"none"_q)
				.arg(OpenAi::ReasoningItem(signature, u"gpt-6"_q, u"origin"_q)
					? u"model"_q
					: u"-"_q)
				.arg(OpenAi::ReasoningItem(signature, u"gpt-5"_q, u"other"_q)
					? u"origin"_q
					: u"-"_q)
				.arg(OpenAi::ReasoningItem(noBlob, u"gpt-5"_q, u"origin"_q)
					? u"blob"_q
					: u"-"_q)
				.arg(OpenAi::IsReasoningSignature("EqQBCkYIBR") ? 1 : 0)
				.arg(OpenAi::IsReasoningSignature(signature) ? 1 : 0);
			return Expect(
				got,
				u"{\"encrypted_content\":\"blob\",\"summary\":[],\"type\":"
				u"\"reasoning\"} - - - 0 1"_q);
		} },
	};
}

std::vector<Check> ModelListChecks() {
	return {
		{ u"responses_known_models"_q, [] {
			const auto find = [](const QString &id) {
				const auto known = OpenAi::FindKnownModel(id);
				return known
					? u"%1:%2:%3:%4"_q
						.arg(known->id, known->name)
						.arg(known->priceInput)
						.arg(QStringList(
							known->effortLevels.begin(),
							known->effortLevels.end()).join(','))
					: u"none"_q;
			};
			const auto got = QStringList{
				find(u"gpt-6.1-sol-2026-09-29"_q),
				find(u"gpt-5.4-mini"_q),
				find(u"gpt-5.4-mini-2026-03-01"_q),
				find(u"gpt-5.4"_q),
				find(u"gpt-6-luna"_q),
				find(u"gpt-5.4o"_q),
				find(u"o3"_q),
			}.join(u" "_q);
			return Expect(
				got,
				u"gpt-6.1-sol:GPT-6.1 Sol:2:low,medium,high,xhigh,max "
				u"gpt-5.4-mini:GPT-5.4 Mini:0.75:low,medium,high "
				u"gpt-5.4-mini:GPT-5.4 Mini:0.75:low,medium,high "
				u"gpt-5.4:GPT-5.4:2.5:low,medium,high "
				u"gpt-6-luna:GPT-6 Luna:0.1:none,low,medium,high,xhigh,max "
				u"none none"_q);
		} },
		{ u"responses_support_and_limits"_q, [] {
			const auto levels = OpenAi::ResponsesEffortLevels();
			const auto got = u"%1%2%3%4%5 %6 %7"_q
				.arg(OpenAi::SupportsReasoning(u"gpt-4.1"_q) ? 1 : 0)
				.arg(OpenAi::SupportsReasoning(u"gpt-4o-mini"_q) ? 1 : 0)
				.arg(OpenAi::SupportsReasoning(u"chatgpt-4o-latest"_q) ? 1 : 0)
				.arg(OpenAi::SupportsReasoning(u"o3"_q) ? 1 : 0)
				.arg(OpenAi::SupportsReasoning(u"gpt-6.1-sol"_q) ? 1 : 0)
				.arg(OpenAi::ContextWindowLimit())
				.arg(QStringList(levels.begin(), levels.end()).join(','));
			return Expect(
				got,
				u"00011 272000 none,low,medium,high,xhigh,max"_q);
		} },
		{ u"responses_models_request"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = OpenAiBaseUrl();
			const auto plan = OpenAi::ResponsesModelsRequest(settings);
			settings.apiKey = u"sk-1"_q;
			const auto key = OpenAi::ResponsesModelsRequest(settings);
			return Expect(
				u"%1 %2 %3 %4"_q
					.arg(plan.url)
					.arg(plan.post ? u"post"_q : u"get"_q)
					.arg(plan.headers.size())
					.arg(key.headers.empty()
						? QString()
						: QString::fromUtf8(key.headers[0].second)),
				u"https://api.openai.com/v1/models get 0 Bearer sk-1"_q);
		} },
		{ u"responses_parse_models_plan"_q, [] {
			const auto models = OpenAi::ParseResponsesModels(R"({"models":[
				{"slug":"gpt-6.1-sol","display_name":"GPT-6.1 Sol","visibility":"list","context_window":1050000,"supported_reasoning_levels":[{"effort":"low"},{"effort":"minimal"},{"effort":"high"}]},
				{"slug":"gpt-6-luna","visibility":"list","input_modalities":["text"],"supported_reasoning_levels":["none","low"]},
				{"slug":"gpt-hidden","display_name":"Hidden","visibility":"hide"},
				{"slug":"mystery-1","context_window":64000,"input_modalities":["text","image","file"]},
				{"display_name":"No slug"}
			]})");
			return Expect(
				DescribeModels(models),
				u"gpt-6.1-sol|GPT-6.1 Sol|272000|vf|low,high|2 ; "
				u"gpt-6-luna|GPT-6 Luna|272000||none,low|0.1 ; "
				u"mystery-1|mystery-1|64000|vf||0"_q);
		} },
		{ u"responses_parse_models_api"_q, [] {
			const auto models = OpenAi::ParseResponsesModels(R"({"object":"list","data":[
				{"id":"gpt-5","object":"model"},
				{"id":"text-embedding-3-large"},
				{"id":"gpt-4o-audio-preview"},
				{"id":"gpt-5-codex"},
				{"id":"dall-e-3"},
				{"id":"whisper-1"},
				{"id":"gpt-4o-realtime-preview"},
				{"id":"gpt-4o-search-preview"},
				{"id":"gpt-image-1"},
				{"id":"omni-moderation-latest"},
				{"id":"gpt-3.5-turbo-instruct"},
				{"id":"gpt-4o-mini-tts"},
				{"id":"o3"},
				{"id":"gpt-6.1-sol-2026-09-29"},
				{"id":"gpt-4.1"},
				{"id":"gpt-6-astra"}
			]})");
			auto ids = QStringList();
			for (const auto &model : models) {
				ids.push_back(model.id + u":"_q + model.name);
			}
			const auto tools = ranges::all_of(models, [](const OpenAi::ModelInfo &m) {
				return m.tools && *m.tools;
			});
			return Expect(
				ids.join(' ') + (tools ? u" tools"_q : u" no-tools"_q),
				u"gpt-6-astra:GPT-6 Astra gpt-6.1-sol-2026-09-29:GPT-6.1 Sol "
				u"gpt-5:GPT-5 gpt-4.1:gpt-4.1 o3:o3 tools"_q);
		} },
		{ u"responses_parse_models_bad"_q, [] {
			return (OpenAi::ParseResponsesModels("nope").empty()
				&& OpenAi::ParseResponsesModels(R"({"x":1})").empty()
				&& OpenAi::ParseResponsesModels(R"([1,2])").empty())
				? QString()
				: u"malformed lists must be empty"_q;
		} },
		{ u"responses_error_kind_for_code"_q, [] {
			const auto kind = [](const char *code) {
				return ErrorKindName(OpenAi::ErrorKindForCode(
					QString::fromUtf8(code),
					ErrorKind::BadResponse));
			};
			return Expect(
				QStringList{
					kind("subscription_sharing_usage_limit_exceeded"),
					kind("subscription_sharing_usage_unavailable"),
					kind("subscription_sharing_user_unavailable"),
					kind("subscription_sharing_user_not_eligible"),
					kind("subscription_sharing_invalid_user"),
					kind("chatpass_v2_scope_not_authorized"),
					kind("chatpass_v2_invalid_authorization_context"),
					kind("invalid_api_key"),
					kind("chatgpt_signed_out"),
					kind("subscription_sharing_unsupported_capability"),
					kind("subscription_sharing_route_not_supported"),
					kind("something_else"),
				}.join(','),
				u"rate_limited,overloaded,overloaded,auth,auth,auth,auth,"
				u"auth,auth,bad_request,bad_request,bad_response"_q);
		} },
	};
}

} // namespace

std::vector<Check> ResponsesChecks() {
	auto result = RequestBodyChecks();
	for (const auto &list : { StreamParseChecks(), ModelListChecks() }) {
		result.insert(result.end(), list.begin(), list.end());
	}
	return result;
}

} // namespace Agent::Tests
