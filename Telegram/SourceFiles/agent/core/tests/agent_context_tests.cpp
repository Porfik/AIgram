/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_context.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] Message TextMessage(Role role, QString text) {
	return Message{ role, { TextBlock{ std::move(text) } } };
}

[[nodiscard]] Message UserText(QString text) {
	return TextMessage(Role::User, std::move(text));
}

[[nodiscard]] Message AssistantText(QString text) {
	return TextMessage(Role::Assistant, std::move(text));
}

[[nodiscard]] QString Repeated(int size, QChar character) {
	return QString(size, character);
}

[[nodiscard]] QString TruncationLine(int dropped) {
	return u"\n[%1 characters dropped. Request a smaller page.]"_q.arg(dropped);
}

[[nodiscard]] QString DescribeTexts(const std::vector<Message> &messages) {
	auto result = QString();
	for (const auto &message : messages) {
		for (const auto &block : message.blocks) {
			if (const auto text = std::get_if<TextBlock>(&block)) {
				if (!result.isEmpty()) {
					result += u" | "_q;
				}
				result += (text->hidden ? u"hidden("_q : u"text("_q)
					+ text->text
					+ ')';
			}
		}
	}
	return result;
}

} // namespace

std::vector<Check> ContextChecks() {
	return {
		{ u"context_estimate_tokens_grows"_q, [] {
			if (EstimateTokens(QString()) != 0) {
				return u"empty text is not zero"_q;
			}
			const auto small = EstimateTokens(Repeated(30, 'a'));
			const auto large = EstimateTokens(Repeated(300, 'a'));
			if (small != 10 || large != 100) {
				return u"unexpected estimates: %1, %2"_q
					.arg(small)
					.arg(large);
			}
			return QString();
		} },
		{ u"context_estimate_tokens_message"_q, [] {
			const auto text = EstimateTokens(TextMessage(
				Role::User,
				Repeated(300, 'a')));
			if (text != 104) {
				return u"text message: %1"_q.arg(text);
			}
			const auto image = EstimateTokens(Message{
				Role::User,
				{ ImageBlock{ QByteArray("bytes"), u"image/png"_q } },
			});
			if (image != 1004) {
				return u"image message: %1"_q.arg(image);
			}
			const auto tool = EstimateTokens(Message{
				Role::User,
				{ ToolResultBlock{ u"t1"_q, Repeated(300, 'r'), false } },
			});
			if (tool != 104) {
				return u"tool result message: %1"_q.arg(tool);
			}
			return QString();
		} },
		{ u"context_estimate_tokens_request"_q, [] {
			const auto request = Request{
				u"model"_q,
				Repeated(30, 's'),
				{ UserText(Repeated(300, 'a')) },
				{ ToolSpec{
					u"echo"_q,
					u"Echoes."_q,
					QJsonObject{ { u"type"_q, QJsonValue(u"object"_q) } },
				} },
				0,
			};
			const auto estimate = EstimateTokens(request);
			const auto expected = 10
				+ EstimateTokens(u"echoEchoes."_q)
				+ EstimateTokens(uR"({"type":"object"})"_q)
				+ 104;
			if (estimate != expected) {
				return u"request: %1, expected %2"_q
					.arg(estimate)
					.arg(expected);
			}
			return QString();
		} },
		{ u"context_cap_tool_result_below"_q, [] {
			const auto content = Repeated(100, 'a');
			return (CapToolResult(content, 200) == content)
				? QString()
				: u"content changed"_q;
		} },
		{ u"context_cap_tool_result_at_limit"_q, [] {
			const auto content = Repeated(200, 'a');
			return (CapToolResult(content, 200) == content)
				? QString()
				: u"content changed"_q;
		} },
		{ u"context_cap_tool_result_above"_q, [] {
			const auto content = Repeated(250, 'a');
			const auto capped = CapToolResult(content, 200);
			const auto expected = Repeated(200, 'a') + TruncationLine(50);
			return (capped == expected)
				? QString()
				: u"capped: %1"_q.arg(capped);
		} },
		{ u"context_cap_tool_result_surrogate"_q, [] {
			const auto emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
			auto content = Repeated(199, 'a') + emoji + Repeated(50, 'b');
			const auto capped = CapToolResult(content, 200);
			const auto expected = Repeated(199, 'a')
				+ TruncationLine(52);
			return (capped == expected)
				? QString()
				: u"capped: %1"_q.arg(capped);
		} },
		{ u"context_messages_no_summary"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(u"first"_q),
				AssistantText(u"second"_q),
			};
			const auto messages = ContextMessages(record);
			if (messages.size() != 2) {
				return u"message count differs"_q;
			} else if (DescribeTexts(messages)
				!= u"text(first) | text(second)"_q) {
				return u"texts: %1"_q.arg(DescribeTexts(messages));
			}
			return QString();
		} },
		{ u"context_messages_summary_before_user"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(u"first"_q),
				AssistantText(u"second"_q),
				UserText(u"third"_q),
			};
			record.summary = u"the summary"_q;
			record.summaryUntil = 2;
			const auto messages = ContextMessages(record);
			if (messages.size() != 1 || messages[0].role != Role::User) {
				return u"unexpected shape"_q;
			}
			const auto first = std::get_if<TextBlock>(&messages[0].blocks[0]);
			const auto second = std::get_if<TextBlock>(&messages[0].blocks[1]);
			if (!first || !second) {
				return u"blocks missing"_q;
			} else if (first->text
					!= u"Summary of the earlier conversation:\nthe summary"_q
				|| !first->hidden) {
				return u"summary block wrong: %1"_q.arg(first->text);
			} else if (second->text != u"third"_q || second->hidden) {
				return u"user text wrong"_q;
			}
			return QString();
		} },
		{ u"context_messages_summary_before_assistant"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(u"first"_q),
				AssistantText(u"second"_q),
			};
			record.summary = u"the summary"_q;
			record.summaryUntil = 1;
			const auto messages = ContextMessages(record);
			if (messages.size() != 2) {
				return u"message count differs"_q;
			}
			const auto summary = std::get_if<TextBlock>(
				&messages[0].blocks[0]);
			if (!summary || messages[0].role != Role::User) {
				return u"leading summary message missing"_q;
			} else if (summary->text
					!= u"Summary of the earlier conversation:\nthe summary"_q
				|| !summary->hidden) {
				return u"summary block wrong: %1"_q.arg(summary->text);
			} else if (messages[1].role != Role::Assistant) {
				return u"assistant message not preserved"_q;
			}
			return QString();
		} },
		{ u"context_messages_everything_summarized"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(u"first"_q),
				AssistantText(u"second"_q),
			};
			record.summary = u"the summary"_q;
			record.summaryUntil = 2;
			const auto messages = ContextMessages(record);
			if (messages.size() != 1 || messages[0].role != Role::User) {
				return u"unexpected shape"_q;
			}
			const auto summary = std::get_if<TextBlock>(&messages[0].blocks[0]);
			if (!summary
				|| !summary->hidden
				|| messages[0].blocks.size() != 1) {
				return u"summary block wrong"_q;
			}
			return QString();
		} },
		{ u"context_plan_compaction_under_threshold"_q, [] {
			auto record = SessionRecord();
			for (auto i = 0; i != 16; ++i) {
				record.messages.push_back((i % 2 == 0)
					? UserText(Repeated(2000, 'a'))
					: AssistantText(Repeated(2000, 'a')));
			}
			if (PlanCompaction(record, 0, 32768) != 0) {
				return u"compacted below the threshold"_q;
			}
			record.summary = u"s"_q;
			record.summaryUntil = 4;
			if (PlanCompaction(record, 0, 32768) != 4) {
				return u"did not keep the current summaryUntil"_q;
			}
			return QString();
		} },
		{ u"context_plan_compaction_smallest_fitting_cut"_q, [] {
			auto record = SessionRecord();
			for (auto i = 0; i != 16; ++i) {
				record.messages.push_back((i % 2 == 0)
					? UserText(Repeated(2000, 'a'))
					: AssistantText(Repeated(2000, 'a')));
			}
			if (PlanCompaction(record, 0, 12000) != 12) {
				return u"expected the turn start at 12"_q;
			}
			return QString();
		} },
		{ u"context_plan_compaction_skips_tool_results"_q, [] {
			auto record = SessionRecord();
			for (auto i = 0; i != 16; ++i) {
				record.messages.push_back((i % 2 == 0)
					? UserText(Repeated(2000, 'a'))
					: AssistantText(Repeated(2000, 'a')));
			}
			record.messages[12] = Message{
				Role::User,
				{ ToolResultBlock{ u"t1"_q, u"x"_q, false } },
			};
			if (PlanCompaction(record, 0, 12000) != 14) {
				return u"expected the turn start at 14"_q;
			}
			return QString();
		} },
		{ u"context_plan_compaction_falls_back_to_end"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(Repeated(900, 'a')),
				AssistantText(Repeated(900, 'a')),
			};
			if (PlanCompaction(record, 8000, 12000) != 2) {
				return u"expected the transcript end"_q;
			}
			return QString();
		} },
		{ u"context_plan_compaction_respects_summary_until"_q, [] {
			auto record = SessionRecord();
			for (auto i = 0; i != 12; ++i) {
				record.messages.push_back((i % 2 == 0)
					? UserText(Repeated(900, 'a'))
					: AssistantText(Repeated(900, 'a')));
			}
			record.summary = Repeated(70000, 's');
			record.summaryUntil = 6;
			if (PlanCompaction(record, 0, 32768) != 8) {
				return u"expected the turn start at 8"_q;
			}
			return QString();
		} },
		{ u"context_plan_compaction_default_window"_q, [] {
			auto record = SessionRecord();
			for (auto i = 0; i != 16; ++i) {
				record.messages.push_back((i % 2 == 0)
					? UserText(Repeated(2000, 'a'))
					: AssistantText(Repeated(2000, 'a')));
			}
			if (PlanCompaction(record, 0, 0) != 0) {
				return u"zero window did not use the default"_q;
			}
			return QString();
		} },
		{ u"context_compaction_request"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(u"Hello there"_q),
				Message{
					Role::Assistant,
					{
						TextBlock{ u"Hi! Let me check."_q },
						ToolUseBlock{
							u"tu1"_q,
							u"echo"_q,
							QJsonObject{ { u"x"_q, QJsonValue(1) } },
							QString(),
							QByteArray(),
						},
					},
				},
				Message{
					Role::User,
					{ ToolResultBlock{ u"tu1"_q, u"tool output"_q, false } },
				},
				UserText(u"second"_q),
			};
			record.summary = u"old summary"_q;
			record.summaryUntil = 1;
			const auto request = CompactionRequest(record, 4, u"model-x"_q);
			if (request.model != u"model-x"_q) {
				return u"model differs"_q;
			} else if (!request.tools.empty()) {
				return u"compaction must not have tools"_q;
			} else if (request.maxOutputTokens != 2048) {
				return u"maxOutputTokens: %1"_q
					.arg(request.maxOutputTokens);
			} else if (request.messages.size() != 1
				|| request.messages[0].role != Role::User) {
				return u"unexpected messages shape"_q;
			}
			const auto text = std::get_if<TextBlock>(
				&request.messages[0].blocks[0]);
			if (!text) {
				return u"text block missing"_q;
			}
			const auto contains = [&](const QString &needle) {
				return text->text.contains(needle)
					? QString()
					: u"missing: %1"_q.arg(needle);
			};
			for (const auto &needle : {
				u"Previous summary:\nold summary"_q,
				u"Transcript:"_q,
				u"Assistant: Hi! Let me check."_q,
				u"Tool call echo {\"x\":1}"_q,
				u"Tool result: tool output"_q,
				u"User: second"_q,
			}) {
				const auto failure = contains(needle);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			if (text->text.contains(u"Hello there"_q)) {
				return u"summary range must start after it"_q;
			}
			return QString();
		} },
		{ u"context_title_request_caps_inputs"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				UserText(Repeated(2000, 'u')),
				AssistantText(Repeated(600, 'a')),
				UserText(u"more"_q),
			};
			const auto request = TitleRequest(record, u"model-x"_q);
			if (request.model != u"model-x"_q) {
				return u"model differs"_q;
			} else if (!request.tools.empty()) {
				return u"title must not have tools"_q;
			} else if (request.maxOutputTokens != 256) {
				return u"maxOutputTokens: %1"_q
					.arg(request.maxOutputTokens);
			} else if (request.messages.size() != 1) {
				return u"unexpected messages shape"_q;
			}
			const auto block = std::get_if<TextBlock>(
				&request.messages[0].blocks[0]);
			if (!block) {
				return u"text block missing"_q;
			}
			const auto text = block->text;
			const auto userPart = Repeated(1500, 'u');
			const auto assistantPart = Repeated(500, 'a');
			const auto expected = u"User message:\n"_q
				+ userPart
				+ u"\n\nAssistant reply:\n"_q
				+ assistantPart;
			return (text == expected)
				? QString()
				: u"title input: %1..."_q.arg(text.left(40));
		} },
		{ u"context_clean_title_variants"_q, [] {
			const auto cases = std::vector<std::pair<QString, QString>>{
				{ u"\"My title\""_q, u"My title"_q },
				{ u"\u201CMy title\u201D"_q, u"My title"_q },
				{ u"'My title'"_q, u"My title"_q },
				{ u"\u2018My title\u2019"_q, u"My title"_q },
				{ u"My title."_q, u"My title"_q },
				{ u"\"My title.\""_q, u"My title"_q },
				{ u"\n\n  Second line  \nfirst"_q, u"Second line"_q },
				{ Repeated(100, 't'), Repeated(60, 't') },
				{ u"   Spaced   title   "_q, u"Spaced   title"_q },
				{ QString(), QString() },
			};
			for (const auto &[input, expected] : cases) {
				const auto cleaned = CleanTitle(input);
				if (cleaned != expected) {
					return u"CleanTitle(%1): %2"_q.arg(input, cleaned);
				}
			}
			return QString();
		} },
		{ u"context_overflow_errors"_q, [] {
			const auto openai = Error{
				ErrorKind::BadRequest,
				400,
				u"This model's maximum context length is 16385 tokens. "
				u"However, you requested 20000 tokens in the messages."_q,
			};
			const auto openrouter = Error{
				ErrorKind::BadRequest,
				400,
				u"Provider returned error: prompt is too long: 150000 "
				u"tokens > 131072 maximum"_q,
			};
			const auto status413 = Error{
				ErrorKind::Network,
				413,
				u"context length exceeded"_q,
			};
			const auto unrelated = Error{
				ErrorKind::BadRequest,
				400,
				u"no-such/model-xyz is not a valid model ID"_q,
			};
			const auto notBadRequest = Error{
				ErrorKind::Network,
				0,
				u"context length exceeded"_q,
			};
			if (!IsContextOverflow(openai)) {
				return u"openai overflow not detected"_q;
			} else if (!IsContextOverflow(openrouter)) {
				return u"openrouter overflow not detected"_q;
			} else if (!IsContextOverflow(status413)) {
				return u"413 overflow not detected"_q;
			} else if (IsContextOverflow(unrelated)) {
				return u"unrelated 400 detected"_q;
			} else if (IsContextOverflow(notBadRequest)) {
				return u"network error detected as overflow"_q;
			}
			return QString();
		} },
	};

}

} // namespace Agent::Tests
