/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_journal.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] SessionRecord Record(const QString &id) {
	auto record = SessionRecord();
	record.info.id = id;
	record.info.title = u"title of "_q + id;
	record.info.scope = Scope::Account();
	record.info.task = u"task of "_q + id;
	return record;
}

[[nodiscard]] Message Assistant(
		const std::vector<Block> &blocks,
		qint64 date = 0) {
	return Message{ .role = Role::Assistant, .blocks = blocks, .date = date };
}

[[nodiscard]] Message User(
		const std::vector<Block> &blocks,
		qint64 date = 0) {
	return Message{ .role = Role::User, .blocks = blocks, .date = date };
}

[[nodiscard]] ToolUseBlock Call(const QString &id, const QString &name) {
	return ToolUseBlock{ .id = id, .name = name };
}

[[nodiscard]] ToolResultBlock Result(
		const QString &id,
		const QString &content,
		bool error = false) {
	return ToolResultBlock{
		.toolUseId = id,
		.content = content,
		.error = error,
	};
}

[[nodiscard]] Card MakeCard(
		const QString &toolUseId,
		CardStatus status,
		CardKind kind = CardKind::Send) {
	auto card = Card();
	card.toolUseId = toolUseId;
	card.kind = kind;
	card.status = status;
	return card;
}

} // namespace

