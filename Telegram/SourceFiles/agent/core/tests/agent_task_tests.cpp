/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_task.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>

namespace Agent::Tests {
namespace {

[[nodiscard]] QTimeZone Zone() {
	return QTimeZone(3 * 3600);
}

[[nodiscard]] QDateTime At(int year, int month, int day, int hour, int minute) {
	return QDateTime(QDate(year, month, day), QTime(hour, minute), Zone());
}

TaskRefs SampleRefs() {
	auto refs = TaskRefs();
	refs.chat = [](const QJsonValue &value) -> PeerKey {
		const auto id = value.isString()
			? (value.toString() == u"alpha"_q
				? 111
				: value.toString() == u"beta"_q
				? 222
				: value.toString() == u"gamma"_q
				? 333
				: 0)
			: qint64(value.toDouble());
		return (id == 111 || id == 222 || id == 333) ? PeerKey(id) : 0;
	};
	refs.folder = [](const QJsonValue &value) -> std::optional<int> {
		const auto id = value.isString()
			? (value.toString() == u"work"_q ? 1 : 2)
			: int(value.toDouble());
		return (id == 1 || id == 2) ? std::optional<int>(id) : std::nullopt;
	};
	refs.actionTool = [](const QString &name) {
		return name == u"send_message"_q || name == u"set_mute"_q;
	};
	refs.zone = Zone();
	refs.now = At(2026, 7, 18, 12, 0).toSecsSinceEpoch();
	return refs;
}

Task SampleTask() {
	auto task = Task();
	task.id = u"0011223344556677"_q;
	task.scope = Scope::Account();
	task.title = u"Weather report"_q;
	task.instruction = u"Summarize the weather in the morning chats."_q;
	task.trigger.kind = TriggerKind::Message;
	task.trigger.filter.chats.peers = { 111 };
	task.trigger.filter.chats.kinds = kChatKindGroup;
	task.trigger.filter.chats.folders = { 1 };
	task.trigger.filter.senders = { 222 };
	task.trigger.filter.keywords = QStringList{ u"rain"_q, u"snow"_q };
	task.trigger.filter.regex = u"storm|wind"_q;
	task.trigger.filter.media = kTaskMediaPhoto | kTaskMediaVideo;
	task.trigger.filter.mentionsOnly = false;
	task.trigger.filter.window = { .from = 9 * 60, .till = 18 * 60 };
	task.frame.tools = { u"send_message"_q };
	task.frame.chats.peers = { 333 };
	task.frame.chats.folders = { 2 };
	task.frame.chats.triggerChat = true;
	task.providerId = u"provider"_q;
	task.limits = { 0.5, 50'000, 10 };
	task.notify = TaskNotify::LogOnly;
	task.catchUp = true;
	task.state = TaskState::Paused;
	task.pausedUntil = 1'700'000'000;
	task.created = 1'700'000'100;
	task.lastFired = 1'700'000'200;
	task.lastSeen = 1'700'000'300;
	task.spendingDay = 2460673;
	task.spentToday = SpendingTotals{
		.inputTokens = 100,
		.outputTokens = 200,
		.cachedInputTokens = 10,
		.cost = 0.03,
		.hasCost = true,
		.requests = 2,
	};
	return task;
}

TaskRun SampleRun(int index) {
	auto run = TaskRun();
	run.sessionId = u"session%1"_q.arg(index);
	run.kind = index ? TriggerKind::Schedule : TriggerKind::Message;
	run.chat = 111 + index;
	run.sender = 222 + index;
	run.messageId = 1000 + index;
	run.preview = u"preview %1"_q.arg(index);
	run.missed = index;
	run.started = 1'700'000'000 + index;
	run.finished = 1'700'000'500 + index;
	run.outcome = RunOutcome(index % 5);
	run.actions = 3 + index;
	run.usage = Usage{ 10 + index, 20 + index, 2, 0.01 * index, index > 0 };
	run.error = index ? u"boom"_q : QString();
	return run;
}

QString TotalsDiffers(const SpendingTotals &a, const SpendingTotals &b) {
	if (a.inputTokens != b.inputTokens
		|| a.outputTokens != b.outputTokens
		|| a.cachedInputTokens != b.cachedInputTokens
		|| a.cost != b.cost
		|| a.hasCost != b.hasCost
		|| a.requests != b.requests) {
		return u"spentToday differs"_q;
	}
	return QString();
}

QString TaskDiffers(const Task &a, const Task &b) {
	if (a.id != b.id) {
		return u"id differs"_q;
	} else if (!(a.scope == b.scope)) {
		return u"scope differs"_q;
	} else if (a.title != b.title) {
		return u"title differs"_q;
	} else if (a.instruction != b.instruction) {
		return u"instruction differs"_q;
	} else if (!(a.trigger == b.trigger)) {
		return u"trigger differs"_q;
	} else if (!(a.frame == b.frame)) {
		return u"frame differs"_q;
	} else if (a.providerId != b.providerId) {
		return u"providerId differs"_q;
	} else if (!(a.limits == b.limits)) {
		return u"limits differs"_q;
	} else if (a.notify != b.notify) {
		return u"notify differs"_q;
	} else if (a.catchUp != b.catchUp) {
		return u"catchUp differs"_q;
	} else if (a.state != b.state) {
		return u"state differs"_q;
	} else if (a.pausedUntil != b.pausedUntil) {
		return u"pausedUntil differs"_q;
	} else if (a.created != b.created) {
		return u"created differs"_q;
	} else if (a.lastFired != b.lastFired) {
		return u"lastFired differs"_q;
	} else if (a.lastSeen != b.lastSeen) {
		return u"lastSeen differs"_q;
	} else if (a.spendingDay != b.spendingDay) {
		return u"spendingDay differs"_q;
	}
	return TotalsDiffers(a.spentToday, b.spentToday);
}

QString RunDiffers(const TaskRun &a, const TaskRun &b) {
	if (a.sessionId != b.sessionId
		|| a.kind != b.kind
		|| a.chat != b.chat
		|| a.sender != b.sender
		|| a.messageId != b.messageId
		|| a.preview != b.preview
		|| a.missed != b.missed
		|| a.started != b.started
		|| a.finished != b.finished
		|| a.outcome != b.outcome
		|| a.actions != b.actions
		|| a.usage.inputTokens != b.usage.inputTokens
		|| a.usage.outputTokens != b.usage.outputTokens
		|| a.usage.cachedInputTokens != b.usage.cachedInputTokens
		|| a.usage.cost != b.usage.cost
		|| a.usage.hasCost != b.usage.hasCost
		|| a.error != b.error) {
		return u"run differs"_q;
	}
	return QString();
}

MessageFacts SampleMessage() {
	auto message = MessageFacts();
	message.chat = ChatFacts{ .peer = 333, .kind = kChatKindGroup };
	message.sender = 444;
	message.text = u"hello world"_q;
	message.local = At(2026, 7, 20, 11, 0);
	return message;
}

std::vector<Check> FilterChecks() {
	return {
		{ u"task_chat_set_peers"_q, [] {
			auto set = ChatSet();
			set.peers = { 333 };
			const auto chat = ChatFacts{ .peer = 333 };
			if (!ChatSetContains(set, chat)) {
				return u"listed peer must match"_q;
			} else if (ChatSetContains(set, ChatFacts{ .peer = 444 })) {
				return u"other peer must not match"_q;
			} else if (ChatSetContains(set, ChatFacts{ .peer = 0 })) {
				return u"peer 0 must never match"_q;
			}
			return QString();
		} },
		{ u"task_chat_set_kinds"_q, [] {
			auto set = ChatSet();
			set.kinds = kChatKindGroup;
			if (!ChatSetContains(set, ChatFacts{ .peer = 1, .kind = kChatKindGroup })) {
				return u"kind must match"_q;
			} else if (ChatSetContains(
					set,
					ChatFacts{ .peer = 1, .kind = kChatKindPrivate })) {
				return u"other kind must not match"_q;
			} else if (ChatSetContains(set, ChatFacts{
					.peer = 1,
					.kind = kChatKindGroup,
					.self = true })) {
				return u"self must be excluded from kinds"_q;
			}
			return QString();
		} },
		{ u"task_chat_set_folders"_q, [] {
			auto set = ChatSet();
			set.folders = { 1 };
			if (!ChatSetContains(set, ChatFacts{ .peer = 1, .folders = { 2, 1 } })) {
				return u"one of the folders must match"_q;
			} else if (ChatSetContains(set, ChatFacts{ .peer = 1, .folders = { 3 } })) {
				return u"unknown folder must not match"_q;
			}
			return QString();
		} },
		{ u"task_chat_set_trigger_chat"_q, [] {
			auto set = ChatSet();
			set.triggerChat = true;
			const auto chat = ChatFacts{ .peer = 333 };
			if (!ChatSetContains(set, chat, 333)) {
				return u"trigger chat must match"_q;
			} else if (ChatSetContains(set, chat)) {
				return u"trigger chat without trigger peer must not match"_q;
			} else if (ChatSetContains(set, chat, 444)) {
				return u"other trigger peer must not match"_q;
			}
			set.triggerChat = false;
			if (ChatSetContains(set, chat, 333)) {
				return u"triggerChat flag must be required"_q;
			}
			return QString();
		} },
		{ u"task_time_window_plain"_q, [] {
			const auto window = TimeWindow{ .from = 600, .till = 720 };
			if (!InTimeWindow(window, At(2026, 7, 20, 11, 0))) {
				return u"inside time must match"_q;
			} else if (InTimeWindow(window, At(2026, 7, 20, 12, 0))) {
				return u"till must be exclusive"_q;
			} else if (InTimeWindow(window, At(2026, 7, 20, 9, 59))) {
				return u"before from must not match"_q;
			}
			return QString();
		} },
		{ u"task_time_window_over_midnight_days"_q, [] {
			const auto monday = QDate(2026, 7, 20);
			const auto mondayBit = uchar(1) << (monday.dayOfWeek() - 1);
			const auto window = TimeWindow{
				.from = 22 * 60,
				.till = 2 * 60,
				.days = mondayBit,
			};
			if (!InTimeWindow(window, QDateTime(monday, QTime(23, 0), Zone()))) {
				return u"late evening on the day must match"_q;
			} else if (!InTimeWindow(
					window,
					QDateTime(monday.addDays(1), QTime(1, 0), Zone()))) {
				return u"early morning must count as the previous day"_q;
			} else if (InTimeWindow(
					window,
					QDateTime(monday, QTime(1, 0), Zone()))) {
				return u"early morning on the day itself must not match"_q;
			} else if (InTimeWindow(
					window,
					QDateTime(monday.addDays(1), QTime(12, 0), Zone()))) {
				return u"day time outside the window must not match"_q;
			}
			return QString();
		} },
		{ u"task_time_window_always"_q, [] {
			const auto window = TimeWindow();
			if (!window.always()) {
				return u"default window must be always"_q;
			} else if (!InTimeWindow(window, At(2026, 7, 20, 3, 0))) {
				return u"always window must match any time"_q;
			}
			auto days = TimeWindow{ .from = 600, .till = 720, .days = 0x7F };
			if (days.always()) {
				return u"days must disable always"_q;
			}
			days = TimeWindow{ .from = 600, .till = 600 };
			if (!days.always()) {
				return u"equal from and till must be always"_q;
			} else if (!InTimeWindow(days, At(2026, 7, 20, 23, 0))) {
				return u"equal from and till must match any time"_q;
			}
			return QString();
		} },
		{ u"task_filter_agent_and_outgoing"_q, [] {
			auto filter = MessageFilter();
			filter.chats.peers = { 555 };
			auto message = SampleMessage();
			message.chat = ChatFacts{ .peer = 555, .self = true };
			message.outgoing = true;
			if (!MatchesMessage(filter, message)) {
				return u"own message in Saved Messages must match"_q;
			}
			message.chat = ChatFacts{ .peer = 333, .kind = kChatKindGroup };
			if (MatchesMessage(filter, message)) {
				return u"own message outside Saved Messages must not match"_q;
			}
			message.chat = ChatFacts{ .peer = 555, .self = true };
			message.fromAgent = true;
			if (MatchesMessage(filter, message)) {
				return u"agent messages must never match"_q;
			}
			message.fromAgent = false;
			auto listed = MessageFilter();
			listed.chats.peers = { 333 };
			if (MatchesMessage(listed, message)) {
				return u"own message must be named in chats.peers"_q;
			}
			auto kinds = MessageFilter();
			kinds.chats.kinds = kChatKindGroup;
			if (MatchesMessage(kinds, message)) {
				return u"own message must match only by peers"_q;
			}
			return QString();
		} },
		{ u"task_filter_senders_and_empty_chats"_q, [] {
			auto filter = MessageFilter();
			filter.chats.peers = { 333 };
			filter.senders = { 444 };
			auto message = SampleMessage();
			if (!MatchesMessage(filter, message)) {
				return u"listed sender must match"_q;
			}
			message.sender = 555;
			if (MatchesMessage(filter, message)) {
				return u"other sender must not match"_q;
			}
			filter.senders.clear();
			if (!MatchesMessage(filter, message)) {
				return u"any sender must match without senders"_q;
			}
			auto empty = MessageFilter();
			if (MatchesMessage(empty, SampleMessage())) {
				return u"an empty chat set must match no chat"_q;
			}
			return QString();
		} },
		{ u"task_filter_keywords"_q, [] {
			auto filter = MessageFilter();
			filter.chats.peers = { 333 };
			filter.keywords = QStringList{ u"world"_q, u"peace"_q };
			auto message = SampleMessage();
			if (!MatchesMessage(filter, message)) {
				return u"keyword must match as substring"_q;
			}
			message.text = u"HELLO WORLD"_q;
			if (!MatchesMessage(filter, message)) {
				return u"keywords must be case-insensitive"_q;
			}
			message.text = u"nothing here"_q;
			if (MatchesMessage(filter, message)) {
				return u"message without keywords must not match"_q;
			}
			return QString();
		} },
		{ u"task_filter_regex"_q, [] {
			auto filter = MessageFilter();
			filter.chats.peers = { 333 };
			filter.regex = u"hel+o"_q;
			auto message = SampleMessage();
			if (!MatchesMessage(filter, message)) {
				return u"regex must match"_q;
			}
			message.text = u"no match here"_q;
			if (MatchesMessage(filter, message)) {
				return u"regex must not match other text"_q;
			}
			filter.regex = u"("_q;
			if (MatchesMessage(filter, message)) {
				return u"an invalid regex must match nothing"_q;
			}
			filter.keywords = QStringList{ u"here"_q };
			if (!MatchesMessage(filter, message)) {
				return u"keywords must be enough when regex is invalid"_q;
			}
			filter.regex = u"world|peace"_q;
			filter.keywords = QStringList{ u"peace"_q };
			message.text = u"say world"_q;
			if (!MatchesMessage(filter, message)) {
				return u"either keywords or regex must be enough"_q;
			}
			return QString();
		} },
		{ u"task_filter_media_and_mentions"_q, [] {
			auto filter = MessageFilter();
			filter.chats.peers = { 333 };
			filter.media = kTaskMediaPhoto | kTaskMediaVoice;
			auto message = SampleMessage();
			message.media = kTaskMediaPhoto;
			if (!MatchesMessage(filter, message)) {
				return u"matching media must pass"_q;
			}
			message.media = kTaskMediaText;
			if (MatchesMessage(filter, message)) {
				return u"other media must not pass"_q;
			}
			filter.media = 0;
			if (!MatchesMessage(filter, message)) {
				return u"no media condition must pass any media"_q;
			}
			filter.mentionsOnly = true;
			if (MatchesMessage(filter, message)) {
				return u"mentions only must require a mention"_q;
			}
			message.mentionsMe = true;
			if (!MatchesMessage(filter, message)) {
				return u"a mention must pass mentions only"_q;
			}
			return QString();
		} },
		{ u"task_filter_window"_q, [] {
			auto filter = MessageFilter();
			filter.chats.peers = { 333 };
			filter.window = { .from = 600, .till = 720 };
			auto message = SampleMessage();
			if (!MatchesMessage(filter, message)) {
				return u"message inside the window must match"_q;
			}
			message.local = At(2026, 7, 20, 13, 0);
			if (MatchesMessage(filter, message)) {
				return u"message outside the window must not match"_q;
			}
			return QString();
		} },
		{ u"task_next_interval"_q, [] {
			const auto anchor = qint64(1'700'000'000);
			auto schedule = Schedule();
			schedule.kind = ScheduleKind::Interval;
			schedule.everyMinutes = 30;
			if (NextFireTime(schedule, anchor, anchor - 100, Zone()) != anchor) {
				return u"the anchor itself must fire when after is earlier"_q;
			}
			if (NextFireTime(schedule, anchor, anchor + 3600, Zone())
				!= anchor + 5400) {
				return u"interval must step from the anchor"_q;
			}
			if (NextFireTime(schedule, anchor, anchor + 5400, Zone())
				!= anchor + 7200) {
				return u"interval must fire strictly after after"_q;
			}
			schedule.everyMinutes = 5;
			if (NextFireTime(schedule, anchor, anchor, Zone())
				!= anchor + kTaskMinIntervalMinutes * 60) {
				return u"interval must be clamped to the minimum"_q;
			}
			return QString();
		} },
		{ u"task_next_times_midnight_and_days"_q, [] {
			auto schedule = Schedule();
			schedule.kind = ScheduleKind::Times;
			schedule.times = { 22 * 60, 8 * 60 };
			const auto after = At(2026, 7, 20, 23, 0).toSecsSinceEpoch();
			if (NextFireTime(schedule, 0, after, Zone())
				!= At(2026, 7, 21, 8, 0).toSecsSinceEpoch()) {
				return u"times must cross midnight to the next morning"_q;
			}
			const auto evening = At(2026, 7, 20, 21, 0).toSecsSinceEpoch();
			if (NextFireTime(schedule, 0, evening, Zone())
				!= At(2026, 7, 20, 22, 0).toSecsSinceEpoch()) {
				return u"times must pick the later time of the same day"_q;
			}
			const auto morning = At(2026, 7, 20, 7, 0).toSecsSinceEpoch();
			if (NextFireTime(schedule, 0, morning, Zone())
				!= At(2026, 7, 20, 8, 0).toSecsSinceEpoch()) {
				return u"times must pick the next time of the same day"_q;
			}
			const auto monday = QDate(2026, 7, 20);
			auto mondayOnly = Schedule();
			mondayOnly.kind = ScheduleKind::Times;
			mondayOnly.times = { 8 * 60 };
			mondayOnly.days = uchar(1) << (monday.dayOfWeek() - 1);
			auto expected = QDateTime();
			for (auto offset = 1; offset != 9; ++offset) {
				const auto date = monday.addDays(offset);
				if (date.dayOfWeek() == monday.dayOfWeek()) {
					expected = QDateTime(date, QTime(8, 0), Zone());
					break;
				}
			}
			if (!expected.isValid()
				|| NextFireTime(mondayOnly, 0, evening, Zone())
					!= expected.toSecsSinceEpoch()) {
				return u"days must push the fire to the next matching day"_q;
			}
			auto none = Schedule();
			none.kind = ScheduleKind::Times;
			if (NextFireTime(none, 0, after, Zone()) != 0) {
				return u"no times must give no fire time"_q;
			}
			return QString();
		} },
		{ u"task_next_once"_q, [] {
			auto schedule = Schedule();
			schedule.kind = ScheduleKind::Once;
			schedule.at = 1'700'000'500;
			if (NextFireTime(schedule, 0, 1'700'000'000, Zone())
				!= schedule.at) {
				return u"a future once time must fire"_q;
			}
			if (NextFireTime(schedule, 0, schedule.at, Zone()) != 0) {
				return u"once must fire strictly after after"_q;
			}
			if (NextFireTime(schedule, 0, schedule.at + 1, Zone()) != 0) {
				return u"a past once time must not fire"_q;
			}
			return QString();
		} },
	};
}

std::vector<Check> SerializeChecks() {
	return {
		{ u"task_serialize_task_roundtrip"_q, [] {
			const auto task = SampleTask();
			const auto restored = DeserializeTask(SerializeTask(task));
			if (!restored) {
				return u"task failed to deserialize"_q;
			}
			return TaskDiffers(task, *restored);
		} },
		{ u"task_serialize_runs_roundtrip"_q, [] {
			const auto runs = { SampleRun(0), SampleRun(1), SampleRun(4) };
			const auto list = std::vector<TaskRun>(runs.begin(), runs.end());
			const auto restored = DeserializeRuns(SerializeRuns(list));
			if (!restored) {
				return u"runs failed to deserialize"_q;
			} else if (restored->size() != list.size()) {
				return u"run count differs"_q;
			}
			for (auto i = 0; i != int(list.size()); ++i) {
				if (const auto error = RunDiffers(list[i], (*restored)[i]);
					!error.isEmpty()) {
					return error;
				}
			}
			return QString();
		} },
		{ u"task_serialize_truncated_and_bad_version"_q, [] {
			auto bytes = SerializeTask(SampleTask());
			if (DeserializeTask(bytes.left(bytes.size() - 1))) {
				return u"truncated task must not deserialize"_q;
			}
			{
				QDataStream stream(&bytes, QIODevice::ReadWrite);
				stream.setVersion(QDataStream::Qt_5_1);
				stream << quint32(7);
			}
			if (DeserializeTask(bytes)) {
				return u"a bad task version must not deserialize"_q;
			}
			const auto tasks = std::vector<Task>{ SampleTask() };
			const auto taskBytes = SerializeTasks(tasks);
			if (DeserializeTasks(taskBytes.left(taskBytes.size() - 1))) {
				return u"truncated tasks must not deserialize"_q;
			}
			const auto runBytes = SerializeRuns({ SampleRun(0) });
			if (DeserializeRuns(runBytes.left(runBytes.size() - 1))) {
				return u"truncated runs must not deserialize"_q;
			}
			return QString();
		} },
		{ u"task_serialize_count_limits"_q, [] {
			auto bytes = QByteArray();
			QDataStream stream(&bytes, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_5_1);
			stream << quint32(1) << qint32(kTaskMaxTasks + 1);
			if (DeserializeTasks(bytes)) {
				return u"too many tasks must not deserialize"_q;
			}
			auto runBytes = QByteArray();
			QDataStream runStream(&runBytes, QIODevice::WriteOnly);
			runStream.setVersion(QDataStream::Qt_5_1);
			runStream << quint32(1) << qint32(kTaskMaxRuns * 2 + 1);
			if (DeserializeRuns(runBytes)) {
				return u"too many runs must not deserialize"_q;
			}
			return QString();
		} },
	};
}

[[nodiscard]] QJsonObject MessageTaskInput() {
	return QJsonObject{
		{ u"title"_q, u"T"_q },
		{ u"instruction"_q, u"I"_q },
		{ u"trigger"_q, QJsonObject{
			{ u"type"_q, u"message"_q },
			{ u"chats"_q, QJsonArray{ 111 } },
		} },
	};
}

[[nodiscard]] bool Errors(const TaskParse &parse, const QString &part) {
	return parse.task.has_value() || !parse.error.contains(part);
}

std::vector<Check> ParseChecks() {
	return {
		{ u"task_parse_message_full"_q, [] {
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"chats"_q, QJsonArray{ u"alpha"_q } },
				{ u"chat_types"_q, QJsonArray{ u"group"_q } },
				{ u"folders"_q, QJsonArray{ u"work"_q } },
				{ u"senders"_q, QJsonArray{ 222 } },
				{ u"keywords"_q, QJsonArray{ u"rain"_q } },
				{ u"regex"_q, u"storm|wind"_q },
				{ u"media"_q, QJsonArray{ u"photo"_q, u"video"_q } },
				{ u"mentions_only"_q, true },
				{ u"time_window"_q, QJsonObject{
					{ u"from"_q, u"09:00"_q },
					{ u"till"_q, u"18:00"_q },
					{ u"days"_q, QJsonArray{ u"mon"_q, u"tue"_q } },
				} },
			};
			input[u"allowed_actions"_q] = QJsonArray{ u"send_message"_q };
			input[u"allowed_chats"_q] = QJsonObject{
				{ u"chats"_q, QJsonArray{ 111 } },
				{ u"chat_types"_q, QJsonArray{ u"private"_q } },
				{ u"folders"_q, QJsonArray{ 2 } },
				{ u"trigger_chat"_q, true },
			};
			input[u"notify"_q] = u"log_only"_q;
			input[u"catch_up"_q] = true;
			input[u"limits"_q] = QJsonObject{
				{ u"daily_cost"_q, 0.5 },
				{ u"daily_tokens"_q, 50'000 },
				{ u"actions_per_hour"_q, 10 },
			};
			const auto parsed = ParseTaskInput(input, Scope::Account(), SampleRefs());
			if (!parsed.task) {
				return u"parse failed: "_q + parsed.error;
			}
			const auto &task = *parsed.task;
			const auto &filter = task.trigger.filter;
			if (task.trigger.kind != TriggerKind::Message) {
				return u"trigger kind differs"_q;
			} else if (filter.chats.peers != std::vector<PeerKey>{ 111 }) {
				return u"chats differ"_q;
			} else if (filter.chats.kinds != kChatKindGroup) {
				return u"chat types differ"_q;
			} else if (filter.chats.folders != std::vector<int>{ 1 }) {
				return u"folders differ"_q;
			} else if (filter.senders != std::vector<PeerKey>{ 222 }) {
				return u"senders differ"_q;
			} else if (filter.keywords != QStringList{ u"rain"_q }) {
				return u"keywords differ"_q;
			} else if (filter.regex != u"storm|wind"_q) {
				return u"regex differs"_q;
			} else if (filter.media != (kTaskMediaPhoto | kTaskMediaVideo)) {
				return u"media differs"_q;
			} else if (!filter.mentionsOnly) {
				return u"mentions only differ"_q;
			} else if (filter.window.from != 9 * 60
				|| filter.window.till != 18 * 60) {
				return u"window hours differ: %1 %2"_q
					.arg(filter.window.from)
					.arg(filter.window.till);
			} else if (filter.window.days != 0x03) {
				return u"window days differ"_q;
			} else if (task.frame.tools != std::vector<QString>{ u"send_message"_q }
				|| task.frame.chats.peers != std::vector<PeerKey>{ 111 }
				|| task.frame.chats.kinds != kChatKindPrivate
				|| task.frame.chats.folders != std::vector<int>{ 2 }
				|| !task.frame.chats.triggerChat) {
				return u"frame differs"_q;
			} else if (task.notify != TaskNotify::LogOnly
				|| !task.catchUp
				|| !(task.limits == TaskLimits{ 0.5, 50'000, 10 })) {
				return u"settings differ"_q;
			}
			return QString();
		} },
		{ u"task_parse_schedule_and_interval"_q, [] {
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"schedule"_q },
				{ u"times"_q, QJsonArray{ u"22:30"_q, u"08:00"_q, u"22:30"_q } },
				{ u"days"_q, QJsonArray{ u"sat"_q } },
			};
			const auto saturday = QDate(2026, 7, 25);
			const auto satBit = uchar(1) << (saturday.dayOfWeek() - 1);
			auto parsed = ParseTaskInput(input, Scope::Account(), SampleRefs());
			if (!parsed.task) {
				return u"times parse failed: "_q + parsed.error;
			}
			const auto &schedule = parsed.task->trigger.schedule;
			if (parsed.task->trigger.kind != TriggerKind::Schedule) {
				return u"schedule kind differs"_q;
			} else if (schedule.kind != ScheduleKind::Times) {
				return u"times kind differs"_q;
			} else if (schedule.times != std::vector<int>{ 8 * 60, 22 * 60 + 30 }) {
				auto text = u"times differ:"_q;
				for (const auto time : schedule.times) {
					text += u" %1"_q.arg(time);
				}
				return text;
			} else if (schedule.days != satBit) {
				return u"schedule days differ"_q;
			} else if (schedule.everyMinutes != 60) {
				return u"times every minutes differ"_q;
			}
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"schedule"_q },
				{ u"every_minutes"_q, 90 },
			};
			parsed = ParseTaskInput(input, Scope::Account(), SampleRefs());
			if (!parsed.task) {
				return u"interval parse failed: "_q + parsed.error;
			}
			const auto &interval = parsed.task->trigger.schedule;
			if (interval.kind != ScheduleKind::Interval
				|| interval.everyMinutes != 90) {
				return u"interval schedule differs"_q;
			}
			return QString();
		} },
		{ u"task_parse_once"_q, [] {
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"once"_q },
				{ u"at"_q, u"2026-07-21 10:00"_q },
			};
			const auto parsed = ParseTaskInput(input, Scope::Account(), SampleRefs());
			if (!parsed.task) {
				return u"parse failed: "_q + parsed.error;
			}
			const auto &schedule = parsed.task->trigger.schedule;
			if (schedule.kind != ScheduleKind::Once
				|| schedule.at != At(2026, 7, 21, 10, 0).toSecsSinceEpoch()) {
				return u"once time differs"_q;
			}
			return QString();
		} },
		{ u"task_parse_keywords_trimmed"_q, [] {
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"chats"_q, QJsonArray{ 111 } },
				{ u"keywords"_q, QJsonArray{ u" rain "_q, u""_q, u"  "_q } },
			};
			const auto parsed = ParseTaskInput(
				input,
				Scope::Account(),
				SampleRefs());
			if (!parsed.task) {
				return u"keywords parse failed: "_q + parsed.error;
			}
			if (parsed.task->trigger.filter.keywords
				!= QStringList{ u"rain"_q }) {
				return u"keywords must be trimmed and empty ones dropped"_q;
			}
			auto filter = MessageFilter();
			filter.chats.peers = { 333 };
			filter.keywords = QStringList{ u""_q };
			if (MatchesMessage(filter, SampleMessage())) {
				return u"an empty keyword must not match every message"_q;
			}
			filter.keywords = QStringList{ u""_q, u"world"_q };
			if (!MatchesMessage(filter, SampleMessage())) {
				return u"empty keywords must not block the real ones"_q;
			}
			return QString();
		} },
		{ u"task_parse_errors"_q, [] {
			const auto refs = SampleRefs();
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{ { u"type"_q, u"message"_q } };
			if (Errors(ParseTaskInput(input, Scope::Account(), refs), u"chats"_q)) {
				return u"message trigger without chats must fail"_q;
			}
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"chats"_q, QJsonArray{ 999 } },
			};
			if (Errors(ParseTaskInput(input, Scope::Account(), refs), u"Unknown chat"_q)) {
				return u"unknown chat must fail"_q;
			}
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"chat_types"_q, QJsonArray{ u"group"_q } },
				{ u"regex"_q, u"("_q },
			};
			if (Errors(ParseTaskInput(input, Scope::Account(), refs), u"regular expression"_q)) {
				return u"bad regex must fail"_q;
			}
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"schedule"_q },
				{ u"every_minutes"_q, 10 },
			};
			if (Errors(ParseTaskInput(input, Scope::Account(), refs), u"at least"_q)) {
				return u"interval below minimum must fail"_q;
			}
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"schedule"_q },
				{ u"every_minutes"_q, 30 },
				{ u"times"_q, QJsonArray{ u"08:00"_q } },
			};
			if (Errors(ParseTaskInput(input, Scope::Account(), refs), u"both"_q)) {
				return u"every_minutes with times must fail"_q;
			}
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"chats"_q, QJsonArray{ 111 } },
			};
			input[u"allowed_actions"_q] = QJsonArray{ u"nope"_q };
			if (Errors(ParseTaskInput(input, Scope::Account(), refs), u"Unknown action"_q)) {
				return u"unknown action must fail"_q;
			}
			return QString();
		} },
		{ u"task_parse_chat_scope"_q, [] {
			const auto scope = Scope::Chat(111);
			const auto refs = SampleRefs();
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"chats"_q, QJsonArray{ 111 } },
			};
			if (Errors(ParseTaskInput(input, scope, refs), u"account scope"_q)) {
				return u"chats must be rejected in the chat scope"_q;
			}
			input[u"allowed_chats"_q] = QJsonObject{ { u"trigger_chat"_q, true } };
			if (Errors(ParseTaskInput(input, scope, refs), u"account scope"_q)) {
				return u"allowed_chats must be rejected in the chat scope"_q;
			}
			input.remove(u"allowed_chats"_q);
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"keywords"_q, QJsonArray{ u"rain"_q } },
			};
			const auto parsed = ParseTaskInput(input, scope, refs);
			if (!parsed.task) {
				return u"chat scope parse failed: "_q + parsed.error;
			}
			const auto expected = std::vector<PeerKey>{ 111 };
			if (parsed.task->trigger.filter.chats.peers != expected
				|| parsed.task->frame.chats.peers != expected) {
				return u"the chat scope must set both chat sets"_q;
			}
			return QString();
		} },
		{ u"task_parse_default_frame"_q, [] {
			const auto refs = SampleRefs();
			const auto parsed = ParseTaskInput(
				MessageTaskInput(),
				Scope::Account(),
				refs);
			if (!parsed.task) {
				return u"parse failed: "_q + parsed.error;
			}
			const auto &chats = parsed.task->frame.chats;
			if (!chats.triggerChat
				|| !chats.peers.empty()
				|| !chats.folders.empty()
				|| chats.kinds) {
				return u"message frame must default to the trigger chat"_q;
			}
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"schedule"_q },
				{ u"every_minutes"_q, 30 },
			};
			const auto scheduled = ParseTaskInput(input, Scope::Account(), refs);
			if (!scheduled.task) {
				return u"schedule parse failed: "_q + scheduled.error;
			}
			if (!scheduled.task->frame.chats.empty()) {
				return u"schedule frame must default to empty"_q;
			}
			return QString();
		} },
	};
}

