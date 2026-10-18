/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_record.h"

namespace Agent {

QString GenerateSessionId() {
	return QString();
}

QByteArray SerializeSession(const SessionRecord &record) {
	return QByteArray();
}

std::optional<SessionRecord> DeserializeSession(const QByteArray &bytes) {
	return std::nullopt;
}

QByteArray SerializeIndex(const std::vector<SessionInfo> &list) {
	return QByteArray();
}

std::optional<std::vector<SessionInfo>> DeserializeIndex(
		const QByteArray &bytes) {
	return std::nullopt;
}

} // namespace Agent
