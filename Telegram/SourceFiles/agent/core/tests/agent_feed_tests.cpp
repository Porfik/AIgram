/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_feed.h"
#include "base/variant.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] Message UserText(const QString &text) {
	return Message{ Role::User, { TextBlock{ text, false } } };
}

[[nodiscard]] QString DumpStep(const FeedStep &step) {
	return v::match(step, [](const FeedThinking &value) {
		return u"thinking("_q + QString::number(value.index)
			+ u"): '"_q + value.text + u"' "_q;
	}, [](const FeedTool &value) {
		return u"tool("_q + QString::number(value.index)
			+ u"): '"_q + value.call.id + u"'"_q
			+ (value.result ? (u"='"_q + value.result->content + u"'"_q)
				: QString())
			+ u" "_q;
	});
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
			} else if constexpr (std::is_same_v<T, FeedSteps>) {
				result += u"steps("_q + QString::number(value.index)
					+ u")[ "_q;
				for (const auto &step : value.steps) {
					result += DumpStep(step);
				}
				result += u"] "_q;
			} else if constexpr (std::is_same_v<T, FeedSummary>) {
				result += u"summary("_q + QString::number(value.until)
					+ u") "_q;
			} else if constexpr (std::is_same_v<T, FeedActions>) {
				result += u"actions("_q + QString::number(value.index)
					+ u")[ "_q;
				for (const auto &action : value.actions) {
					result += DumpStep(FeedStep(action));
				}
				result += u"] "_q;
			} else if constexpr (std::is_same_v<T, FeedCard>) {
				result += u"card("_q + QString::number(value.index)
					+ u"): '"_q + value.call.id + u"'"_q
					+ (value.result
						? (u"='"_q + value.result->content + u"'"_q)
						: QString())
					+ u" "_q;
			}
		}, item);
	}
	return result.trimmed();
}

[[nodiscard]] QString Expect(
		const std::vector<FeedItem> &feed,
		const QString &expected) {
	const auto dump = DumpFeed(feed);
	return (dump == expected)
		? QString()
		: (u"expected: "_q + expected + u"\ngot: "_q + dump);
}

[[nodiscard]] ToolUseBlock Call(const QString &id) {
	return ToolUseBlock{ id, u"tool"_q, {}, QString(), {} };
}

[[nodiscard]] ToolResultBlock Result(
		const QString &id,
		const QString &content) {
	return ToolResultBlock{ id, content, false };
}

[[nodiscard]] ToolUseBlock Action(const QString &id, const QString &name) {
	return ToolUseBlock{ id, name, {}, QString(), {} };
}

