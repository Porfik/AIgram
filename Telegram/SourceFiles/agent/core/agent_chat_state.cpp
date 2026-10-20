/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_chat_state.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

namespace Agent {
namespace {

constexpr auto kChatStateVersion = quint32(1);

} // namespace

QByteArray SerializeChatState(const ChatState &state) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< kChatStateVersion
		<< qint32(state.pinned ? 1 : 0)
		<< qint32(state.pinnedIndex)
		<< state.since;
	return result;
}

std::optional<ChatState> DeserializeChatState(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	auto pinned = qint32(0);
	auto index = qint32(0);
	auto result = ChatState();
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kChatStateVersion) {
		return std::nullopt;
	}
	stream >> pinned >> index >> result.since;
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| index < 0) {
		return std::nullopt;
	}
	result.pinned = (pinned != 0);
	result.pinnedIndex = index;
	return result;
}

} // namespace Agent
