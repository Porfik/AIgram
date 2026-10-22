/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_engine.h"
#include "agent/core/agent_task.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

#include <deque>
#include <memory>
#include <vector>

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class Manager;
class Session;

struct WaitingRun {
	QString taskId;
	QString sessionId;
	int cards = 0;
	qint64 since = 0;
};

[[nodiscard]] ChatFacts MakeChatFacts(
	not_null<Main::Session*> session,
	PeerKey peer);

class TaskRunner final : public base::has_weak_ptr {
public:
	explicit TaskRunner(not_null<Manager*> manager);
	~TaskRunner();

	[[nodiscard]] std::vector<WaitingRun> waiting(const Scope &scope) const;
	[[nodiscard]] rpl::producer<> waitingChanges() const;
	[[nodiscard]] bool running(const QString &taskId) const;
	[[nodiscard]] rpl::producer<QString> runningChanges() const;

	void taskRemoved(const QString &taskId);
	void stopAll();
	void runSaved(not_null<Session*> session);

private:
	struct Trigger {
		TriggerKind kind = TriggerKind::Message;
		PeerKey chat = 0;
		PeerKey sender = 0;
		int64 messageId = 0;
		QString preview;
		qint64 date = 0;
		std::vector<std::pair<PeerKey, int64>> missed;
		int missedSchedules = 0;
	};
	struct Run {
		QString taskId;
		not_null<Session*> session;
		TaskRun info;
		qint64 waitingSince = 0;
		bool finished = false;
		base::flat_map<QString, ToolUseBlock> calls;
		rpl::lifetime lifetime;
	};

	[[nodiscard]] Main::Session &session() const;
	void handleItem(not_null<HistoryItem*> item);
	[[nodiscard]] MessageFacts messageFacts(
		not_null<HistoryItem*> item) const;
	void enqueue(const Task &task, Trigger trigger);
	void dropTrigger(const Task &task);
	void processQueues();
	[[nodiscard]] bool checkCanRun(const Task &task);
	void startRun(const Task &task, Trigger trigger);
	[[nodiscard]] QString triggerHint(
		const Task &task,
		const Trigger &trigger) const;
	[[nodiscard]] QString triggerData(const Trigger &trigger) const;
	[[nodiscard]] QString frameText(const Task &task) const;
	void handleState(not_null<Run*> run);
	void handleEvent(not_null<Run*> run, const EngineEvent &event);
	void countAction(not_null<Run*> run, const ToolUseBlock &call);
	[[nodiscard]] std::vector<qint64> &actionTimes(const QString &taskId);
	void finishRun(not_null<Run*> run);
	[[nodiscard]] Run *findRun(const QString &sessionId) const;
	[[nodiscard]] Run *findTaskRun(const QString &taskId) const;
	void saveRunInfo(not_null<Run*> run);
	void removeRun(not_null<Run*> run);

	void rescheduleLater();
	void reschedule();
	void flushMissed();
	void checkExpired();
	void refreshPauses();
	void pauseTask(
		const QString &taskId,
		TaskState state,
		qint64 until,
		const QString &text);
	void notify(
		const Task &task,
		const QString &sessionId,
		PeerKey peer,
		const QString &text,
		bool sound);
	[[nodiscard]] QString actionText(
		const ToolUseBlock &call,
		PeerKey peer) const;

	const not_null<Manager*> _manager;
	const qint64 _startedAt = 0;
	std::vector<std::unique_ptr<Run>> _runs;
	base::flat_map<QString, std::deque<Trigger>> _queues;
	base::flat_map<QString, std::vector<qint64>> _actions;
	base::flat_map<
		QString,
		std::vector<std::pair<PeerKey, int64>>> _missed;
	std::deque<QString> _finished;
	base::Timer _scheduleTimer;
	base::Timer _expireTimer;
	base::Timer _missedTimer;
	base::Timer _pauseTimer;
	bool _scheduled = false;
	bool _rescheduling = false;
	bool _startupChecked = false;
	int _backgroundNotifiedDay = 0;
	rpl::event_stream<> _waitingChanges;
	rpl::event_stream<QString> _runningChanges;
	rpl::lifetime _lifetime;

};

} // namespace Agent
