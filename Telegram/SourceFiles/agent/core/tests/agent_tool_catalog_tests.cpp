/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_tool_catalog.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] QString Same(const QString &result, const QString &expected) {
	return (result == expected)
		? QString()
		: u"got:\n%1\nexpected:\n%2"_q.arg(result, expected);
}

[[nodiscard]] QString Same(
		const std::vector<int> &result,
		const QString &expected) {
	auto joined = QString();
	for (const auto index : result) {
		if (!joined.isEmpty()) {
			joined += u", "_q;
		}
		joined += QString::number(index);
	}
	return Same(joined, expected);
}

[[nodiscard]] QString Same(
		const std::vector<QString> &result,
		const QString &expected) {
	auto joined = QString();
	for (const auto &name : result) {
		if (!joined.isEmpty()) {
			joined += u", "_q;
		}
		joined += name;
	}
	return Same(joined, expected);
}

[[nodiscard]] std::vector<CatalogEntry> CatalogEntries() {
	return {
		{
			u"search_messages"_q,
			u"Search messages in one chat by a query"_q,
			QString(),
			CatalogSection::Read },
		{
			u"read_history"_q,
			u"Read messages around a date"_q,
			u"general"_q,
			CatalogSection::Read },
		{
			u"mute_chat"_q,
			u"Mute a chat"_q,
			u"chat_state"_q,
			CatalogSection::ChatState },
		{
			u"custom_tool"_q,
			QString(),
			QString(),
			CatalogSection::Other },
	};
}

[[nodiscard]] std::vector<CatalogGroup> CatalogGroups() {
	return {
		{ u"general"_q, u"reading chats and settings"_q },
		{ u"chat_state"_q, u"mute, archive, pin and read state"_q },
	};
}

[[nodiscard]] std::vector<ToolDocument> SampleDocuments() {
	return {
		{
			u"search_messages"_q,
			u"Search messages in one chat by a query."_q },
		{
			u"read_history"_q,
			u"Read messages around a message or from a date."_q },
		{
			u"list_chats"_q,
			u"List the chats of the account."_q },
		{
			u"transcribe_voice"_q,
			u"Transcribe a voice message to text."_q },
	};
}

} // namespace