[[nodiscard]] Card HandoffCard(const QString &id) {
	auto card = Card();
	card.toolUseId = id;
	card.kind = CardKind::Handoff;
	card.status = CardStatus::Done;
	return card;
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
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					TextBlock{ u"before"_q, false },
					Call(u"call1"_q),
					TextBlock{ u"after"_q, false },
				} },
			}, 0), u"assistant(0): 'before' "
				"steps(0)[ tool(0): 'call1' ] "
				"assistant(0): 'after'"_q);
		} },
		{ u"feed_thinking_empty_and_non_empty"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					ThinkingBlock{ QString(), QByteArray("sign1") },
					TextBlock{ u"answer"_q, false },
					ThinkingBlock{ u"reasoning"_q, QByteArray("sign2") },
				} },
			}, 0), u"assistant(0): 'answer' "
				"steps(0)[ thinking(0): 'reasoning' ]"_q);
		} },
		{ u"feed_blank_text_keeps_the_run"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					ThinkingBlock{ u"plan"_q, QByteArray("sign1") },
					TextBlock{ u" \n"_q, false },
					Call(u"call1"_q),
				} },
			}, 0), u"steps(0)[ thinking(0): 'plan' tool(0): 'call1' ]"_q);
		} },
		{ u"feed_tool_results_across_messages"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, { Call(u"call1"_q) } },
				Message{ Role::User, {
					Result(u"call1"_q, u"result text"_q),
				} },
			}, 0), u"steps(0)[ tool(0): 'call1'='result text' ]"_q);
		} },
		{ u"feed_parallel_tool_uses_reverse_results"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					Call(u"call1"_q),
					Call(u"call2"_q),
				} },
				Message{ Role::User, {
					Result(u"call2"_q, u"second"_q),
					Result(u"call1"_q, u"first"_q),
				} },
			}, 0), u"steps(0)[ tool(0): 'call1'='first' "
				"tool(0): 'call2'='second' ]"_q);
		} },
		{ u"feed_unmatched_result"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, { Call(u"call1"_q) } },
				Message{ Role::User, {
					Result(u"unknown"_q, u"orphan"_q),
				} },
			}, 0), u"steps(0)[ tool(0): 'call1' ]"_q);
		} },
		{ u"feed_steps_span_tool_rounds"_q, [] {
			return Expect(BuildFeed({
				UserText(u"go"_q),
				Message{ Role::Assistant, {
					ThinkingBlock{ u"plan"_q, QByteArray() },
					Call(u"call1"_q),
				} },
				Message{ Role::User, { Result(u"call1"_q, u"one"_q) } },
				Message{ Role::Assistant, { Call(u"call2"_q) } },
				Message{ Role::User, { Result(u"call2"_q, u"two"_q) } },
				Message{ Role::Assistant, {
					TextBlock{ u"done"_q, false },
				} },
			}, 0), u"user(0): 'go' "
				"steps(1)[ thinking(1): 'plan' tool(1): 'call1'='one' "
				"tool(3): 'call2'='two' ] "
				"assistant(5): 'done'"_q);
		} },
		{ u"feed_steps_split_by_text"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					Call(u"call1"_q),
				} },
				Message{ Role::User, { Result(u"call1"_q, u"one"_q) } },
				Message{ Role::Assistant, {
					TextBlock{ u"still reading"_q, false },
					Call(u"call2"_q),
				} },
				UserText(u"next"_q),
				Message{ Role::Assistant, { Call(u"call3"_q) } },
			}, 0), u"steps(0)[ tool(0): 'call1'='one' ] "
				"assistant(2): 'still reading' "
				"steps(2)[ tool(2): 'call2' ] "
				"user(3): 'next' "
				"steps(4)[ tool(4): 'call3' ]"_q);
		} },
		{ u"feed_steps_split_by_summary"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, { Call(u"call1"_q) } },
				Message{ Role::Assistant, { Call(u"call2"_q) } },
			}, 1), u"steps(0)[ tool(0): 'call1' ] "
				"summary(1) "
				"steps(1)[ tool(1): 'call2' ]"_q);
		} },
		{ u"feed_hidden_text_keeps_steps"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, { Call(u"call1"_q) } },
				Message{ Role::User, {
					Result(u"call1"_q, u"one"_q),
					TextBlock{ u"hint"_q, true },
				} },
				Message{ Role::Assistant, { Call(u"call2"_q) } },
			}, 0), u"steps(0)[ tool(0): 'call1'='one' "
				"tool(2): 'call2' ]"_q);
		} },
		{ u"feed_images_skipped"_q, [] {
			const auto feed = BuildFeed({
				Message{ Role::User, {
					MediaBlock{
						.bytes = QByteArray("img"),
						.mimeType = u"image/png"_q,
					},
					TextBlock{ u"caption"_q, false },
					MediaBlock{},
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
		{ u"feed_card_ends_run"_q, [] {
			return Expect(BuildFeed({
				Message{ Role::Assistant, { Call(u"call1"_q) } },
				Message{ Role::User, { Result(u"call1"_q, u"ok"_q) } },
				Message{ Role::Assistant, { Call(u"call2"_q) } },
				Message{ Role::User, { Result(u"call2"_q, u"two"_q) } },
				Message{ Role::Assistant, {
					TextBlock{ u"done"_q, false },
				} },
			}, 0, { HandoffCard(u"call1"_q) }),
				u"card(0): 'call1'='ok' "
					"steps(2)[ tool(2): 'call2'='two' ] "
					"assistant(4): 'done'"_q);
		} },
		{ u"feed_actions_after_steps"_q, [] {
			const auto actions = base::flat_set<QString>{ u"set_mute"_q };
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					Call(u"call1"_q),
					Action(u"mute1"_q, u"set_mute"_q),
				} },
				Message{ Role::User, {
					Result(u"mute1"_q, u"two"_q),
					Result(u"call1"_q, u"one"_q),
				} },
				Message{ Role::Assistant, {
					TextBlock{ u"done"_q, false },
				} },
			}, 0, {}, actions),
				u"steps(0)[ tool(0): 'call1'='one' ] "
					"actions(0)[ tool(0): 'mute1'='two' ] "
					"assistant(2): 'done'"_q);
		} },
		{ u"feed_actions_only_run"_q, [] {
			const auto actions = base::flat_set<QString>{ u"set_mute"_q };
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					Action(u"mute1"_q, u"set_mute"_q),
				} },
				Message{ Role::User, {
					Result(u"mute1"_q, u"ok"_q),
				} },
			}, 0, {}, actions),
				u"actions(0)[ tool(0): 'mute1'='ok' ]"_q);
		} },
		{ u"feed_summary_between_actions_and_text"_q, [] {
			const auto actions = base::flat_set<QString>{ u"set_mute"_q };
			return Expect(BuildFeed({
				Message{ Role::Assistant, {
					Action(u"mute1"_q, u"set_mute"_q),
				} },
				Message{ Role::User, {
					Result(u"mute1"_q, u"ok"_q),
				} },
				UserText(u"next"_q),
			}, 2, {}, actions),
				u"actions(0)[ tool(0): 'mute1'='ok' ] "
					"summary(2) user(2): 'next'"_q);
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
