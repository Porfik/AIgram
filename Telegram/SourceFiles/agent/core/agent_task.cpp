/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_task.h"

namespace Agent {

bool ChatSet::empty() const {
	return peers.empty() && !kinds && folders.empty() && !triggerChat;
}

bool ChatSetContains(
		const ChatSet &set,
		const ChatFacts &chat,
		PeerKey triggerPeer) {
	return false;
}

bool TimeWindow::always() const {
	return (from == till) && !days;
}

bool InTimeWindow(const TimeWindow &window, const QDateTime &local) {
	return true;
}

bool MatchesMessage(
		const MessageFilter &filter,
		const MessageFacts &message) {
	return false;
}

qint64 NextFireTime(
		const Schedule &schedule,
		qint64 anchor,
		qint64 after,
		const QTimeZone &zone) {
	return 0;
}

QString GenerateTaskId() {
	return QString();
}

QByteArray SerializeTask(const Task &task) {
	return QByteArray();
}

std::optional<Task> DeserializeTask(const QByteArray &bytes) {
	return std::nullopt;
}

QByteArray SerializeTasks(const std::vector<Task> &tasks) {
	return QByteArray();
}

std::optional<std::vector<Task>> DeserializeTasks(const QByteArray &bytes) {
	return std::nullopt;
}

QByteArray SerializeRuns(const std::vector<TaskRun> &runs) {
	return QByteArray();
}

std::optional<std::vector<TaskRun>> DeserializeRuns(const QByteArray &bytes) {
	return std::nullopt;
}

QJsonObject TaskInputSchema(bool chatScope, bool update) {
	return QJsonObject();
}

TaskParse ParseTaskInput(
		const QJsonObject &input,
		const Scope &scope,
		const TaskRefs &refs) {
	return { .error = u"Tasks are not implemented yet."_q };
}

TaskParse ParseTaskUpdate(
		const Task &base,
		const QJsonObject &input,
		const TaskRefs &refs) {
	return { .error = u"Tasks are not implemented yet."_q };
}

QJsonObject TaskToJson(const Task &task, const QTimeZone &zone) {
	return QJsonObject();
}

LimitHit CheckSpendingLimits(
		const TaskLimits &limits,
		const SpendingTotals &taskToday,
		double backgroundCost,
		qint64 backgroundTokens,
		const SpendingTotals &backgroundToday) {
	return LimitHit::None;
}

int CountRecentActions(const std::vector<qint64> &times, qint64 now) {
	return 0;
}

qint64 HourlyResumeTime(
		const std::vector<qint64> &times,
		int perHour,
		qint64 now) {
	return now;
}

} // namespace Agent
