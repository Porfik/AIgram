/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "agent/core/agent_spending.h"
#include "agent/core/agent_types.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QStringList>
#include <QtCore/QTimeZone>

#include <optional>
#include <vector>

namespace Agent {

inline constexpr auto kTaskMinIntervalMinutes = 15;
inline constexpr auto kTaskMaxRuns = 50;
inline constexpr auto kTaskMaxQueued = 20;
inline constexpr auto kTaskMaxParallelRuns = 3;
inline constexpr auto kTaskMaxSteps = 20;
inline constexpr auto kTaskCardExpireSeconds = 24 * 3600;
inline constexpr auto kTaskMaxTasks = 100;
inline constexpr auto kTaskMaxTitleChars = 60;
inline constexpr auto kTaskMaxInstructionChars = 4000;
inline constexpr auto kTaskMaxKeywords = 50;
inline constexpr auto kTaskMaxTimes = 24;

enum ChatKindFlag : uchar {
	kChatKindPrivate = 0x01,
	kChatKindGroup = 0x02,
	kChatKindChannel = 0x04,
	kChatKindBot = 0x08,
};

struct ChatSet {
	std::vector<PeerKey> peers;
	uchar kinds = 0;
	std::vector<int> folders;
	bool triggerChat = false;

	[[nodiscard]] bool empty() const;

	friend inline bool operator==(const ChatSet &, const ChatSet &) = default;
};

struct ChatFacts {
	PeerKey peer = 0;
	uchar kind = 0;
	std::vector<int> folders;
	bool self = false;
};

[[nodiscard]] bool ChatSetContains(
	const ChatSet &set,
	const ChatFacts &chat,
	PeerKey triggerPeer = 0);

enum TaskMediaFlag : ushort {
	kTaskMediaText = 0x001,
	kTaskMediaPhoto = 0x002,
	kTaskMediaVideo = 0x004,
	kTaskMediaVoice = 0x008,
	kTaskMediaRound = 0x010,
	kTaskMediaAudio = 0x020,
	kTaskMediaDocument = 0x040,
	kTaskMediaSticker = 0x080,
	kTaskMediaGif = 0x100,
};

struct TimeWindow {
	int from = 0;
	int till = 0;
	uchar days = 0;

	[[nodiscard]] bool always() const;

	friend inline bool operator==(
		const TimeWindow &,
		const TimeWindow &) = default;
};

[[nodiscard]] bool InTimeWindow(
	const TimeWindow &window,
	const QDateTime &local);

struct MessageFilter {
	ChatSet chats;
	std::vector<PeerKey> senders;
	QStringList keywords;
	QString regex;
	ushort media = 0;
	bool mentionsOnly = false;
	TimeWindow window;

	friend inline bool operator==(
		const MessageFilter &,
		const MessageFilter &) = default;
};

struct MessageFacts {
	ChatFacts chat;
	PeerKey sender = 0;
	bool outgoing = false;
	bool fromAgent = false;
	QString text;
	ushort media = kTaskMediaText;
	bool mentionsMe = false;
	QDateTime local;
};

[[nodiscard]] bool MatchesMessage(
	const MessageFilter &filter,
	const MessageFacts &message);

enum class ScheduleKind : uchar {
	Interval,
	Times,
	Once,
};

struct Schedule {
	ScheduleKind kind = ScheduleKind::Interval;
	int everyMinutes = 60;
	std::vector<int> times;
	uchar days = 0;
	qint64 at = 0;

	friend inline bool operator==(const Schedule &, const Schedule &) = default;
};

[[nodiscard]] qint64 NextFireTime(
	const Schedule &schedule,
	qint64 anchor,
	qint64 after,
	const QTimeZone &zone);

enum class TriggerKind : uchar {
	Message,
	Schedule,
};

struct TaskTrigger {
	TriggerKind kind = TriggerKind::Message;
	MessageFilter filter;
	Schedule schedule;

