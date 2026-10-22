/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_tool_catalog.h"

#include "base/basic_types.h"
#include "base/flat_map.h"
#include "base/flat_set.h"

#include <QtCore/QLocale>
#include <QtCore/QStringList>

#include <algorithm>
#include <cmath>

namespace Agent {
namespace {

[[nodiscard]] QString SectionTitle(CatalogSection section) {
	switch (section) {
	case CatalogSection::Read: return u"Find and read"_q;
	case CatalogSection::Messages: return u"Messages"_q;
	case CatalogSection::ChatState: return u"Chat list"_q;
	case CatalogSection::Folders: return u"Folders"_q;
	case CatalogSection::MediaFiles: return u"Media and files"_q;
	case CatalogSection::Memory: return u"Memory"_q;
	case CatalogSection::Tasks: return u"Background tasks"_q;
	case CatalogSection::Admin: return u"Administration"_q;
	case CatalogSection::Contacts: return u"Contacts"_q;
	case CatalogSection::Profile: return u"Profile"_q;
	case CatalogSection::TelegramAi: return u"Telegram AI"_q;
	case CatalogSection::Mtproto: return u"Telegram API"_q;
	case CatalogSection::Other: return u"Other"_q;
	}
	return u"Other"_q;
}

[[nodiscard]] QStringList Words(const QString &text) {
	auto result = QStringList();
	auto start = -1;
	auto previousLowerOrDigit = false;
	for (auto i = 0; i != text.size(); ++i) {
		const auto ch = text[i];
		if (!ch.isLetterOrNumber()) {
			if (start >= 0) {
				result.push_back(text.mid(start, i - start).toLower());
				start = -1;
			}
			previousLowerOrDigit = false;
			continue;
		} else if (start >= 0 && ch.isUpper() && previousLowerOrDigit) {
			result.push_back(text.mid(start, i - start).toLower());
			start = i;
		} else if (start < 0) {
			start = i;
		}
		previousLowerOrDigit = ch.isLower() || ch.isNumber();
	}
	if (start >= 0) {
		result.push_back(text.mid(start).toLower());
	}
	return result;
}

[[nodiscard]] bool IsStopWord(const QString &word) {
	static const auto kStopWords = base::flat_set<QString>{
		u"a"_q,
		u"an"_q,
		u"and"_q,
		u"are"_q,
		u"as"_q,
		u"at"_q,
		u"be"_q,
		u"by"_q,
		u"for"_q,
		u"from"_q,
		u"in"_q,
		u"is"_q,
		u"it"_q,
		u"of"_q,
		u"on"_q,
		u"or"_q,
		u"that"_q,
		u"the"_q,
		u"this"_q,
		u"to"_q,
		u"with"_q,
	};
	return kStopWords.contains(word);
}

[[nodiscard]] QString Stem(QString word) {
	const auto size = word.size();
	if (size > 4 && word.endsWith(u"ies"_q)) {
		return word.left(size - 3) + u"y"_q;
	} else if (size > 4
		&& (word.endsWith(u"ches"_q)
			|| word.endsWith(u"shes"_q)
			|| word.endsWith(u"sses"_q)
			|| word.endsWith(u"xes"_q)
			|| word.endsWith(u"zes"_q))) {
		return word.left(size - 2);
	} else if (size > 3
		&& word.endsWith(u"s"_q)
		&& !word.endsWith(u"ss"_q)) {
		return word.left(size - 1);
	}
	return word;
}

} // namespace

QString FormatToolCatalog(
		const std::vector<CatalogEntry> &entries,
		const std::vector<CatalogGroup> &groups) {
	auto lines = QStringList();
	for (auto section = 0
		; section != int(CatalogSection::Other) + 1
		; ++section) {
		auto sectionLines = QStringList();
		for (const auto &entry : entries) {
			if (int(entry.section) != section) {
				continue;
			}
			auto line = u"- %1"_q.arg(entry.name);
			if (!entry.snippet.isEmpty()) {
				line += u": %1"_q.arg(entry.snippet);
			}
			if (!entry.group.isEmpty()) {
				line += u" [group %1]"_q.arg(entry.group);
			}
			sectionLines.push_back(line);
		}
		if (sectionLines.isEmpty()) {
			continue;
		}
		lines.push_back(u"## %1"_q.arg(SectionTitle(
			CatalogSection(section))));
		lines.append(sectionLines);
	}
	if (!groups.empty()) {
		lines.push_back(u"Groups for load_tools:"_q);
		for (const auto &group : groups) {
			lines.push_back(u"- %1: %2"_q.arg(group.name, group.when));
		}
	}
	return lines.join(u'\n');
}

std::vector<int> RankToolDocuments(
		const QString &query,
		const std::vector<ToolDocument> &documents,
		int limit) {
	const auto queryWords = Words(query);
	if (queryWords.isEmpty() || documents.empty() || limit <= 0) {
		return {};
	}
	auto terms = std::vector<QString>();
	{
		auto seen = base::flat_set<QString>();
		for (const auto &word : queryWords) {
			if (IsStopWord(word)) {
				continue;
			}
			const auto stem = Stem(word);
			if (seen.insert(stem).second) {
				terms.push_back(stem);
			}
		}
	}
	if (terms.empty()) {
		return {};
	}
	const auto count = int(documents.size());
	auto frequencies = std::vector<base::flat_map<QString, int>>();
	frequencies.reserve(count);
	auto lengths = std::vector<int>(count, 0);
	auto totalLength = 0;
	for (auto i = 0; i != count; ++i) {
		auto words = Words(documents[i].name);
		words += Words(documents[i].text);
		auto map = base::flat_map<QString, int>();
		for (const auto &word : words) {
			if (IsStopWord(word)) {
				continue;
			}
			++map[Stem(word)];
		}
		for (const auto &[term, tf] : map) {
			lengths[i] += tf;
		}
		totalLength += lengths[i];
		frequencies.push_back(std::move(map));
	}
	if (!totalLength) {
		return {};
	}
	auto dfs = base::flat_map<QString, int>();
	for (const auto &term : terms) {
		auto df = 0;
		for (const auto &map : frequencies) {
			if (map.contains(term)) {
				++df;
			}
		}
		dfs.emplace(term, df);
	}
	auto rarest = QString();
	auto rarestDf = 0;
	auto answerable = 0;
	for (const auto &term : terms) {
		const auto df = dfs[term];
		if (!df) {
			continue;
		}
		++answerable;
		if (!rarestDf || df < rarestDf) {
			rarest = term;
			rarestDf = df;
		}
	}
	if (!rarestDf) {
		return {};
	}
	constexpr auto kK1 = 1.2;
	constexpr auto kB = 0.75;
	const auto avgdl = float64(totalLength) / count;
	struct Match {
		int index = 0;
		float64 score = 0.;
	};
	auto matches = std::vector<Match>();
	matches.reserve(count);
	for (auto i = 0; i != count; ++i) {
		const auto &map = frequencies[i];
		if (!map.contains(rarest)) {
			continue;
		} else if (answerable >= 4) {
			auto present = 0;
			for (const auto &term : terms) {
				if (dfs[term] && map.contains(term)) {
					++present;
				}
			}
			if (present * 2 < answerable + 1) {
				continue;
			}
		}
		auto score = 0.;
		for (const auto &[term, tf] : map) {
			const auto dfIt = dfs.find(term);
			if (dfIt == dfs.end() || !dfIt->second) {
				continue;
			}
			const auto idf = std::log(
				1.0 + (count - dfIt->second + 0.5) / (dfIt->second + 0.5));
			score += idf * tf * (kK1 + 1)
				/ (tf + kK1 * (1 - kB + kB * lengths[i] / avgdl));
		}
		if (score > 0) {
			matches.push_back({ i, score });
		}
	}
	std::stable_sort(
		matches.begin(),
		matches.end(),
		[](const Match &a, const Match &b) {
			return a.score > b.score;
		});
	if (matches.size() > limit) {
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
