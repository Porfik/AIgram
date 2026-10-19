/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_engine.h"
#include "agent/core/agent_providers.h"
#include "agent/core/agent_record.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

#include <memory>
#include <optional>
#include <vector>

namespace Agent {

class Loop;
class Manager;
class PolicyGate;
class SingleRequest;
class ToolRegistry;

struct SendRequest {
	QString text;
	std::vector<PeerKey> mentions;
	QString hint;
	QString untrusted;
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
	int pendingCards = 0;
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

	void decideCard(const QString &toolUseId, CardDecision decision);
	[[nodiscard]] bool canUndo(const QString &toolUseId) const;
	void undo(const std::vector<QString> &toolUseIds);
	[[nodiscard]] rpl::producer<> undoChanges() const;
	[[nodiscard]] rpl::producer<QString> undoErrors() const;

private:
	friend class Manager;

	void dispatch(SendRequest request);
	void finishOpen();
	void applyCard(Card card);
	[[nodiscard]] QString undoHint();
	void failWithoutProvider();
	void accountUsage(const Usage &usage, const ProviderSettings &provider);
	[[nodiscard]] QString buildSystem() const;
	[[nodiscard]] int planCompaction(
		const ProviderSettings &provider,
		bool force = false) const;
	void startCompaction(int until);
	void runTurn();
	void runLoop(const ProviderSettings &provider);
	void handleEvent(EngineEvent event);
	[[nodiscard]] bool handleTurnFinished(const TurnFinished &turn);
	void continueAfterTurn(const TurnFinished &turn);
	void compactAndResume();
	void processQueue();
	void copyLoopMessages();
	[[nodiscard]] bool applyState(SessionStatus status);
	[[nodiscard]] bool applyState(
		SessionStatus status,
		std::optional<Error> error,
		std::optional<TurnEnd> lastEnd);
	[[nodiscard]] bool publishRecord();
	void startTitleRequest();
	void applyRename(const QString &title);

	const not_null<Manager*> _manager;
	SessionRecord _record;
	SessionState _state;
	std::unique_ptr<PolicyGate> _gate;
	std::unique_ptr<ToolRegistry> _tools;
	std::unique_ptr<Loop> _loop;
	std::vector<SendRequest> _queue;
	std::unique_ptr<SingleRequest> _compaction;
	std::unique_ptr<SingleRequest> _titleRequest;
	std::optional<ProviderSettings> _loopProvider;
	int _loopBase = 0;
	int _loopContext = 0;
	int _undoneReported = 0;
	bool _titleRequested = false;
	bool _overflowRetried = false;
	bool _repairedOnLoad = false;
	rpl::event_stream<> _recordChanges;
	rpl::event_stream<EngineEvent> _live;
	rpl::event_stream<> _stateChanges;
	rpl::event_stream<> _undoChanges;
	rpl::event_stream<QString> _undoErrors;
	rpl::lifetime _loopLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Agent
