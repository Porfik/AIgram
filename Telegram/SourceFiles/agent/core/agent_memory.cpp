/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_memory.h"

namespace Agent {

QString NormalizeMemoryText(const QString &text) {
	return text.trimmed();
}

MemoryResult MemoryAdd(Memory &memory, const QString &text) {
	return { .error = MemoryError::Full };
}

MemoryResult MemoryUpdate(
		Memory &memory,
		const QString &id,
		const QString &text) {
	return { .error = MemoryError::UnknownId };
}

MemoryResult MemoryRemove(Memory &memory, const QString &id) {
	return { .error = MemoryError::UnknownId };
}

MemoryResult MemoryRestore(Memory &memory, MemoryFact fact, int index) {
	return { .error = MemoryError::Full };
}

QString FormatMemory(const Memory &memory) {
	return QString();
}

QString MemoryErrorText(MemoryError error) {
	return QString();
}

QByteArray SerializeMemoryBook(const MemoryBook &book) {
	return QByteArray();
}

std::optional<MemoryBook> DeserializeMemoryBook(const QByteArray &bytes) {
	return std::nullopt;
}

} // namespace Agent
