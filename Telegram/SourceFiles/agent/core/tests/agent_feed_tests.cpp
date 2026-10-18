/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_feed.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] Message UserText(const QString &text) {
	return Message{ Role::User, { TextBlock{ text, false } } };
}

[[nodiscard]] QString DumpFeed(const std::vector<FeedItem> &feed) {
	auto result = QString();
	for (const auto &item : feed) {
		std::visit([&](const auto &value) {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, FeedUser>) {
				result += u"user("_q + QString::number(value.index)
					+ u"): '"_q + value.text + u"' "_q;
			} else if constexpr (std::is_same_v<T, FeedAssistant>) {
				result += u"assistant("_q + QString::number(value.index)
					+ u"): '"_q + value.text + u"' "_q;
			} else if constexpr (std::is_same_v<T, FeedThinking>) {
				result += u"thinking("_q + QString::number(value.index)
					+ u"): '"_q + value.text + u"' "_q;
			} else if constexpr (std::is_same_v<T, FeedTool>) {
				result += u"tool("_q + QString::number(value.index)
					+ u"): '"_q + value.call.id + u"' "_q;
			} else if constexpr (std::is_same_v<T, FeedSummary>) {
				result += u"summary("_q + QString::number(value.until)
					+ u") "_q;
			}
		}, item);
	}
	return result;
}

} // namespace