std::vector<Check> JournalChecks() {
	return {
		{ u"journal_reversible_actions"_q, [] {
			auto record = Record(u"s1"_q);
			record.info.created = 100;
			record.messages = {
				User({ Result(u"r0"_q, u"old"_q) }),
				Assistant({ Call(u"a1"_q, u"mute"_q) }, 200),
				User({ Result(u"a1"_q, u"done"_q) }, 200),
				Assistant({ Call(u"r1"_q, u"read"_q) }, 300),
				User({ Result(u"r1"_q, u"text"_q) }, 300),
				Assistant({ Call(u"a2"_q, u"mute"_q) }, 400),
			};
			const auto list = BuildJournal(
				{ record },
				{ u"mute"_q },
				0);
			if (list.size() != 1) {
				return u"expected one reversible action"_q;
			}
			const auto &entry = list[0];
			if (entry.call.id != u"a1"_q
				|| !entry.result
				|| entry.result->content != u"done"_q) {
				return u"wrong action or result"_q;
			}
			if (entry.sessionId != u"s1"_q
				|| entry.sessionTitle != u"title of s1"_q
				|| entry.task != u"task of s1"_q
				|| entry.date != 200) {
				return u"entry fields must come from the record"_q;
			}
			return QString();
		} },
		{ u"journal_cards"_q, [] {
			auto record = Record(u"s1"_q);
			record.info.created = 100;
			record.cards = {
				MakeCard(u"c1"_q, CardStatus::Done),
				MakeCard(u"c2"_q, CardStatus::Drafted),
				MakeCard(u"c3"_q, CardStatus::Declined),
				MakeCard(u"c4"_q, CardStatus::Pending),
				MakeCard(u"c5"_q, CardStatus::Expired),
			};
			record.messages = {
				Assistant({
					Call(u"c1"_q, u"send_message"_q),
					Call(u"c2"_q, u"send_message"_q),
					Call(u"c3"_q, u"send_message"_q),
					Call(u"c4"_q, u"send_message"_q),
					Call(u"c5"_q, u"send_message"_q),
				}, 200),
				User({
					Result(u"c1"_q, u"ok"_q),
					Result(u"c2"_q, u"ok"_q),
					Result(u"c3"_q, u"ok"_q),
					Result(u"c4"_q, u"ok"_q),
					Result(u"c5"_q, u"ok"_q),
				}, 200),
			};
			const auto list = BuildJournal({ record }, {}, 0);
			if (list.size() != 2) {
				return u"expected done and drafted cards only"_q;
			}
			for (const auto &entry : list) {
				if (!entry.card) {
					return u"card entry must have the card"_q;
				}
				if (entry.failed) {
					return u"a card is never failed"_q;
				}
			}
			if (list[0].call.id != u"c2"_q
				|| list[0].card->status != CardStatus::Drafted) {
				return u"the newer drafted card must come first"_q;
			}
			if (list[1].call.id != u"c1"_q
				|| list[1].card->status != CardStatus::Done) {
				return u"the done card must come after"_q;
			}
			return QString();
		} },
		{ u"journal_failed_results"_q, [] {
			auto record = Record(u"s1"_q);
			record.info.created = 100;
			record.messages = {
				Assistant({
					Call(u"e1"_q, u"mute"_q),
					Call(u"f1"_q, u"mute"_q),
					Call(u"f2"_q, u"mute"_q),
					Call(u"f3"_q, u"mute"_q),
				}, 200),
				User({
					Result(u"e1"_q, u"crashed"_q, true),
					Result(u"f1"_q, u"{\"done\":[],\"failed\":[1]}"_q),
					Result(u"f2"_q, u"{\"done\":[1],\"failed\":[2]}"_q),
					Result(u"f3"_q, u"ok"_q),
				}, 200),
			};
			const auto list = BuildJournal(
				{ record },
				{ u"mute"_q },
				0);
			if (list.size() != 4) {
				return u"expected four actions"_q;
			}
			if (!list[3].failed) {
				return u"an error result must be failed"_q;
			}
			if (!list[2].failed) {
				return u"empty done with failed must be failed"_q;
			}
			if (list[1].failed) {
				return u"non-empty done must not be failed"_q;
			}
			if (list[0].failed) {
				return u"plain text must not be failed"_q;
			}
			return QString();
		} },
		{ u"journal_undone"_q, [] {
			auto record = Record(u"s1"_q);
			record.info.created = 100;
			record.undone = { u"a1"_q };
			record.messages = {
				Assistant({ Call(u"a1"_q, u"mute"_q) }, 200),
				User({ Result(u"a1"_q, u"ok"_q) }, 200),
			};
			const auto list = BuildJournal(
				{ record },
				{ u"mute"_q },
				0);
			if (list.size() != 1 || !list[0].undone) {
				return u"the undone action must be marked"_q;
			}
			return QString();
		} },
		{ u"journal_ordering"_q, [] {
			auto first = Record(u"s1"_q);
			first.info.created = 100;
			first.messages = {
				Assistant({ Call(u"a1"_q, u"mute"_q) }, 200),
				User({ Result(u"a1"_q, u"ok"_q) }, 200),
			};
			auto second = Record(u"s2"_q);
			second.info.created = 150;
			second.messages = {
				Assistant({ Call(u"a2"_q, u"mute"_q) }, 200),
				User({ Result(u"a2"_q, u"ok"_q) }, 200),
				Assistant({ Call(u"a3"_q, u"mute"_q) }, 300),
				User({ Result(u"a3"_q, u"ok"_q) }, 300),
			};
			const auto list = BuildJournal(
				{ first, second },
				{ u"mute"_q },
				0);
			if (list.size() != 3) {
				return u"expected three actions"_q;
			}
			if (list[0].call.id != u"a3"_q) {
				return u"the newest date must come first"_q;
			}
			if (list[1].call.id != u"a2"_q
				|| list[2].call.id != u"a1"_q) {
				return u"a tie must put the later input first"_q;
			}
			return QString();
		} },
		{ u"journal_limit"_q, [] {
			auto record = Record(u"s1"_q);
			record.info.created = 100;
			record.messages = {
				Assistant({ Call(u"a1"_q, u"mute"_q) }, 200),
				User({ Result(u"a1"_q, u"ok"_q) }, 200),
				Assistant({ Call(u"a2"_q, u"mute"_q) }, 300),
				User({ Result(u"a2"_q, u"ok"_q) }, 300),
			};
			const auto limited = BuildJournal(
				{ record },
				{ u"mute"_q },
				1);
			if (limited.size() != 1 || limited[0].call.id != u"a2"_q) {
				return u"the limit must keep the newest entry"_q;
			}
			const auto all = BuildJournal(
				{ record },
				{ u"mute"_q },
				-1);
			if (all.size() != 2) {
				return u"a non-positive limit must keep all entries"_q;
			}
			return QString();
		} },
		{ u"journal_date_fallback"_q, [] {
			auto record = Record(u"s1"_q);
			record.info.created = 555;
			record.messages = {
				Assistant({ Call(u"a1"_q, u"mute"_q) }, 0),
				User({ Result(u"a1"_q, u"ok"_q) }, 0),
			};
			const auto list = BuildJournal(
				{ record },
				{ u"mute"_q },
				0);
			if (list.size() != 1 || list[0].date != 555) {
				return u"zero message date must use info.created"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