std::vector<Check> UpdateAndJsonChecks() {
	return {
		{ u"task_update_keeps_untouched"_q, [] {
			const auto base = SampleTask();
			const auto input = QJsonObject{
				{ u"task"_q, base.id },
				{ u"title"_q, u"New title"_q },
				{ u"limits"_q, QJsonObject{ { u"daily_cost"_q, 0.75 } } },
			};
			const auto parsed = ParseTaskUpdate(base, input, SampleRefs());
			if (!parsed.task) {
				return u"update failed: "_q + parsed.error;
			}
			const auto &task = *parsed.task;
			if (task.title != u"New title"_q
				|| task.instruction != base.instruction
				|| !(task.trigger == base.trigger)
				|| !(task.frame == base.frame)
				|| task.notify != base.notify
				|| task.catchUp != base.catchUp
				|| task.state != base.state
				|| task.id != base.id
				|| task.limits.dailyCost != 0.75
				|| task.limits.dailyTokens != base.limits.dailyTokens
				|| task.limits.actionsPerHour
					!= base.limits.actionsPerHour) {
				return u"untouched fields must stay"_q;
			}
			auto triggerInput = QJsonObject{
				{ u"task"_q, base.id },
				{ u"trigger"_q, QJsonObject{
					{ u"type"_q, u"schedule"_q },
					{ u"every_minutes"_q, 90 },
				} },
			};
			const auto trigger = ParseTaskUpdate(base, triggerInput, SampleRefs());
			if (!trigger.task
				|| trigger.task->trigger.kind != TriggerKind::Schedule
				|| trigger.task->trigger.schedule.everyMinutes != 90
				|| !(trigger.task->frame == base.frame)) {
				return u"trigger must be replaced whole"_q;
			}
			auto missing = QJsonObject{ { u"title"_q, u"x"_q } };
			if (ParseTaskUpdate(base, missing, SampleRefs()).error.isEmpty()) {
				return u"update without an id must fail"_q;
			}
			return QString();
		} },
		{ u"task_to_json_roundtrip"_q, [] {
			const auto task = SampleTask();
			const auto refs = SampleRefs();
			auto parsed = ParseTaskInput(TaskToJson(task, Zone()), task.scope, refs);
			if (!parsed.task) {
				return u"round trip failed: "_q + parsed.error;
			}
			const auto &restored = *parsed.task;
			if (!(restored.trigger == task.trigger)
				|| !(restored.frame == task.frame)
				|| !(restored.limits == task.limits)
				|| restored.notify != task.notify
				|| restored.catchUp != task.catchUp) {
				return u"account round trip differs: trigger %1 frame %2 limits %3"_q
					.arg(QString::number(restored.trigger == task.trigger),
						QString::number(restored.frame == task.frame),
						QString::number(restored.limits == task.limits))
					+ u" notify %1 catch %2"_q.arg(
						QString::number(restored.notify == task.notify),
						QString::number(restored.catchUp == task.catchUp));
			}
			const auto scope = Scope::Chat(111);
			auto input = MessageTaskInput();
			input[u"trigger"_q] = QJsonObject{
				{ u"type"_q, u"message"_q },
				{ u"keywords"_q, QJsonArray{ u"rain"_q } },
			};
			const auto chatTask = ParseTaskInput(input, scope, refs);
			if (!chatTask.task) {
				return u"chat task failed: "_q + chatTask.error;
			}
			parsed = ParseTaskInput(TaskToJson(*chatTask.task, Zone()), scope, refs);
			if (!parsed.task) {
				return u"chat round trip failed: "_q + parsed.error;
			}
			if (!(parsed.task->trigger == chatTask.task->trigger)
				|| !(parsed.task->frame == chatTask.task->frame)
				|| !(parsed.task->limits == chatTask.task->limits)
				|| parsed.task->notify != chatTask.task->notify
				|| parsed.task->catchUp != chatTask.task->catchUp) {
				return u"chat round trip differs"_q;
			}
			auto limited = SampleTask();
			limited.state = TaskState::HourlyLimit;
			limited.pausedUntil = At(2026, 7, 21, 10, 0).toSecsSinceEpoch();
			const auto json = TaskToJson(limited, Zone());
			if (json.value(u"state"_q) != u"hourly_limit"_q
				|| json.value(u"paused_until"_q) != u"2026-07-21 10:00"_q) {
				return u"paused state must be reported"_q;
			}
			return QString();
		} },
	};
}

