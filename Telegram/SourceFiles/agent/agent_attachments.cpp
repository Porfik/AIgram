/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_attachments.h"

namespace Agent {

Attachments::Attachments(not_null<Main::Session*> session)
: _session(session) {
}

MediaBlock Attachments::add(
		const QString &sessionId,
		int index,
		const AttachmentInput &input) {
	return MediaBlock();
}

std::optional<MediaBlock> Attachments::read(
		const QString &sessionId,
		const QString &id) const {
	return std::nullopt;
}

void Attachments::remove(const QString &sessionId, int count) {
}

} // namespace Agent
