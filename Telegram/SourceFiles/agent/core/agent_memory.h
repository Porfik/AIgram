/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "base/flat_map.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Agent {

inline constexpr auto kMemoryMaxFacts = 50;
inline constexpr auto kMemoryMaxChars = 300;

struct MemoryFact {
	QString id;
	QString text;

	friend inline bool operator==(
		const MemoryFact &,
		const MemoryFact &) = default;
};

struct Memory {
	std::vector<MemoryFact> facts;
	int nextId = 1;
};

enum class MemoryError : uchar {
	None,
	Empty,
	TooLong,
	Full,
	UnknownId,
	DuplicateId,
};

struct MemoryResult {
	MemoryError error = MemoryError::None;
	MemoryFact fact;
	QString previousText;
	int index = -1;
};

[[nodiscard]] QString NormalizeMemoryText(const QString &text);

MemoryResult MemoryAdd(Memory &memory, const QString &text);
MemoryResult MemoryUpdate(
	Memory &memory,
	const QString &id,
	const QString &text);
MemoryResult MemoryRemove(Memory &memory, const QString &id);
MemoryResult MemoryRestore(Memory &memory, MemoryFact fact, int index);

[[nodiscard]] QString FormatMemory(const Memory &memory);
[[nodiscard]] QString MemoryErrorText(MemoryError error);

using MemoryBook = base::flat_map<PeerKey, Memory>;

[[nodiscard]] QByteArray SerializeMemoryBook(const MemoryBook &book);
[[nodiscard]] std::optional<MemoryBook> DeserializeMemoryBook(
	const QByteArray &bytes);

} // namespace Agent