	friend inline bool operator==(
		const TaskTrigger &,
		const TaskTrigger &) = default;
};

struct TaskFrame {
	std::vector<QString> tools;
	ChatSet chats;

	friend inline bool operator==(const TaskFrame &, const TaskFrame &) = default;
};

struct TaskLimits {
	double dailyCost = 0.25;
	qint64 dailyTokens = 250'000;
	int actionsPerHour = 30;

	friend inline bool operator==(
		const TaskLimits &,
		const TaskLimits &) = default;
};

enum class TaskNotify : uchar {
	Actions,
	LogOnly,
};

enum class TaskState : uchar {
	Active,
	Paused,
	DailyLimit,
	HourlyLimit,
	Finished,
};

struct Task {
	QString id;
	Scope scope;
	QString title;
	QString instruction;
	TaskTrigger trigger;
	TaskFrame frame;
	QString providerId;
	TaskLimits limits;
	TaskNotify notify = TaskNotify::Actions;
	bool catchUp = false;
	TaskState state = TaskState::Active;
	qint64 pausedUntil = 0;
	qint64 created = 0;
	qint64 lastFired = 0;
	qint64 lastSeen = 0;
	int spendingDay = 0;
	SpendingTotals spentToday;
};

enum class RunOutcome : uchar {
	Running,
	Waiting,
	Done,
	Failed,
	Interrupted,
};

struct TaskRun {
	QString sessionId;
	TriggerKind kind = TriggerKind::Message;
	PeerKey chat = 0;
	PeerKey sender = 0;
	int64 messageId = 0;
	QString preview;
	int missed = 0;
	qint64 started = 0;
	qint64 finished = 0;
	RunOutcome outcome = RunOutcome::Running;
	int actions = 0;
	Usage usage;
	QString error;
};

[[nodiscard]] QString GenerateTaskId();

[[nodiscard]] QByteArray SerializeTask(const Task &task);
[[nodiscard]] std::optional<Task> DeserializeTask(const QByteArray &bytes);
[[nodiscard]] QByteArray SerializeTasks(const std::vector<Task> &tasks);
[[nodiscard]] std::optional<std::vector<Task>> DeserializeTasks(
	const QByteArray &bytes);
[[nodiscard]] QByteArray SerializeRuns(const std::vector<TaskRun> &runs);
[[nodiscard]] std::optional<std::vector<TaskRun>> DeserializeRuns(
	const QByteArray &bytes);

struct TaskRefs {
	Fn<PeerKey(const QJsonValue &)> chat;
	Fn<std::optional<int>(const QJsonValue &)> folder;
	Fn<bool(const QString &)> actionTool;
	QTimeZone zone;
	qint64 now = 0;
};

struct TaskParse {
	std::optional<Task> task;
	QString error;
};

[[nodiscard]] QJsonObject TaskInputSchema(bool chatScope, bool update);
[[nodiscard]] TaskParse ParseTaskInput(
	const QJsonObject &input,
	const Scope &scope,
	const TaskRefs &refs);
[[nodiscard]] TaskParse ParseTaskUpdate(
	const Task &base,
	const QJsonObject &input,
	const TaskRefs &refs);
[[nodiscard]] QJsonObject TaskToJson(
	const Task &task,
	const QTimeZone &zone);

enum class LimitHit : uchar {
	None,
	Task,
	Background,
};

[[nodiscard]] LimitHit CheckSpendingLimits(
	const TaskLimits &limits,
	const SpendingTotals &taskToday,
	double backgroundCost,
	qint64 backgroundTokens,
	const SpendingTotals &backgroundToday);
[[nodiscard]] int CountRecentActions(
	const std::vector<qint64> &times,
	qint64 now);
[[nodiscard]] qint64 HourlyResumeTime(
	const std::vector<qint64> &times,
	int perHour,
	qint64 now);
[[nodiscard]] int CallActionCount(const QJsonObject &input);
[[nodiscard]] std::vector<qint64> RunActionTimes(
	const std::vector<TaskRun> &runs,
	qint64 now);

} // namespace Agent