std::vector<Check> ToolCatalogChecks() {
	return {
		{ u"tool_catalog_full"_q, [] {
			return Same(
				FormatToolCatalog(CatalogEntries(), CatalogGroups()),
				u"## Find and read\n"
				u"- search_messages: Search messages in one chat by a query\n"
				u"- read_history: Read messages around a date"
				u" [group general]\n"
				u"## Chat list\n"
				u"- mute_chat: Mute a chat [group chat_state]\n"
				u"## Other\n"
				u"- custom_tool\n"
				u"Groups for load_tools:\n"
				u"- general: reading chats and settings\n"
				u"- chat_state: mute, archive, pin and read state"_q);
		} },
		{ u"tool_catalog_empty"_q, [] {
			return Same(FormatToolCatalog({}, {}), QString());
		} },
		{ u"tool_catalog_groups_only"_q, [] {
			return Same(
				FormatToolCatalog({}, CatalogGroups()),
				u"Groups for load_tools:\n"
				u"- general: reading chats and settings\n"
				u"- chat_state: mute, archive, pin and read state"_q);
		} },
		{ u"tool_catalog_section_order"_q, [] {
			const auto entries = std::vector<CatalogEntry>{
				{
					u"custom_tool"_q,
					u"A custom tool"_q,
					QString(),
					CatalogSection::Other },
				{
					u"mute_chat"_q,
					QString(),
					QString(),
					CatalogSection::ChatState },
				{
					u"search_messages"_q,
					QString(),
					QString(),
					CatalogSection::Read },
			};
			return Same(
				FormatToolCatalog(entries, {}),
				u"## Find and read\n"
				u"- search_messages\n"
				u"## Chat list\n"
				u"- mute_chat\n"
				u"## Other\n"
				u"- custom_tool: A custom tool"_q);
		} },
		{ u"tool_catalog_skips_empty_sections"_q, [] {
			const auto entries = std::vector<CatalogEntry>{
				{
					u"search_messages"_q,
					QString(),
					QString(),
					CatalogSection::Read },
				{
					u"memory_add"_q,
					QString(),
					QString(),
					CatalogSection::Memory },
			};
			return Same(
				FormatToolCatalog(entries, {}),
				u"## Find and read\n"
				u"- search_messages\n"
				u"## Memory\n"
				u"- memory_add"_q);
		} },
		{ u"tool_catalog_no_trailing_newline"_q, [] {
			const auto result = FormatToolCatalog(
				CatalogEntries(),
				CatalogGroups());
			return (!result.isEmpty() && !result.endsWith(u'\n'))
				? QString()
				: u"catalog must not end with a newline"_q;
		} },
		{ u"rank_matches_one_tool"_q, [] {
			return Same(
				RankToolDocuments(u"transcribe voice"_q, SampleDocuments(), 10),
				u"3"_q);
		} },
		{ u"rank_plural_finds_singular"_q, [] {
			return Same(
				RankToolDocuments(u"messages"_q, SampleDocuments(), 10),
				u"0, 1, 3"_q);
		} },
		{ u"rank_camel_case_query"_q, [] {
			return Same(
				RankToolDocuments(
					u"searchMessages"_q,
					SampleDocuments(),
					10),
				u"0"_q);
		} },
		{ u"rank_rarest_term_rejects_common_word"_q, [] {
			const auto documents = std::vector<ToolDocument>{
				{ u"alpha_search"_q, u"Alpha one"_q },
				{ u"beta_search"_q, u"Beta two"_q },
			};
			return Same(
				RankToolDocuments(u"alpha search"_q, documents, 10),
				u"0"_q);
		} },
		{ u"rank_half_rule_long_query"_q, [] {
			const auto documents = std::vector<ToolDocument>{
				{ u"all_five"_q, u"alpha beta gamma delta epsilon"_q },
				{ u"two_only"_q, u"alpha beta"_q },
			};
			return Same(
				RankToolDocuments(
					u"alpha beta gamma delta epsilon"_q,
					documents,
					10),
				u"0"_q);
		} },
		{ u"rank_no_half_rule_below_four_terms"_q, [] {
			const auto documents = std::vector<ToolDocument>{
				{ u"first"_q, u"one two"_q },
				{ u"second"_q, u"three"_q },
				{ u"third"_q, u"one two"_q },
			};
			return Same(
				RankToolDocuments(u"one two three"_q, documents, 10),
				u"1"_q);
		} },
		{ u"rank_ties_in_document_order"_q, [] {
			const auto documents = std::vector<ToolDocument>{
				{ u"aa_tool"_q, u"alpha beta"_q },
				{ u"bb_tool"_q, u"alpha beta"_q },
			};
			return Same(
				RankToolDocuments(u"alpha"_q, documents, 10),
				u"0, 1"_q);
		} },
		{ u"rank_cyrillic_words"_q, [] {
			const auto documents = std::vector<ToolDocument>{
				{ u"list_chats"_q, u"List the chats of the account."_q },
				{
					u"история_чата"_q,
					u"Читать историю сообщений чата."_q },
			};
			return Same(
				RankToolDocuments(u"История чата"_q, documents, 10),
				u"1"_q);
		} },
		{ u"rank_limit_and_edge_cases"_q, [] {
			const auto limited = RankToolDocuments(
				u"messages"_q,
				SampleDocuments(),
				2);
			if (const auto error = Same(limited, u"0, 1"_q); !error.isEmpty()) {
				return error;
			}
			if (const auto error = Same(
				RankToolDocuments(u"messages"_q, SampleDocuments(), 1),
				u"0"_q); !error.isEmpty()) {
				return error;
			}
			if (!RankToolDocuments(u"messages"_q, SampleDocuments(), 0).empty()
				|| !RankToolDocuments(
					u"messages"_q,
					SampleDocuments(),
					-1).empty()) {
				return u"non-positive limit must return nothing"_q;
			}
			if (!RankToolDocuments(u""_q, SampleDocuments(), 10).empty()) {
				return u"empty query must return nothing"_q;
			}
			if (!RankToolDocuments(u"messages"_q, {}, 10).empty()) {
				return u"no documents must return nothing"_q;
			}
			return QString();
		} },
		{ u"nearest_separator_insensitive"_q, [] {
			const auto names = std::vector<QString>{
				u"read_history"_q,
				u"search_messages"_q };
			return Same(
				NearestToolNames(u"Search-Messages"_q, names, 10),
				u"search_messages"_q);
		} },
		{ u"nearest_distance_order"_q, [] {
			const auto names = std::vector<QString>{
				u"read_historyzzz"_q,
				u"read_historyz"_q,
				u"read_historyzz"_q };
			return Same(
				NearestToolNames(u"read_history"_q, names, 10),
				u"read_historyz, read_historyzz, read_historyzzz"_q);
		} },
		{ u"nearest_shared_word"_q, [] {
			const auto names = std::vector<QString>{
				u"read_history"_q,
				u"list_chats"_q,
				u"get_message"_q };
			return Same(
				NearestToolNames(u"get_history"_q, names, 10),
				u"read_history, get_message"_q);
		} },
		{ u"nearest_tier_order"_q, [] {
			const auto names = std::vector<QString>{
				u"read_history"_q,
				u"get_history_"_q };
			return Same(
				NearestToolNames(u"get_history"_q, names, 10),
				u"get_history_, read_history"_q);
		} },
		{ u"nearest_no_self_no_duplicates"_q, [] {
			const auto names = std::vector<QString>{
				u"read_history"_q,
				u"READ_HISTORY"_q,
				u"read_history"_q };
			return Same(
				NearestToolNames(u"read_history"_q, names, 10),
				QString());
		} },
		{ u"nearest_limit"_q, [] {
			const auto names = std::vector<QString>{
				u"get_history"_q,
				u"read_history"_q,
				u"get_message"_q,
				u"scan_history"_q };
			if (const auto error = Same(
				NearestToolNames(u"get_history"_q, names, 1),
				u"read_history"_q); !error.isEmpty()) {
				return error;
			}
			if (const auto error = Same(
				NearestToolNames(u"get_history"_q, names, 2),
				u"read_history, get_message"_q); !error.isEmpty()) {
				return error;
			}
			return Same(
				NearestToolNames(u"get_history"_q, names, 0),
				QString());
		} },
		{ u"snippet_short_unchanged"_q, [] {
			if (const auto error = Same(
				MatchSnippet(u"short text"_q, { u"zzz"_q }, 20),
				u"short text"_q); !error.isEmpty()) {
				return error;
			}
			if (const auto error = Same(
				MatchSnippet(u"exact"_q, {}, 5),
				u"exact"_q); !error.isEmpty()) {
				return error;
			}
			return Same(MatchSnippet(u"abc"_q, {}, 0), u"abc"_q);
		} },
		{ u"snippet_match_in_middle"_q, [] {
			const auto text = u"aaa bbb ccc ddd eee fff ggg hhh"_q;
			return Same(
				MatchSnippet(text, { u"eee"_q }, 16),
				u"…ddd eee fff…"_q);
		} },
		{ u"snippet_match_at_start"_q, [] {
			const auto text = u"aaa bbb ccc ddd eee fff ggg hhh"_q;
			return Same(
				MatchSnippet(text, { u"aaa"_q }, 10),
				u"aaa bbb…"_q);
		} },
		{ u"snippet_match_at_end"_q, [] {
			const auto text = u"aaa bbb ccc ddd eee fff ggg hhh"_q;
			return Same(
				MatchSnippet(text, { u"hhh"_q }, 12),
				u"…fff ggg hhh"_q);
		} },
		{ u"snippet_no_match"_q, [] {
			const auto text = u"aaa bbb ccc ddd eee fff ggg hhh"_q;
			return Same(
				MatchSnippet(text, { u"zzz"_q }, 10),
				u"aaa bbb…"_q);
		} },
		{ u"snippet_earliest_query_match"_q, [] {
			const auto text = u"aaa bbb ccc ddd eee fff ggg hhh"_q;
			return Same(
				MatchSnippet(text, { u"fff"_q, u"bbb"_q }, 10),
				u"…bbb…"_q);
		} },
		{ u"snippet_edges_stay_without_whitespace"_q, [] {
			const auto text = QString(30, 'x')
				+ u" needle "_q
				+ QString(60, 'y');
			return Same(
				MatchSnippet(text, { u"needle"_q }, 66),
				u"…"_q
					+ QString(21, 'x')
					+ u" needle "_q
					+ QString(37, 'y')
					+ u"…"_q);
		} },
		{ u"snippet_surrogate_pair_at_left_edge"_q, [] {
			return Same(
				MatchSnippet(
					u"a\U0001F600bcdefghij"_q,
					{ u"bcdefghij"_q },
					5),
				u"…bcde…"_q);
		} },
		{ u"snippet_surrogate_pair_at_right_edge"_q, [] {
			return Same(
				MatchSnippet(
					u"abcdefghij\U0001F600kl"_q,
					{ u"abcdefghij"_q },
					11),
				u"abcdefghij…"_q);
		} },
		{ u"snippet_cyrillic_case"_q, [] {
			return Same(
				MatchSnippet(
					u"Пример истории сообщений"_q,
					{ u"ИСТОРИИ"_q },
					10),
				u"…истории…"_q);
		} },
		{ u"snippet_latin_case"_q, [] {
			return Same(
				MatchSnippet(
					u"Example of message history"_q,
					{ u"HISTORY"_q },
					10),
				u"…history"_q);
		} },
		{ u"turn_context_full"_q, [] {
			auto context = TurnContext();
			context.now = QDateTime(QDate(2026, 10, 22), QTime(14, 30));
			context.zone = u"UTC+3"_q;
			context.loadedGroups = { u"chat"_q, u"memory"_q };
			context.topic = u"Project talk"_q;
			context.pendingCards = 2;
			return Same(
				FormatTurnContext(context),
				u"Now: 2026-10-22 14:30, Thursday, UTC+3.\n"
				u"Loaded tool groups: chat, memory.\n"
				u"The user has Project talk open.\n"
				u"Waiting for the user's decision: 2 cards."_q);
		} },
		{ u"turn_context_only_now"_q, [] {
			auto context = TurnContext();
			context.now = QDateTime(QDate(2026, 10, 22), QTime(14, 30));
			context.zone = u"UTC+3"_q;
			return Same(
				FormatTurnContext(context),
				u"Now: 2026-10-22 14:30, Thursday, UTC+3."_q);
		} },
		{ u"turn_context_zone_empty"_q, [] {
			auto context = TurnContext();
			context.now = QDateTime(QDate(2026, 10, 22), QTime(9, 5));
			return Same(
				FormatTurnContext(context),
				u"Now: 2026-10-22 09:05, Thursday."_q);
		} },
		{ u"turn_context_invalid_now"_q, [] {
			auto context = TurnContext();
			context.loadedGroups = { u"chat"_q };
			context.pendingCards = 1;
			return Same(
				FormatTurnContext(context),
				u"Loaded tool groups: chat.\n"
				u"Waiting for the user's decision: 1 card."_q);
		} },
		{ u"turn_context_cards"_q, [] {
			auto one = TurnContext();
			one.pendingCards = 1;
			if (const auto error = Same(
				FormatTurnContext(one),
				u"Waiting for the user's decision: 1 card."_q
			); !error.isEmpty()) {
				return error;
			}
			auto several = TurnContext();
			several.pendingCards = 5;
			if (const auto error = Same(
				FormatTurnContext(several),
				u"Waiting for the user's decision: 5 cards."_q
			); !error.isEmpty()) {
				return error;
			}
			auto zero = TurnContext();
			zero.pendingCards = 0;
			return Same(FormatTurnContext(zero), QString());
		} },
		{ u"turn_context_empty"_q, [] {
			return Same(FormatTurnContext(TurnContext()), QString());
		} },
	};
}

} // namespace Agent::Tests
