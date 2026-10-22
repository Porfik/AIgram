/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_tool_catalog.h"

#include "base/basic_types.h"
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

} // namespace Agent
