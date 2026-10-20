/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_types.h"

#include <optional>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

struct AttachmentInput {
	QString name;
	QString mimeType;
	QByteArray bytes;
	int duration = 0;
};

class Attachments final {
public:
	explicit Attachments(not_null<Main::Session*> session);

	[[nodiscard]] MediaBlock add(
		const QString &sessionId,
		int index,
		const AttachmentInput &input);
	[[nodiscard]] std::optional<MediaBlock> read(
		const QString &sessionId,
		const QString &id) const;
	void remove(const QString &sessionId, int count);

private:
	const not_null<Main::Session*> _session;

};

} // namespace Agent
