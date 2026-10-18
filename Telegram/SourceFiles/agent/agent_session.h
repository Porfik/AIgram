/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_engine.h"
#include "agent/core/agent_record.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>

#include <memory>
#include <optional>
#include <vector>

namespace Agent {

class Loop;
class Manager;
class PolicyGate;
class ToolRegistry;

struct SendRequest {
	QString text;
	std::vector<PeerKey> mentions;
	QString hint;
};

enum class SessionStatus : uchar {
	Idle,
	Working,
	Compacting,
	Failed,
};

struct SessionState {
	SessionStatus status = SessionStatus::Idle;
	std::optional<Error> error;
	std::optional<TurnEnd> lastEnd;
	std::vector<QString> queued;
};

class Session final : public base::has_weak_ptr {
public:
	Session(not_null<Manager*> manager, SessionRecord record);
	~Session();

	[[nodiscard]] Manager &manager() const;
	[[nodiscard]] const QString &id() const;
	[[nodiscard]] const Scope &scope() const;

	[[nodiscard]] const SessionRecord &record() const;
	[[nodiscard]] rpl::producer<> recordChanges() const;
	[[nodiscard]] rpl::producer<EngineEvent> live() const;

	[[nodiscard]] const SessionState &state() const;
	[[nodiscard]] rpl::producer<> stateChanges() const;

	void send(SendRequest request);
	void stop();
	void retry();

private:
	const not_null<Manager*> _manager;
	SessionRecord _record;
	SessionState _state;
	std::unique_ptr<PolicyGate> _gate;
	std::unique_ptr<ToolRegistry> _tools;
	std::unique_ptr<Loop> _loop;
	rpl::event_stream<> _recordChanges;
	rpl::event_stream<EngineEvent> _live;
	rpl::event_stream<> _stateChanges;

};

} // namespace Agent
