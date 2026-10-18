/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_sse.h"

#include <random>

namespace Agent::Tests {
namespace {

[[nodiscard]] QString Describe(const std::vector<SseEvent> &events) {
	auto result = QString();
	for (const auto &event : events) {
		if (!result.isEmpty()) {
			result += u", "_q;
		}
		result += u"{type: %1, data: %2}"_q.arg(
			QString::fromUtf8(event.event),
			QString::fromUtf8(event.data).replace('\n', u"\\n"_q));
	}
	return result;
}

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

[[nodiscard]] std::vector<SseEvent> Parse(const QByteArray &bytes, int mode) {
	auto parser = SseParser();
	auto result = std::vector<SseEvent>();
	for (const auto &chunk : SplitChunks(bytes, mode)) {
		auto events = parser.feed(chunk);
		result.insert(result.end(), events.begin(), events.end());
	}
	auto events = parser.finish();
	result.insert(result.end(), events.begin(), events.end());
	return result;
}

[[nodiscard]] QString CheckSame(
		const QByteArray &bytes,
		const std::vector<SseEvent> &expected,
		const QString &what) {
	for (const auto mode : { 0, 1, 2 }) {
		const auto parsed = Parse(bytes, mode);
		if (Describe(parsed) != Describe(expected)) {
			return u"%1 (mode %2): got %3, expected %4"_q
				.arg(what)
				.arg(mode)
				.arg(Describe(parsed), Describe(expected));
		}
	}
	return QString();
}

[[nodiscard]] QString CheckFixture(const QString &name) {
	const auto bytes = ReadFixture(name);
	if (bytes.isEmpty()) {
		return u"fixture is empty or missing: "_q + name;
	}
	return CheckSame(bytes, Parse(bytes, 0), name);
}

[[nodiscard]] std::vector<Check> FixtureChecks() {
	return {
		{ u"sse_fixture_openai_text"_q, [] {
			return CheckFixture(u"openai-text.sse"_q);
		} },
		{ u"sse_fixture_openai_tool_call"_q, [] {
			return CheckFixture(u"openai-tool-call.sse"_q);
		} },
		{ u"sse_fixture_openai_parallel_tools"_q, [] {
			return CheckFixture(u"openai-parallel-tools.sse"_q);
		} },
		{ u"sse_fixture_openai_tool_result"_q, [] {
			return CheckFixture(u"openai-tool-result.sse"_q);
		} },
	};
}

} // namespace

std::vector<Check> SseChecks() {
	auto checks = FixtureChecks();
	auto append = [&](Check &&check) {
		checks.push_back(std::move(check));
	};
	append({ u"sse_line_endings_crlf_cr_lf"_q, [] {
		const auto expected = std::vector<SseEvent>{
			{ "", "a" },
			{ "", "b" },
			{ "", "c" },
		};
		auto failure = CheckSame(
			"data: a\r\n\r\ndata: b\r\rdata: c\n\n",
			expected,
			u"mixed line endings"_q);
		if (!failure.isEmpty()) {
			return failure;
		}
		return CheckSame(
			"data: a\r\n\r\ndata: b\r\rdata: c\r\r",
			expected,
			u"trailing cr flushed at finish"_q);
	} });
	append({ u"sse_comments_and_unknown_fields"_q, [] {
		return CheckSame(
			": OPENROUTER PROCESSING\n"
			"id: 5\n"
			"retry: 10\n"
			"unknown: ignored\n"
			"data: a\n\n",
			{ { "", "a" } },
			u"comments and ignored fields"_q);
	} });
	append({ u"sse_colon_space_and_colon_in_data"_q, [] {
		auto failure = CheckSame(
			"data:a\n\n",
			{ { "", "a" } },
			u"no space after colon"_q);
		if (!failure.isEmpty()) {
			return failure;
		}
		return CheckSame(
			"data: a:b:c \n\n",
			{ { "", "a:b:c " } },
			u"only first space stripped"_q);
	} });
	append({ u"sse_multi_line_data_and_event_type"_q, [] {
		auto failure = CheckSame(
			"data: a\ndata: b\n\n",
			{ { "", "a\nb" } },
			u"multi-line data"_q);
		if (!failure.isEmpty()) {
			return failure;
		}
		return CheckSame(
			"event: error\ndata: {\"x\":1}\n\n",
			{ { "error", "{\"x\":1}" } },
			u"event type kept"_q);
	} });
	append({ u"sse_missing_final_empty_line"_q, [] {
		auto failure = CheckSame(
			"event: message\ndata: a",
			{ { "message", "a" } },
			u"no line ending at all"_q);
		if (!failure.isEmpty()) {
			return failure;
		}
		return CheckSame(
			"data: a\ndata: b\n\ndata: c",
			{ { "", "a\nb" }, { "", "c" } },
			u"last event without empty line"_q);
	} });
	append({ u"sse_empty_lines_without_data_ignored"_q, [] {
		return CheckSame(
			"\n\n\n\n",
			{},
			u"only empty lines"_q);
	} });
	append({ u"sse_data_line_without_colon"_q, [] {
		auto failure = CheckSame(
			"data\n\n",
			{ { "", "" } },
			u"bare data line"_q);
		if (!failure.isEmpty()) {
			return failure;
		}
		return CheckSame(
			"data\ndata: a\n\n",
			{ { "", "\na" } },
			u"bare data line joined"_q);
	} });
	append({ u"sse_cr_newline_split_across_chunks"_q, [] {
		auto parser = SseParser();
		auto events = parser.feed("data: a\r");
		auto more = parser.feed("\n\ndata: b\n");
		events.insert(events.end(), more.begin(), more.end());
		auto last = parser.finish();
		events.insert(events.end(), last.begin(), last.end());
		const auto expected = std::vector<SseEvent>{
			{ "", "a" },
			{ "", "b" },
		};
		if (Describe(events) != Describe(expected)) {
			return u"split cr lf: got %1, expected %2"_q
				.arg(Describe(events), Describe(expected));
		}
		return QString();
	} });
	return checks;
}

} // namespace Agent::Tests
