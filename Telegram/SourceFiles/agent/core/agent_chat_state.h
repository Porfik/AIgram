/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>

#include <optional>

namespace Agent {

struct ChatState {
	bool pinned = true;
	int pinnedIndex = 0;
	qint64 since = 0;

	friend inline bool operator==(
		const ChatState &,
		const ChatState &) = default;
};

[[nodiscard]] QByteArray SerializeChatState(const ChatState &state);
[[nodiscard]] std::optional<ChatState> DeserializeChatState(
	const QByteArray &bytes);

} // namespace Agent
