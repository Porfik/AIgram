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

#include <QtCore/QJsonArray>
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

[[nodiscard]] int Levenshtein(const QString &a, const QString &b) {
	const auto columns = b.size() + 1;
	auto previous = std::vector<int>(columns);
	for (auto j = 0; j != columns; ++j) {
		previous[j] = j;
	}
	for (auto i = 1; i != a.size() + 1; ++i) {
		auto current = std::vector<int>(columns, 0);
		current[0] = i;
		for (auto j = 1; j != columns; ++j) {
			const auto cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
			current[j] = std::min({
				previous[j] + 1,
				current[j - 1] + 1,
				previous[j - 1] + cost });
		}
		previous = std::move(current);
	}
	return previous[columns - 1];
}

[[nodiscard]] QString StripSeparators(const QString &name) {
	auto result = name;
	result.remove(u'-');
	result.remove(u'_');
	return result;
}

[[nodiscard]] QJsonValue CoerceScalar(
		const QJsonValue &value,
		const QString &type) {
	if (!value.isString()) {
		return value;
	}
	const auto text = value.toString().trimmed();
	auto ok = false;
	if (type == u"integer"_q) {
		const auto number = text.toLongLong(&ok);
		return ok ? QJsonValue(number) : value;
	} else if (type == u"number"_q) {
		const auto number = text.toDouble(&ok);
		return (ok && std::isfinite(number)) ? QJsonValue(number) : value;
	} else if (type == u"boolean"_q) {
		if (text == u"true"_q) {
			return QJsonValue(true);
		} else if (text == u"false"_q) {
			return QJsonValue(false);
		}
	}
	return value;
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

std::vector<QString> NearestToolNames(
		const QString &name,
		const std::vector<QString> &names,
		int limit) {
	if (limit <= 0) {
		return {};
	}
	const auto lower = name.toLower();
	const auto stripped = StripSeparators(lower);
	auto nameWords = base::flat_set<QString>();
	for (const auto &word : lower.split('_', Qt::SkipEmptyParts)) {
		nameWords.insert(word);
	}
	auto taken = base::flat_set<QString>();
	auto result = std::vector<QString>();
	const auto add = [&](const QString &candidate) {
		const auto candidateLower = candidate.toLower();
		if (candidateLower == lower || taken.contains(candidateLower)) {
			return;
		}
		taken.insert(candidateLower);
		result.push_back(candidate);
	};
	const auto full = [&] {
		return result.size() >= limit;
	};
	for (const auto &candidate : names) {
		if (StripSeparators(candidate.toLower()) == stripped) {
			add(candidate);
			if (full()) {
				return result;
			}
		}
	}
	struct Candidate {
		int index = 0;
		int distance = 0;
	};
	auto close = std::vector<Candidate>();
	const auto threshold = std::max(2, int(lower.size()) / 4);
	for (auto i = 0; i != int(names.size()); ++i) {
		const auto distance = Levenshtein(lower, names[i].toLower());
		if (distance <= threshold) {
			close.push_back({ i, distance });
		}
	}
	std::stable_sort(
		close.begin(),
		close.end(),
		[](const Candidate &a, const Candidate &b) {
			return a.distance < b.distance;
		});
	for (const auto &candidate : close) {
		add(names[candidate.index]);
		if (full()) {
			return result;
		}
	}
	for (const auto &candidate : names) {
		auto words = base::flat_set<QString>();
		const auto parts = candidate.toLower().split(
			'_',
			Qt::SkipEmptyParts);
		for (const auto &word : parts) {
			words.insert(word);
		}
		auto shared = false;
		for (const auto &word : words) {
			if (nameWords.contains(word)) {
				shared = true;
				break;
			}
		}
		if (shared) {
			add(candidate);
			if (full()) {
				return result;
			}
		}
	}
	return result;
}

QString MatchSnippet(
		const QString &text,
		const std::vector<QString> &queries,
		int width) {
	if (width <= 0 || text.size() <= width) {
		return text;
	}
	auto words = QStringList();
	for (const auto &query : queries) {
		words += Words(query);
	}
	auto match = -1;
	for (auto i = 0; match < 0 && i != text.size(); ++i) {
		if (i > 0 && text[i - 1].isLetterOrNumber()) {
			continue;
		}
		for (const auto &word : words) {
			const auto part = text.mid(i, word.size());
			if (part.startsWith(word, Qt::CaseInsensitive)) {
				match = i;
				break;
			}
		}
	}
	const auto last = int(text.size()) - width;
	auto start = (match > 0) ? match - width / 3 : 0;
	start = std::clamp(start, 0, last);
	auto end = start + width;
	if (start > 0) {
		const auto limit = std::min(start + 20, end - 1);
		for (auto i = start; i <= limit; ++i) {
			if (text[i].isSpace()) {
				start = i + 1;
				break;
			}
		}
	}
	if (end < text.size()) {
		for (auto i = end - 1; i >= std::max(end - 20, start); --i) {
			if (text[i].isSpace()) {
				end = i;
				break;
			}
		}
	}
	if (start + 1 < end && text[start].isLowSurrogate()) {
		++start;
	}
	if (start + 1 < end && text[end - 1].isHighSurrogate()) {
		--end;
	}
	return (start > 0 ? u"…"_q : QString())
		+ text.mid(start, end - start)
		+ (end < text.size() ? u"…"_q : QString());
}

QString FormatTurnContext(const TurnContext &context) {
	auto lines = QStringList();
	if (context.now.isValid()) {
		auto line = u"Now: %1, %2"_q.arg(
			context.now.toString(u"yyyy-MM-dd HH:mm"_q),
			QLocale::c().dayName(
				context.now.date().dayOfWeek(),
				QLocale::LongFormat));
		if (!context.zone.isEmpty()) {
			line += u", %1"_q.arg(context.zone);
		}
		lines.push_back(line + u'.');
	}
	if (!context.loadedGroups.empty()) {
		lines.push_back(u"Loaded tool groups: %1."_q.arg(
			QStringList(
				context.loadedGroups.begin(),
				context.loadedGroups.end()).join(u", "_q)));
	}
	if (!context.topic.isEmpty()) {
		lines.push_back(u"The user has %1 open."_q.arg(context.topic));
	}
	if (context.pendingCards > 0) {
		lines.push_back(context.pendingCards == 1
			? u"Waiting for the user's decision: 1 card."_q
			: u"Waiting for the user's decision: %1 cards."_q.arg(
				context.pendingCards));
	}
	return lines.join(u'\n');
}

QJsonObject CoerceToolInput(const QJsonObject &schema, QJsonObject input) {
	const auto properties = schema.value(u"properties"_q).toObject();
	for (const auto &key : input.keys()) {
		const auto property = properties.value(key).toObject();
		const auto type = property.value(u"type"_q).toString();
		const auto value = input.value(key);
		if (type == u"array"_q && value.isArray()) {
			const auto items = property.value(u"items"_q).toObject()
				.value(u"type"_q).toString();
			auto list = value.toArray();
			for (auto i = 0; i != list.size(); ++i) {
				list[i] = CoerceScalar(list.at(i), items);
			}
			input.insert(key, list);
		} else {
			input.insert(key, CoerceScalar(value, type));
		}
	}
	return input;
}

} // namespace Agent
