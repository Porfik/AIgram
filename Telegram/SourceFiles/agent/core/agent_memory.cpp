/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_memory.h"

#include "base/assertion.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

#include <algorithm>

namespace Agent {
namespace {

constexpr auto kBookVersion = quint32(1);
constexpr auto kMaxOwners = 100'000;

[[nodiscard]] QString FactId(int number) {
	return u"m%1"_q.arg(number);
}

[[nodiscard]] std::optional<int> FactNumber(const QString &id) {
	if (!id.startsWith(u'm')) {
		return std::nullopt;
	}
	auto ok = false;
	const auto number = id.mid(1).toInt(&ok);
	return (ok && number > 0) ? std::optional<int>(number) : std::nullopt;
}

[[nodiscard]] MemoryFact *FindFact(Memory &memory, const QString &id) {
	for (auto &fact : memory.facts) {
		if (fact.id == id) {
			return &fact;
		}
	}
	return nullptr;
}

} // namespace

QString NormalizeMemoryText(const QString &text) {
	auto result = text;
	result.replace(u"\r\n"_q, u" "_q);
	result.replace(u'\n', u' ');
	result.replace(u'\r', u' ');
	return result.trimmed();
}

MemoryResult MemoryAdd(Memory &memory, const QString &text) {
	const auto normalized = NormalizeMemoryText(text);
	if (normalized.isEmpty()) {
		return { .error = MemoryError::Empty };
	} else if (normalized.size() > kMemoryMaxChars) {
		return { .error = MemoryError::TooLong };
	} else if (memory.facts.size() >= kMemoryMaxFacts) {
		return { .error = MemoryError::Full };
	}
	auto fact = MemoryFact{ FactId(memory.nextId), normalized };
	const auto index = int(memory.facts.size());
	memory.facts.push_back(fact);
	++memory.nextId;
	return { .error = MemoryError::None, .fact = fact, .index = index };
}

MemoryResult MemoryUpdate(
		Memory &memory,
		const QString &id,
		const QString &text) {
	auto *fact = FindFact(memory, id);
	if (!fact) {
		return { .error = MemoryError::UnknownId };
	}
	const auto normalized = NormalizeMemoryText(text);
	if (normalized.isEmpty()) {
		return { .error = MemoryError::Empty };
	} else if (normalized.size() > kMemoryMaxChars) {
		return { .error = MemoryError::TooLong };
	}
	auto result = MemoryResult{ .error = MemoryError::None };
	result.previousText = fact->text;
	result.fact = MemoryFact{ fact->id, normalized };
	result.index = int(fact - memory.facts.data());
	fact->text = normalized;
	return result;
}

MemoryResult MemoryRemove(Memory &memory, const QString &id) {
	const auto it = std::find_if(
		memory.facts.begin(),
		memory.facts.end(),
		[&](const MemoryFact &fact) { return fact.id == id; });
	if (it == memory.facts.end()) {
		return { .error = MemoryError::UnknownId };
	}
	const auto index = int(it - memory.facts.begin());
	auto result = MemoryResult{ .error = MemoryError::None };
	result.fact = *it;
	result.index = index;
	memory.facts.erase(it);
	return result;
}

MemoryResult MemoryRestore(Memory &memory, MemoryFact fact, int index) {
	if (FindFact(memory, fact.id)) {
		return { .error = MemoryError::DuplicateId };
	} else if (memory.facts.size() >= kMemoryMaxFacts) {
		return { .error = MemoryError::Full };
	}
	const auto clamped = std::clamp(index, 0, int(memory.facts.size()));
	memory.facts.insert(memory.facts.begin() + clamped, fact);
	if (const auto number = FactNumber(fact.id)) {
		if (*number >= memory.nextId) {
			memory.nextId = *number + 1;
		}
	}
	return { .error = MemoryError::None, .fact = fact, .index = clamped };
}

QString FormatMemory(const Memory &memory) {
	if (memory.facts.empty()) {
		return QString();
	}
	auto lines = QStringList();
	lines.reserve(memory.facts.size());
	for (const auto &fact : memory.facts) {
		lines.push_back(u"[%1] %2"_q.arg(fact.id, fact.text));
	}
	return u"- "_q + lines.join(u"\n- "_q);
}

QString MemoryErrorText(MemoryError error) {
	switch (error) {
	case MemoryError::None: return QString();
	case MemoryError::Empty: return u"The fact is empty."_q;
	case MemoryError::TooLong:
		return u"The fact is longer than %1 characters; "
			u"make it shorter."_q.arg(kMemoryMaxChars);
	case MemoryError::Full:
		return u"Memory is full (%1 facts); "
			u"update or remove facts first."_q.arg(kMemoryMaxFacts);
	case MemoryError::UnknownId:
		return u"There is no fact with this id."_q;
	case MemoryError::DuplicateId:
		return u"A fact with this id already exists."_q;
	}
	Unexpected("Error in Agent::MemoryErrorText.");
}

QByteArray SerializeMemoryBook(const MemoryBook &book) {
	auto owners = 0;
	for (const auto &[owner, memory] : book) {
		if (!memory.facts.empty() || memory.nextId != 1) {
			++owners;
		}
	}
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kBookVersion << qint32(owners);
	for (const auto &[owner, memory] : book) {
		if (memory.facts.empty() && memory.nextId == 1) {
			continue;
		}
		stream << quint64(owner) << qint32(memory.nextId);
		stream << qint32(memory.facts.size());
		for (const auto &fact : memory.facts) {
			stream << fact.id << fact.text;
		}
	}
	return result;
}

std::optional<MemoryBook> DeserializeMemoryBook(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok || version != kBookVersion) {
		return std::nullopt;
	}
	auto ownerCount = qint32(0);
	stream >> ownerCount;
	if (stream.status() != QDataStream::Ok
		|| ownerCount < 0
		|| ownerCount > kMaxOwners) {
		return std::nullopt;
	}
	auto book = MemoryBook();
	for (auto i = 0; i != ownerCount; ++i) {
		auto owner = quint64(0);
		auto nextId = qint32(0);
		auto factCount = qint32(0);
		stream >> owner >> nextId >> factCount;
		if (stream.status() != QDataStream::Ok
			|| nextId <= 0
			|| factCount < 0
			|| factCount > kMemoryMaxFacts) {
			return std::nullopt;
		}
		auto memory = Memory();
		memory.nextId = nextId;
		memory.facts.reserve(factCount);
		for (auto j = 0; j != factCount; ++j) {
			auto fact = MemoryFact();
			stream >> fact.id >> fact.text;
			if (stream.status() != QDataStream::Ok) {
				return std::nullopt;
			}
			memory.facts.push_back(std::move(fact));
		}
		book.emplace_or_assign(PeerKey(owner), std::move(memory));
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return book;
}

} // namespace Agent
