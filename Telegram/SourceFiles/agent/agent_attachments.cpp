/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_attachments.h"
#include "agent/core/agent_media.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Agent {

Attachments::Attachments(not_null<Main::Session*> session)
: _session(session) {
}

MediaBlock Attachments::add(
		const QString &sessionId,
		int index,
		const AttachmentInput &input) {
	auto block = MediaBlock{
		.kind = MediaKindForMime(input.mimeType),
		.bytes = input.bytes,
		.mimeType = input.mimeType,
		.name = input.name,
		.attachment = AttachmentId(index),
		.size = int64(input.bytes.size()),
		.duration = input.duration,
	};
	_session->local().writeAgentValue(
		u"a/%1/%2"_q.arg(sessionId, block.attachment),
		SerializeAttachment(block));
	block.bytes = QByteArray();
	return block;
}

std::optional<MediaBlock> Attachments::read(
		const QString &sessionId,
		const QString &id) const {
	const auto bytes = _session->local().readAgentValue(
		u"a/%1/%2"_q.arg(sessionId, id));
	if (!bytes) {
		LOG(("Agent Error: attachment %1 is missing.").arg(id));
		return std::nullopt;
	}
	auto block = DeserializeAttachment(*bytes);
	if (!block) {
		LOG(("Agent Error: could not parse attachment %1.").arg(id));
		return std::nullopt;
	}
	return block;
}

void Attachments::remove(const QString &sessionId, int count) {
	for (auto i = 1; i <= count; ++i) {
		_session->local().removeAgentValue(
			u"a/%1/%2"_q.arg(sessionId, AttachmentId(i)));
	}
}

} // namespace Agent
