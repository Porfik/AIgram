/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_record.h"

#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class SessionStore final {
public:
	explicit SessionStore(not_null<Main::Session*> session);

	[[nodiscard]] std::vector<SessionInfo> readIndex() const;
	void writeIndex(const std::vector<SessionInfo> &list);

	[[nodiscard]] std::optional<SessionRecord> readSession(
		const QString &id) const;
	void writeSession(const SessionRecord &record);
	void removeSession(const QString &id);

private:
	const not_null<Main::Session*> _session;

};

} // namespace Agent
