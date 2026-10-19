/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_navigation.h"

#include <algorithm>

namespace Agent {
namespace {

[[nodiscard]] QStringList Words(const QString &text) {
	auto result = QStringList();
	auto start = -1;
	for (auto i = 0; i != text.size(); ++i) {
		if (text[i].isLetterOrNumber()) {
			if (start < 0) {
				start = i;
			}
		} else if (start >= 0) {
			result.push_back(text.mid(start, i - start));
			start = -1;
		}
	}
	if (start >= 0) {
		result.push_back(text.mid(start));
	}
	return result;
}

[[nodiscard]] bool WordMatches(const QStringList &field, const QString &word) {
	for (const auto &candidate : field) {
		if (candidate.startsWith(word, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] std::optional<int> MatchScore(
		const QStringList &query,
		const SettingEntry &entry) {
	const auto title = Words(entry.title);
	auto keywordWords = QStringList();
	for (const auto &keyword : entry.keywords) {
		keywordWords += Words(keyword);
	}
	const auto path = Words(entry.path);
	auto score = 0;
	for (const auto &word : query) {
		if (WordMatches(title, word)) {
			continue;
		} else if (WordMatches(keywordWords, word)) {
			score += 1;
		} else if (WordMatches(path, word)) {
			score += 2;
		} else {
			return std::nullopt;
		}
	}
	return score;
}

} // namespace

std::vector<int> RankSettings(
		const QString &query,
		const std::vector<SettingEntry> &entries,
		int limit) {
	const auto words = Words(query);
	if (words.isEmpty() || limit <= 0) {
		return {};
	}
	struct Match {
		int index = 0;
		int score = 0;
	};
	auto matches = std::vector<Match>();
	matches.reserve(entries.size());
	for (auto i = 0, count = int(entries.size()); i != count; ++i) {
		if (const auto score = MatchScore(words, entries[i])) {
			matches.push_back({ i, *score });
		}
	}
	std::stable_sort(
		matches.begin(),
		matches.end(),
		[&](const Match &a, const Match &b) {
			if (a.score != b.score) {
				return a.score < b.score;
			}
			return entries[a.index].title.size()
				< entries[b.index].title.size();
		});
	if (int(matches.size()) > limit) {
		matches.resize(limit);
	}
	auto result = std::vector<int>();
	result.reserve(matches.size());
	for (const auto &match : matches) {
		result.push_back(match.index);
	}
	return result;
}

} // namespace Agent