std::vector<Check> LimitChecks() {
	return {
		{ u"task_limits_cost_and_tokens"_q, [] {
			const auto limits = TaskLimits{ 0.25, 1'000, 30 };
			auto taskToday = SpendingTotals();
			taskToday.cost = 0.3;
			taskToday.hasCost = true;
			if (CheckSpendingLimits(limits, taskToday, 0., 0, {}) != LimitHit::Task) {
				return u"cost over the task limit must hit the task"_q;
			}
			taskToday.cost = 0.1;
			taskToday.inputTokens = 900;
			taskToday.outputTokens = 5000;
			if (CheckSpendingLimits(limits, taskToday, 0., 0, {}) != LimitHit::None) {
				return u"with a cost the token limit must not apply"_q;
			}
			taskToday.hasCost = false;
			if (CheckSpendingLimits(limits, taskToday, 0., 0, {}) != LimitHit::Task) {
				return u"tokens over the limit must hit the task"_q;
			}
			auto clear = SpendingTotals();
			clear.cost = 2.;
			clear.hasCost = true;
			auto cheap = SpendingTotals();
			if (CheckSpendingLimits(limits, {}, 2., 0, clear)
				!= LimitHit::Background) {
				return u"cost over the background limit must hit the background"_q;
			}
			if (CheckSpendingLimits(limits, {}, 0., 0, cheap)
				!= LimitHit::None) {
				return u"under the limits must not hit"_q;
			}
			return QString();
		} },
		{ u"task_count_actions_and_hourly_resume"_q, [] {
			const auto now = qint64(1'700'000'000);
			const auto times = std::vector<qint64>{
				now - 7200,
				now - 1800,
				now - 900,
				now,
			};
			if (CountRecentActions(times, now) != 3) {
				return u"only the last hour actions must count"_q;
			}
			if (CountRecentActions({ now - 3600, now }, now) != 1) {
				return u"the hour window must be open at the start"_q;
			}
			if (HourlyResumeTime(times, 0, now) != now
				|| HourlyResumeTime(times, 4, now) != now) {
				return u"under the limit the resume time must be now"_q;
			}
			if (HourlyResumeTime(times, 2, now) != now + 2700) {
				return u"the resume time must follow the counted actions"_q;
			}
			if (HourlyResumeTime(times, 3, now) != now + 1800) {
				return u"the oldest action must give the earliest resume"_q;
			}
			return QString();
		} },
	};
}

} // namespace

std::vector<Check> TaskChecks() {
	auto result = std::vector<Check>();
	const auto append = [&](std::vector<Check> &&checks) {
		for (auto &check : checks) {
			result.push_back(std::move(check));
		}
	};
	append(FilterChecks());
	append(SerializeChecks());
	append(ParseChecks());
	append(UpdateAndJsonChecks());
	append(LimitChecks());
	return result;
}

} // namespace Agent::Tests