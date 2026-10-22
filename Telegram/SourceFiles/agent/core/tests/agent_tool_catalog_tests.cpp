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
				u"1, 0, 3"_q);
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
			if (const auto error = Same(limited, u"1, 0"_q); !error.isEmpty()) {
				return error;
			}
			if (const auto error = Same(
				RankToolDocuments(u"messages"_q, SampleDocuments(), 1),
				u"1"_q); !error.isEmpty()) {
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
	};
}

} // namespace Agent::Tests
