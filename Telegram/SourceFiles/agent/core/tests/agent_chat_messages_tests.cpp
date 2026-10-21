/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_chat_messages.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] ToolUseBlock Call(const QString &id, const QString &name) {
	return ToolUseBlock{ .id = id, .name = name };
}

[[nodiscard]] ToolResultBlock Result(const QString &id) {
	return ToolResultBlock{ .toolUseId = id, .content = u"ok"_q };
}

[[nodiscard]] Message Text(Role role, const QString &text) {
	return Message{ .role = role, .blocks = { TextBlock{ text } } };
}

} // namespace

std::vector<Check> ChatMessagesChecks() {
	return {
		{ u"chat_messages_steps_join_the_answer"_q, [] {
			const auto messages = std::vector<Message>{
				Text(Role::User, u"hi"_q),
				Message{ Role::Assistant, { Call(u"t1"_q, u"read"_q) } },
				Message{ Role::User, { Result(u"t1"_q) } },
				Text(Role::Assistant, u"done"_q),
			};
			const auto list = BuildChatMessages(BuildFeed(messages, 0));
			if (list.size() != 2) {
				return u"expected the user message and one answer"_q;
			} else if (list[0].kind != ChatMessageKind::User
				|| list[0].text != u"hi"_q) {
				return u"the first message must be the user's"_q;
			} else if (list[1].kind != ChatMessageKind::Agent
				|| list[1].text != u"done"_q
				|| list[1].steps.size() != 1) {
				return u"the steps must sit in the answer bubble"_q;
			} else if (list[1].key != u"a1"_q) {
				return u"the answer is keyed by its first step"_q;
			}
			return QString();
		} },
		{ u"chat_messages_steps_after_text_start_a_bubble"_q, [] {
			const auto messages = std::vector<Message>{
				Text(Role::User, u"hi"_q),
				Message{ Role::Assistant, {
					TextBlock{ u"let me look"_q },
					Call(u"t1"_q, u"read"_q),
				} },
				Message{ Role::User, { Result(u"t1"_q) } },
				Text(Role::Assistant, u"found it"_q),
			};
			const auto list = BuildChatMessages(BuildFeed(messages, 0));
			if (list.size() != 3) {
				return u"expected two agent bubbles"_q;
			} else if (list[1].text != u"let me look"_q
				|| !list[1].steps.empty()) {
				return u"the first bubble holds only the text"_q;
			} else if (list[2].steps.size() != 1
				|| list[2].text != u"found it"_q) {
				return u"the second bubble holds the steps and the answer"_q;
			} else if (list[1].key == list[2].key) {
				return u"bubbles from one message need distinct keys"_q;
			}
			return QString();
		} },
		{ u"chat_messages_cards_split_bubbles"_q, [] {
			auto card = Card();
			card.toolUseId = u"c1"_q;
			const auto messages = std::vector<Message>{
				Text(Role::User, u"send"_q),
				Message{ Role::Assistant, {
					Call(u"t1"_q, u"read"_q),
					Call(u"c1"_q, u"send_message"_q),
				} },
				Message{ Role::User, { Result(u"t1"_q), Result(u"c1"_q) } },
				Text(Role::Assistant, u"sent"_q),
			};
			const auto list = BuildChatMessages(
				BuildFeed(messages, 0, { card }));
			if (list.size() != 4) {
				return u"expected user, steps, card and answer"_q;
			} else if (list[1].kind != ChatMessageKind::Agent
				|| !list[1].text.isEmpty()
				|| list[1].steps.size() != 1) {
				return u"steps before a card get their own bubble"_q;
			} else if (list[2].kind != ChatMessageKind::Card
				|| !list[2].card
				|| !list[2].card->result) {
				return u"the card keeps its call and result"_q;
			} else if (list[3].text != u"sent"_q) {
				return u"the answer follows the card"_q;
			}
			return QString();
		} },
		{ u"chat_messages_actions_join_the_answer"_q, [] {
			const auto messages = std::vector<Message>{
				Text(Role::User, u"mute"_q),
				Message{ Role::Assistant, { Call(u"m1"_q, u"mute"_q) } },
				Message{ Role::User, { Result(u"m1"_q) } },
				Text(Role::Assistant, u"muted"_q),
			};
			const auto list = BuildChatMessages(BuildFeed(
				messages,
				0,
				{},
				{ u"mute"_q }));
			if (list.size() != 2
				|| list[1].actions.size() != 1
				|| list[1].text != u"muted"_q) {
				return u"actions belong to the answer bubble"_q;
			}
			return QString();
		} },
		{ u"chat_messages_summary"_q, [] {
			const auto messages = std::vector<Message>{
				Text(Role::User, u"one"_q),
				Text(Role::Assistant, u"two"_q),
				Text(Role::User, u"three"_q),
			};
			const auto list = BuildChatMessages(BuildFeed(messages, 2));
			if (list.size() != 4 || list[2].kind != ChatMessageKind::Summary) {
				return u"the summary sits where the context was cut"_q;
			}
			return QString();
		} },
		{ u"chat_messages_result_after_thinking"_q, [] {
			const auto messages = std::vector<Message>{
				Text(Role::User, u"q"_q),
				Message{ Role::Assistant, {
					ThinkingBlock{ u"first"_q },
					Call(u"t1"_q, u"read"_q),
					ThinkingBlock{ u"second"_q },
				} },
				Message{ Role::User, { Result(u"t1"_q) } },
			};
			const auto list = BuildChatMessages(BuildFeed(messages, 0));
			if (list.size() != 2 || list[1].steps.size() != 3) {
				return u"thinking around a tool must stay steps"_q;
			}
			const auto tool = std::get_if<FeedTool>(&list[1].steps[1]);
			if (!tool || !tool->result) {
				return u"the result must reach its tool"_q;
			}
			return QString();
		} },
		{ u"chat_messages_live_tail"_q, [] {
			auto tail = LiveTail();
			tail.apply(ThinkingDelta{ u"hm"_q });
			tail.apply(ToolUseStarted{ u"t1"_q, u"read"_q });
			auto call = Call(u"t1"_q, u"read"_q);
			call.input[u"chat"_q] = u"x"_q;
			tail.apply(ToolCallRequested{ call });
			tail.apply(ToolCallFinished{ Result(u"t1"_q) });
			tail.apply(TextDelta{ u"an"_q });
			tail.apply(TextDelta{ u"swer"_q });
			const auto messages = tail.messages();
			if (messages.size() != 2) {
				return u"the tail holds the answer and the results"_q;
			}
			const auto &blocks = messages[0].blocks;
			if (blocks.size() != 3
				|| !std::holds_alternative<ThinkingBlock>(blocks[0])
				|| std::get<ToolUseBlock>(blocks[1]).input.isEmpty()
				|| std::get<TextBlock>(blocks[2]).text != u"answer"_q) {
				return u"unexpected tail blocks"_q;
			}
			auto all = std::vector<Message>{ Text(Role::User, u"q"_q) };
			all.insert(end(all), begin(messages), end(messages));
			const auto list = BuildChatMessages(BuildFeed(all, 0));
			if (list.size() != 2
				|| list[1].text != u"answer"_q
				|| list[1].steps.size() != 2) {
				return u"the tail must build into one answer bubble"_q;
			}
			tail.clear();
			return tail.empty() ? QString() : u"clear must empty"_q;
		} },
		{ u"chat_messages_turn_end_ends_bubble"_q, [] {
			const auto messages = std::vector<Message>{
				Text(Role::User, u"hi"_q),
				Text(Role::Assistant, u"answer"_q),
				Text(Role::User, u"again"_q),
				Message{ Role::Assistant, { ThinkingBlock{ u"plan"_q } } },
			};
			const auto turnEnds = std::vector<TurnEndMark>{
				{ 2, TurnEnd::Interrupted },
			};
			const auto list = BuildChatMessages(
				BuildFeed(messages, 0, {}, {}, turnEnds));
			if (list.size() != 5) {
				return u"expected the user, answer, turn end, user, steps"_q;
			} else if (list[0].kind != ChatMessageKind::User
				|| list[0].text != u"hi"_q) {
				return u"the first message must be the user's"_q;
			} else if (list[1].kind != ChatMessageKind::Agent
				|| list[1].text != u"answer"_q) {
				return u"the answer must be the second message"_q;
			} else if (list[2].kind != ChatMessageKind::TurnEnd
				|| list[2].index != 1
				|| list[2].turnEnd != TurnEnd::Interrupted
				|| list[2].key != u"e1"_q) {
				return u"the turn end must be the third message"_q;
			} else if (list[3].kind != ChatMessageKind::User
				|| list[3].text != u"again"_q) {
				return u"the user message must follow the turn end"_q;
			} else if (list[4].kind != ChatMessageKind::Agent
				|| list[4].steps.size() != 1
				|| list[4].key != u"a3"_q) {
				return u"the steps after the turn end must start a bubble"_q;
			}
			return QString();
		} },
		{ u"chat_messages_user_replies_copy"_q, [] {
			const auto messages = std::vector<Message>{
				Message{
					.role = Role::User,
					.blocks = { TextBlock{ u"reply text"_q } },
					.replyTo = u"a0"_q,
					.replyQuote = u"some quote"_q,
				},
				Text(Role::Assistant, u"done"_q),
			};
			const auto list = BuildChatMessages(BuildFeed(messages, 0));
			if (list.size() != 2) {
				return u"expected the user message and the answer"_q;
			} else if (list[0].kind != ChatMessageKind::User
				|| list[0].replyTo != u"a0"_q
				|| list[0].replyQuote != u"some quote"_q) {
				return u"the reply must be copied to the user message"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
