/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_navigation.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] SettingEntry Entry(
		QString id,
		QString title,
		QStringList keywords,
		QString path) {
	return SettingEntry{
		std::move(id),
		std::move(title),
		std::move(keywords),
		std::move(path),
		QString(),
		std::nullopt };
}

[[nodiscard]] std::vector<SettingEntry> SampleEntries() {
	return {
		Entry(
			u"notifications"_q,
			u"Notifications and sounds"_q,
			{ u"mute"_q, u"sound"_q },
			u"Settings/Notifications and sounds"_q),
		Entry(
			u"privacy"_q,
			u"Privacy and security"_q,
			{ u"password"_q, u"lock"_q },
			u"Settings/Privacy and security"_q),
		Entry(
			u"folders"_q,
			u"Chat folders"_q,
			{ u"archive"_q },
			u"Settings/Folders"_q),
		Entry(
			u"chats"_q,
			u"Chats"_q,
			{ u"mute chats"_q, u"notifications"_q },
			u"Settings/Chats"_q),
	};
}

[[nodiscard]] QString Ids(const std::vector<int> &indices) {
	auto result = QString();
	for (const auto index : indices) {
		if (!result.isEmpty()) {
			result += u", "_q;
		}
		result += QString::number(index);
	}
	return result;
}

} // namespace

std::vector<Check> NavigationChecks() {
	return {
		{ u"navigation_title_beats_keyword_beats_path"_q, [] {
			const auto entries = std::vector<SettingEntry>{
				Entry(u"path"_q, u"Main"_q, {}, u"Deep/Term"_q),
				Entry(u"keyword"_q, u"Main other"_q, { u"term"_q }, u"Other"_q),
				Entry(u"title"_q, u"Term here"_q, {}, u"Other"_q),
			};
			return (RankSettings(u"term"_q, entries, 10)
					== std::vector<int>{ 2, 1, 0 })
				? QString()
				: u"unexpected order: "_q
					+ Ids(RankSettings(u"term"_q, entries, 10));
		} },
		{ u"navigation_shorter_title_first"_q, [] {
			return (RankSettings(u"mute"_q, SampleEntries(), 10)
					== std::vector<int>{ 3, 0 })
				? QString()
				: u"unexpected order: "_q
					+ Ids(RankSettings(u"mute"_q, SampleEntries(), 10));
		} },
		{ u"navigation_case_insensitive_prefix"_q, [] {
			const auto entries = SampleEntries();
			const auto notif = RankSettings(u"NOTIF"_q, entries, 10);
			if (notif != std::vector<int>{ 0, 3 }) {
				return u"uppercase prefix: "_q + Ids(notif);
			}
			const auto infix = RankSettings(u"ications"_q, entries, 10);
			if (!infix.empty()) {
				return u"infix must not match: "_q + Ids(infix);
			}
			const auto full = RankSettings(u"notifications"_q, entries, 10);
			if (full != std::vector<int>{ 0, 3 }) {
				return u"full word: "_q + Ids(full);
			}
			return QString();
		} },
		{ u"navigation_words_across_fields"_q, [] {
			const auto entries = std::vector<SettingEntry>{
				Entry(
					u"phone"_q,
					u"Phone Number"_q,
					{},
					u"Settings/Privacy and security"_q),
				Entry(
					u"other"_q,
					u"Something else"_q,
					{ u"phone number"_q },
					u"Settings/Privacy"_q),
			};
			const auto ranked = RankSettings(u"privacy phone"_q, entries, 10);
			if (ranked != std::vector<int>{ 0, 1 }) {
				return u"unexpected order: "_q + Ids(ranked);
			}
			return QString();
		} },
		{ u"navigation_every_word_must_match_somewhere"_q, [] {
			const auto entries = SampleEntries();
			if (!RankSettings(u"notifications zzz"_q, entries, 10).empty()) {
				return u"a word with no match must reject the entry"_q;
			}
			const auto both = RankSettings(u"notifications mute"_q, entries, 10);
			if (both != std::vector<int>{ 0, 3 }) {
				return u"unexpected order: "_q + Ids(both);
			}
			return QString();
		} },
		{ u"navigation_entry_order_tiebreak"_q, [] {
			const auto entries = std::vector<SettingEntry>{
				Entry(u"first"_q, u"Same"_q, {}, QString()),
				Entry(u"second"_q, u"Same"_q, {}, QString()),
				Entry(u"third"_q, u"Other"_q, {}, QString()),
			};
			return (RankSettings(u"same"_q, entries, 10)
					== std::vector<int>{ 0, 1 })
				? QString()
				: u"unexpected order: "_q
					+ Ids(RankSettings(u"same"_q, entries, 10));
		} },
		{ u"navigation_empty_query_and_limit"_q, [] {
			const auto entries = SampleEntries();
			if (!RankSettings(QString(), entries, 10).empty()) {
				return u"empty query must return nothing"_q;
			}
			if (!RankSettings(u"  .,  "_q, entries, 10).empty()) {
				return u"punctuation-only query must return nothing"_q;
			}
			if (!RankSettings(u"chat"_q, entries, 0).empty()) {
				return u"zero limit must return nothing"_q;
			}
			const auto limited = RankSettings(u"chat"_q, entries, 1);
			if (limited != std::vector<int>{ 3 }) {
				return u"limit must keep the best: "_q + Ids(limited);
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
