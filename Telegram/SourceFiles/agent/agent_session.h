/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/agent_attachments.h"
#include "agent/core/agent_card.h"
#include "agent/core/agent_engine.h"
#include "agent/core/agent_providers.h"
#include "agent/core/agent_record.h"
#include "agent/core/agent_task.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

#include <memory>
#include <optional>
#include <vector>

namespace Agent {

class Manager;
class PendingRequest;
class PolicyGate;
class ToolRegistry;

struct SendRequest {
	QString text;
	std::vector<PeerKey> mentions;
	QString hint;
	QString untrusted;
	std::vector<AttachmentInput> attachments;
	QString replyTo;
	QString replyQuote;
	QString replyText;
};

enum class SessionStatus : uchar {
	Idle,
	Working,
	Compacting,
	Failed,
};

struct RunSetup {
	Task task;
	std::vector<PeerKey> triggerPeers;
	ProviderSettings provider;
	QString frame;
	Fn<void(QString text)> notify;
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
	Session(
		not_null<Manager*> manager,
		SessionRecord record,
		std::optional<RunSetup> run = std::nullopt);
	~Session();

	[[nodiscard]] Manager &manager() const;
	[[nodiscard]] const QString &id() const;
	[[nodiscard]] const Scope &scope() const;
	[[nodiscard]] bool isRun() const;

	[[nodiscard]] const SessionRecord &record() const;
	[[nodiscard]] rpl::producer<> recordChanges() const;
	[[nodiscard]] rpl::producer<EngineEvent> live() const;

	[[nodiscard]] const SessionState &state() const;
	[[nodiscard]] rpl::producer<> stateChanges() const;

	[[nodiscard]] QString profileId() const;
	void setProfile(const QString &profileId);

	void send(SendRequest request);
	void stop();
	void interrupt();
	void retry();

	void decideCard(const QString &toolUseId, CardDecision decision);
	void expireCards();
	[[nodiscard]] bool canUndo(const QString &toolUseId) const;
	void undo(const std::vector<QString> &toolUseIds);
	[[nodiscard]] rpl::producer<> undoChanges() const;
	[[nodiscard]] rpl::producer<QString> undoErrors() const;

private:
	friend class Manager;

	void dispatch(SendRequest request);
	void halt();
	void finishOpen();
	void applyCard(Card card);
	[[nodiscard]] QString undoHint();
	void failWithoutProvider();
	void failWithoutClaude();
	[[nodiscard]] std::unique_ptr<Engine> createEngine(
		const ProviderSettings &provider,
		std::vector<Message> messages);
	[[nodiscard]] std::optional<ProviderSettings> currentProvider() const;
	[[nodiscard]] QString cacheKey() const;
	void accountUsage(const Usage &usage, const ProviderSettings &provider);
	[[nodiscard]] QString buildSystem() const;
	[[nodiscard]] SessionRecord withAttachedImages() const;
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
	void trackThinking(const EngineEvent &event);
	void markTurnEnd(TurnEnd end);
	[[nodiscard]] bool applyState(SessionStatus status);
	[[nodiscard]] bool applyState(
		SessionStatus status,
		std::optional<Error> error,
		std::optional<TurnEnd> lastEnd);
	[[nodiscard]] bool publishRecord();
	void startTitleRequest();
	void applyRename(const QString &title);

	const not_null<Manager*> _manager;
	const std::optional<RunSetup> _run;
	SessionRecord _record;
	SessionState _state;
	std::unique_ptr<PolicyGate> _gate;
	std::unique_ptr<ToolRegistry> _tools;
	std::unique_ptr<Engine> _loop;
	std::vector<SendRequest> _queue;
	std::unique_ptr<PendingRequest> _compaction;
	std::unique_ptr<PendingRequest> _titleRequest;
	std::optional<ProviderSettings> _loopProvider;
	int _loopBase = 0;
	int _loopContext = 0;
	int _undoneReported = 0;
	crl::time _thinkingStarted = 0;
	crl::time _stepStarted = 0;
	std::vector<int> _thinkingDurations;
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