std::vector<Check> FeedChecks() {
	return {
		{ u"feed_user_text_and_hidden_hint"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::User, {
					TextBlock{ u"Hello"_q, false },
					TextBlock{ u"hint"_q, true },
				} },
			}, 0);
			if (feed.size() != 1) {
				return u"expected one item, got: "_q + DumpFeed(feed);
			}
			const auto user = std::get_if<FeedUser>(&feed[0]);
			if (!user) {
				return u"expected FeedUser, got: "_q + DumpFeed(feed);
			}
			if (user->index != 0 || user->text != u"Hello"_q) {
				return u"unexpected user text: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_user_hidden_only"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::User, { TextBlock{ u"hint"_q, true } } },
			}, 0);
			if (!feed.empty()) {
				return u"expected no items, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_assistant_two_adjacent_text"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::Assistant, {
					TextBlock{ u"one"_q, false },
					TextBlock{ u"two"_q, false },
				} },
			}, 0);
			if (feed.size() != 1) {
				return u"expected one item, got: "_q + DumpFeed(feed);
			}
			const auto assistant = std::get_if<FeedAssistant>(&feed[0]);
			if (!assistant) {
				return u"expected FeedAssistant, got: "_q + DumpFeed(feed);
			}
			if (assistant->text != u"one\n\ntwo"_q) {
				return u"unexpected assistant text: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_assistant_text_tool_text"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::Assistant, {
					TextBlock{ u"before"_q, false },
					ToolUseBlock{ u"call1"_q, u"tool"_q, {}, QString(), {} },
					TextBlock{ u"after"_q, false },
				} },
			}, 0);
			if (feed.size() != 3) {
				return u"expected three items, got: "_q + DumpFeed(feed);
			}
			if (!std::get_if<FeedAssistant>(&feed[0])
				|| !std::get_if<FeedTool>(&feed[1])
				|| !std::get_if<FeedAssistant>(&feed[2])) {
				return u"unexpected item kinds, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_thinking_empty_and_non_empty"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::Assistant, {
					ThinkingBlock{ QString(), QByteArray("sign1") },
					TextBlock{ u"answer"_q, false },
					ThinkingBlock{ u"reasoning"_q, QByteArray("sign2") },
				} },
			}, 0);
			auto seen = 0;
			for (const auto &item : feed) {
				if (const auto thinking = std::get_if<FeedThinking>(&item)) {
					if (thinking->text != u"reasoning"_q) {
						return u"unexpected thinking: "_q + DumpFeed(feed);
					}
					++seen;
				}
			}
			if (seen != 1) {
				return u"expected one thinking item, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_tool_results_across_messages"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::Assistant, {
					ToolUseBlock{ u"call1"_q, u"tool"_q, {}, QString(), {} },
				} },
				Message{ Role::User, {
					ToolResultBlock{ u"call1"_q, u"result text"_q, false },
				} },
			}, 0);
			if (feed.size() != 1) {
				return u"expected one item, got: "_q + DumpFeed(feed);
			}
			const auto tool = std::get_if<FeedTool>(&feed[0]);
			if (!tool || !tool->result.has_value()) {
				return u"expected attached result, got: "_q + DumpFeed(feed);
			}
			if (tool->result->content != u"result text"_q) {
				return u"unexpected result content, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_parallel_tool_uses_reverse_results"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::Assistant, {
					ToolUseBlock{ u"call1"_q, u"tool"_q, {}, QString(), {} },
					ToolUseBlock{ u"call2"_q, u"tool"_q, {}, QString(), {} },
				} },
				Message{ Role::User, {
					ToolResultBlock{ u"call2"_q, u"second"_q, false },
					ToolResultBlock{ u"call1"_q, u"first"_q, false },
				} },
			}, 0);
			if (feed.size() != 2) {
				return u"expected two items, got: "_q + DumpFeed(feed);
			}
			const auto first = std::get_if<FeedTool>(&feed[0]);
			const auto second = std::get_if<FeedTool>(&feed[1]);
			if (!first || !second) {
				return u"expected two tool items, got: "_q + DumpFeed(feed);
			}
			if (first->call.id != u"call1"_q || second->call.id != u"call2"_q) {
				return u"unexpected tool order, got: "_q + DumpFeed(feed);
			}
			if (!first->result.has_value() || !second->result.has_value()) {
				return u"expected both results attached, got: "_q
					+ DumpFeed(feed);
			}
			if (first->result->content != u"first"_q
				|| second->result->content != u"second"_q) {
				return u"unexpected result matching, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_unmatched_result"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::Assistant, {
					ToolUseBlock{ u"call1"_q, u"tool"_q, {}, QString(), {} },
				} },
				Message{ Role::User, {
					ToolResultBlock{ u"unknown"_q, u"orphan"_q, false },
				} },
			}, 0);
			if (feed.size() != 1) {
				return u"expected one item, got: "_q + DumpFeed(feed);
			}
			const auto tool = std::get_if<FeedTool>(&feed[0]);
			if (!tool || tool->result.has_value()) {
				return u"expected no result, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_images_skipped"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::User, {
					ImageBlock{ QByteArray("img"), u"image/png"_q },
					TextBlock{ u"caption"_q, false },
					ImageBlock{ QByteArray(), QString() },
				} },
			}, 0);
			if (feed.size() != 1) {
				return u"expected one item, got: "_q + DumpFeed(feed);
			}
			const auto user = std::get_if<FeedUser>(&feed[0]);
			if (!user || user->text != u"caption"_q) {
				return u"unexpected user text, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_summary_at_start"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::User, { TextBlock{ u"hidden"_q, true } } },
				UserText(u"second"_q),
			}, 1);
			if (feed.size() != 2) {
				return u"expected two items, got: "_q + DumpFeed(feed);
			}
			const auto summary = std::get_if<FeedSummary>(&feed[0]);
			if (!summary || summary->until != 1) {
				return u"expected summary first, got: "_q + DumpFeed(feed);
			}
			const auto user = std::get_if<FeedUser>(&feed[1]);
			if (!user || user->text != u"second"_q) {
				return u"expected second text, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_summary_in_middle"_q, [] {
			const auto feed = BuildFeed({
				UserText(u"first"_q),
				UserText(u"second"_q),
				UserText(u"third"_q),
			}, 2);
			if (feed.size() != 4) {
				return u"expected four items, got: "_q + DumpFeed(feed);
			}
			if (!std::get_if<FeedUser>(&feed[0])
				|| !std::get_if<FeedUser>(&feed[1])) {
				return u"expected two users first, got: "_q + DumpFeed(feed);
			}
			const auto summary = std::get_if<FeedSummary>(&feed[2]);
			if (!summary || summary->until != 2) {
				return u"expected summary third, got: "_q + DumpFeed(feed);
			}
			const auto user = std::get_if<FeedUser>(&feed[3]);
			if (!user || user->text != u"third"_q) {
				return u"expected third text, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
		{ u"feed_summary_at_end"_q, [] {
			const auto feed = BuildFeed({
				UserText(u"first"_q),
				UserText(u"second"_q),
			}, 2);
			if (feed.size() != 3) {
				return u"expected three items, got: "_q + DumpFeed(feed);
			}
			if (!std::get_if<FeedUser>(&feed[0])
				|| !std::get_if<FeedUser>(&feed[1])) {
				return u"expected two texts, got: "_q + DumpFeed(feed);
			}
			const auto summary = std::get_if<FeedSummary>(&feed[2]);
			if (!summary || summary->until != 2) {
				return u"expected summary last, got: "_q + DumpFeed(feed);
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
