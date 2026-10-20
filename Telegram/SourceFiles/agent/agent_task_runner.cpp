/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_task_runner.h"

#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "agent/agent_notifications.h"
#include "agent/agent_session.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_record.h"
#include "agent/tools/agent_tool_format.h"
#include "agent/tools/agent_tool_table.h"
#include "agent/tools/agent_tools.h"
#include "agent/ui/agent_task_format.h"
#include "base/unixtime.h"
#include "crl/crl_on_main.h"
#include "data/data_chat_filters.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Agent {
namespace {

constexpr auto kMissedSlack = 60;
constexpr auto kMissedDelay = crl::time(5000);
constexpr auto kMaxMissedMessages = 50;
constexpr auto kMaxTimerDelay = crl::time(3600 * 1000);
constexpr auto kExpireCheck = crl::time(60 * 1000);
constexpr auto kPreviewLimit = 100;
constexpr auto kKeepFinishedRuns = 20;

[[nodiscard]] int Today() {
	return int(QDate::currentDate().toJulianDay());
}

[[nodiscard]] qint64 NextMidnight() {
	return QDateTime(
		QDate::currentDate().addDays(1),
		QTime(0, 0)).toSecsSinceEpoch();
}

[[nodiscard]] QString LocalTime(qint64 time) {
	return QDateTime::fromSecsSinceEpoch(time).toString(
		u"yyyy-MM-dd HH:mm"_q);
}

[[nodiscard]] ushort MediaFlag(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return kTaskMediaText;
	} else if (media->photo()) {
		return kTaskMediaPhoto;
	} else if (const auto document = media->document()) {
		if (document->isVoiceMessage()) {
			return kTaskMediaVoice;
		} else if (document->isVideoMessage()) {
			return kTaskMediaRound;
		} else if (document->sticker()) {
			return kTaskMediaSticker;
		} else if (document->isGifv() || document->isAnimation()) {
			return kTaskMediaGif;
		} else if (document->isVideoFile()) {
			return kTaskMediaVideo;
		} else if (document->isAudioFile() || document->isSong()) {
			return kTaskMediaAudio;
		}
		return kTaskMediaDocument;
	}
	return kTaskMediaText;
}

[[nodiscard]] QString PeerTitle(
		not_null<Main::Session*> session,
		PeerKey key) {
	const auto peer = key
		? session->data().peerLoaded(PeerId(key))
		: nullptr;
	return peer ? peer->name() : u"id %1"_q.arg(key);
}

} // namespace

ChatFacts MakeChatFacts(not_null<Main::Session*> session, PeerKey key) {
	auto result = ChatFacts{ .peer = key };
	const auto peer = key
		? session->data().peerLoaded(PeerId(key))
		: nullptr;
	if (!peer) {
		return result;
	}
	if (const auto user = peer->asUser()) {
		result.kind = user->isBot() ? kChatKindBot : kChatKindPrivate;
		result.self = user->isSelf();
	} else if (peer->isBroadcast()) {
		result.kind = kChatKindChannel;
	} else {
		result.kind = kChatKindGroup;
	}
	if (const auto history = session->data().historyLoaded(peer)) {
		for (const auto &filter : session->data().chatsFilters().list()) {
			if (filter.id() && filter.contains(history)) {
				result.folders.push_back(int(filter.id()));
			}
		}
	}
	return result;
}

TaskRunner::TaskRunner(not_null<Manager*> manager)
: _manager(manager)
, _startedAt(base::unixtime::now())
, _scheduleTimer([=] { reschedule(); })
, _expireTimer([=] { checkExpired(); })
, _missedTimer([=] { flushMissed(); })
, _pauseTimer([=] { refreshPauses(); }) {
	session().data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		handleItem(item);
	}, _lifetime);
	_manager->tasks().changes(
	) | rpl::on_next([=] {
		rescheduleLater();
	}, _lifetime);
	rescheduleLater();
}

TaskRunner::~TaskRunner() = default;

Main::Session &TaskRunner::session() const {
	return _manager->session();
}

MessageFacts TaskRunner::messageFacts(not_null<HistoryItem*> item) const {
	const auto from = item->from();
	return {
		.chat = MakeChatFacts(&session(), item->history()->peer->id.value),
		.sender = from ? from->id.value : PeerKey(),
		.outgoing = item->out(),
		.fromAgent = _manager->acting(),
		.text = item->originalText().text,
		.media = MediaFlag(item),
		.mentionsMe = item->mentionsMe(),
		.local = QDateTime::fromSecsSinceEpoch(item->date()),
	};
}

void TaskRunner::handleItem(not_null<HistoryItem*> item) {
	if (!item->isRegular() || item->isService()) {
		return;
	}
	const auto &tasks = _manager->tasks().list();
	const auto any = ranges::any_of(tasks, [](const Task &task) {
		return (task.trigger.kind == TriggerKind::Message)
			&& (task.state == TaskState::Active);
	});
	const auto peer = item->history()->peer->id.value;
	if (!any || _manager->exclusions().excluded(peer)) {
		return;
	}
	const auto facts = messageFacts(item);
	const auto date = qint64(item->date());
	const auto missed = (date < _startedAt - kMissedSlack);
	auto matched = std::vector<Task>();
	for (const auto &task : tasks) {
		if (task.trigger.kind == TriggerKind::Message
			&& task.state == TaskState::Active
			&& date >= task.created
			&& MatchesMessage(task.trigger.filter, facts)) {
			matched.push_back(task);
		}
	}
	if (matched.empty()) {
		return;
	}
	for (const auto &task : matched) {
		if (missed) {
			if (task.catchUp) {
				_missed[task.id].emplace_back(peer, item->id.bare);
				_missedTimer.callOnce(kMissedDelay);
			}
			continue;
		}
		enqueue(task, Trigger{
			.kind = TriggerKind::Message,
			.chat = peer,
			.sender = facts.outgoing ? PeerKey() : facts.sender,
			.messageId = item->id.bare,
			.preview = item->notificationText().text.left(kPreviewLimit),
			.date = date,
		});
	}
	processQueues();
}

void TaskRunner::enqueue(const Task &task, Trigger trigger) {
	auto &queue = _queues[task.id];
	if (trigger.kind == TriggerKind::Message && trigger.missed.empty()) {
		const auto same = [&](const Trigger &other) {
			return (other.kind == TriggerKind::Message)
				&& other.missed.empty()
				&& (other.chat == trigger.chat)
				&& (other.messageId == trigger.messageId);
		};
		if (ranges::any_of(queue, same)) {
			return;
		}
		const auto run = findTaskRun(task.id);
		if (run
			&& run->info.chat == trigger.chat
			&& run->info.messageId == trigger.messageId) {
			return;
		}
	}
	if (int(queue.size()) >= kTaskMaxQueued) {
		dropTrigger(task);
		return;
	}
	queue.push_back(std::move(trigger));
}

void TaskRunner::dropTrigger(const Task &task) {
	LOG(("Agent Info: too many waiting triggers, one was skipped."));
	auto &tasks = _manager->tasks();
	const auto runs = tasks.runs(task.id);
	const auto marker = u"skip-"_q;
	auto info = (!runs.empty() && runs.front().sessionId.startsWith(marker))
		? runs.front()
		: TaskRun{
			.sessionId = marker + GenerateSessionId(),
			.started = base::unixtime::now(),
			.outcome = RunOutcome::Failed,
		};
	++info.missed;
	info.finished = base::unixtime::now();
	info.error = u"Too many runs were waiting: %1 skipped."_q.arg(
		info.missed);
	tasks.saveRun(task.id, info);
}

void TaskRunner::processQueues() {
	auto active = int(_runs.size());
	while (active < kTaskMaxParallelRuns) {
		auto best = QString();
		auto bestDate = std::numeric_limits<qint64>::max();
		for (const auto &[id, queue] : _queues) {
			if (!queue.empty()
				&& !findTaskRun(id)
				&& queue.front().date < bestDate) {
				best = id;
				bestDate = queue.front().date;
			}
		}
		if (best.isEmpty()) {
			return;
		}
		auto &queue = _queues[best];
		auto trigger = std::move(queue.front());
		queue.pop_front();
		const auto found = _manager->tasks().find(best);
		if (!found) {
			_queues.remove(best);
			continue;
		}
		const auto task = *found;
		if (!checkCanRun(task)) {
			continue;
		}
		startRun(task, std::move(trigger));
		++active;
	}
}

bool TaskRunner::checkCanRun(const Task &task) {
	if (task.state != TaskState::Active) {
		_queues.remove(task.id);
		return false;
	} else if (!_manager->taskProvider(task)) {
		LOG(("Agent Warning: no provider for a background task."));
		return false;
	}
	const auto today = Today();
	const auto spent = (task.spendingDay == today)
		? task.spentToday
		: SpendingTotals();
	const auto &providers = _manager->providers();
	const auto hit = CheckSpendingLimits(
		task.limits,
		spent,
		providers.backgroundDailyCost,
		providers.backgroundDailyTokens,
		_manager->tasks().backgroundToday());
	if (hit == LimitHit::Task) {
		pauseTask(
			task.id,
			TaskState::DailyLimit,
			NextMidnight(),
			tr::lng_agent_notify_daily_limit(tr::now));
		return false;
	} else if (hit == LimitHit::Background) {
		if (_backgroundNotifiedDay != today) {
			_backgroundNotifiedDay = today;
			notify(
				task,
				QString(),
				0,
				tr::lng_agent_notify_background_limit(tr::now),
				true);
		}
		return false;
	}
	const auto perHour = task.limits.actionsPerHour;
	const auto now = qint64(base::unixtime::now());
	const auto &times = _actions[task.id];
	if (perHour > 0 && CountRecentActions(times, now) >= perHour) {
		const auto until = HourlyResumeTime(times, perHour, now);
		pauseTask(
			task.id,
			TaskState::HourlyLimit,
			until,
			tr::lng_agent_notify_hourly_limit(
				tr::now,
				lt_time,
				QDateTime::fromSecsSinceEpoch(until).toString(u"HH:mm"_q)));
		return false;
	}
	return true;
}

void TaskRunner::startRun(const Task &task, Trigger trigger) {
	const auto provider = _manager->taskProvider(task);
	Expects(provider != nullptr);

	const auto now = base::unixtime::now();
	auto record = SessionRecord();
	record.info.id = GenerateSessionId();
	record.info.scope = task.scope;
	record.info.title = task.title;
	record.info.titleFinal = true;
	record.info.created = record.info.updated = now;
	record.info.task = task.id;
	record.tainted = (trigger.kind == TriggerKind::Message);
	const auto sessionId = record.info.id;
	const auto taskId = task.id;
	const auto weak = base::make_weak(this);
	auto triggers = std::vector<PeerKey>();
	if (trigger.chat) {
		triggers.push_back(trigger.chat);
	}
	for (const auto &[peer, id] : trigger.missed) {
		if (!ranges::contains(triggers, peer)) {
			triggers.push_back(peer);
		}
	}
	auto setup = RunSetup{
		.task = task,
		.triggerPeers = std::move(triggers),
		.provider = *provider,
		.frame = frameText(task),
		.notify = [=](QString text) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			} else if (const auto found = _manager->tasks().find(taskId)) {
				const auto copy = *found;
				strong->notify(copy, sessionId, 0, text, true);
			}
		},
	};
	const auto hint = triggerHint(task, trigger);
	const auto data = triggerData(trigger);
	const auto session = _manager->startRun(
		std::move(record),
		std::move(setup));
	_runs.push_back(std::make_unique<Run>(Run{
		.taskId = task.id,
		.session = session,
		.info = TaskRun{
			.sessionId = sessionId,
			.kind = trigger.kind,
			.chat = trigger.chat,
			.sender = trigger.sender,
			.messageId = trigger.messageId,
			.preview = trigger.preview,
			.missed = trigger.missed.empty()
				? trigger.missedSchedules
				: int(trigger.missed.size()),
			.started = now,
			.outcome = RunOutcome::Running,
		},
	}));
	const auto raw = _runs.back().get();
	saveRunInfo(raw);
	session->stateChanges(
	) | rpl::on_next([=] {
		handleState(raw);
	}, raw->lifetime);
	session->live(
	) | rpl::on_next([=](const EngineEvent &event) {
		handleEvent(raw, event);
	}, raw->lifetime);
	_runningChanges.fire_copy(taskId);
	if (!weak || !findRun(sessionId)) {
		return;
	}
	session->send(SendRequest{
		.hint = hint,
		.untrusted = data,
	});
}

QString TaskRunner::triggerHint(
		const Task &task,
		const Trigger &trigger) const {
	if (trigger.kind == TriggerKind::Schedule) {
		return trigger.missedSchedules
			? u"Task run for a scheduled time missed while the app was"
				" closed (%1). Local time now: %2."_q.arg(
					LocalTime(trigger.date),
					LocalTime(base::unixtime::now()))
			: u"Task run on schedule. Local time now: %1."_q.arg(
				LocalTime(base::unixtime::now()));
	} else if (!trigger.missed.empty()) {
		return u"Task run for %1 messages that matched the task while"
			" the app was closed. They are below as untrusted data."_q.arg(
				trigger.missed.size());
	}
	return u"Task run: a new message in chat %1 matched the task. It is"
		" below as untrusted data. Local time now: %2."_q.arg(
			QString::number(trigger.chat),
			LocalTime(base::unixtime::now()));
}

QString TaskRunner::triggerData(const Trigger &trigger) const {
	const auto find = [&](PeerKey peer, int64 id) -> HistoryItem* {
		return session().data().message(PeerId(peer), MsgId(id));
	};
	const auto compact = [](const QJsonDocument &document) {
		return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
	};
	if (trigger.kind == TriggerKind::Schedule) {
		return QString();
	} else if (!trigger.missed.empty()) {
		auto list = QJsonArray();
		const auto &missed = trigger.missed;
		const auto from = std::max(
			int(missed.size()) - kMaxMissedMessages,
			0);
		for (auto i = from; i != int(missed.size()); ++i) {
			if (const auto item = find(missed[i].first, missed[i].second)) {
				list.push_back(ToolFormat::SerializeMessageWithChat(item));
			}
		}
		return list.isEmpty() ? QString() : compact(QJsonDocument(list));
	} else if (const auto item = find(trigger.chat, trigger.messageId)) {
		return compact(QJsonDocument(
			ToolFormat::SerializeMessageWithChat(item)));
	}
	return QString();
}

QString TaskRunner::frameText(const Task &task) const {
	const auto &frame = task.frame;
	auto chats = QStringList();
	if (task.scope.kind == ScopeKind::Chat) {
		chats.push_back(u"this chat"_q);
	} else {
		if (frame.chats.triggerChat) {
			chats.push_back(u"the chat of the trigger message"_q);
		}
		for (const auto peer : frame.chats.peers) {
			chats.push_back(u"\"%1\" (id %2)"_q.arg(
				PeerTitle(&session(), peer),
				QString::number(peer)));
		}
		const auto kinds = std::array{
			std::pair{ kChatKindPrivate, u"private chats"_q },
			std::pair{ kChatKindGroup, u"groups"_q },
			std::pair{ kChatKindChannel, u"channels"_q },
			std::pair{ kChatKindBot, u"bots"_q },
		};
		for (const auto &[flag, name] : kinds) {
			if (frame.chats.kinds & flag) {
				chats.push_back(name);
			}
		}
		for (const auto folder : frame.chats.folders) {
			for (const auto &filter
					: session().data().chatsFilters().list()) {
				if (int(filter.id()) == folder) {
					chats.push_back(u"folder \"%1\""_q.arg(
						filter.titleText().text));
				}
			}
		}
	}
	const auto where = chats.isEmpty()
		? u"no chats"_q
		: chats.join(u", "_q);
	auto tools = QStringList();
	for (const auto &tool : frame.tools) {
		tools.push_back(tool);
	}
	return (tools.isEmpty()
		? u"No action is allowed without asking."_q
		: u"Without asking you may use: %1."_q.arg(tools.join(u", "_q)))
		+ u" Reading and these actions work only in: %1. Any other"
			" action waits for the user's decision."_q.arg(where);
}

void TaskRunner::handleState(not_null<Run*> run) {
	if (run->finished) {
		return;
	}
	const auto &state = run->session->state();
	const auto waiting = (state.pendingCards > 0);
	const auto sessionId = run->info.sessionId;
	if (waiting && !run->waitingSince) {
		run->waitingSince = base::unixtime::now();
		run->info.outcome = RunOutcome::Waiting;
		saveRunInfo(run);
		if (!_expireTimer.isActive()) {
			_expireTimer.callEach(kExpireCheck);
		}
		auto action = QString();
		for (const auto &card : run->session->record().cards) {
			if (card.status == CardStatus::Pending) {
				const auto i = run->calls.find(card.toolUseId);
				if (i != run->calls.end()) {
					action = Ui::FormatTaskTool(i->second.name);
				}
			}
		}
		const auto weak = base::make_weak(this);
		_waitingChanges.fire({});
		if (!weak || !findRun(sessionId)) {
			return;
		}
		if (const auto task = _manager->tasks().find(run->taskId)) {
			const auto copy = *task;
			notify(
				copy,
				sessionId,
				run->info.chat,
				tr::lng_agent_notify_waiting(tr::now, lt_action, action),
				true);
		}
	} else if (!waiting && run->waitingSince) {
		run->waitingSince = 0;
		run->info.outcome = RunOutcome::Running;
		saveRunInfo(run);
		const auto weak = base::make_weak(this);
		_waitingChanges.fire({});
		if (!weak || !findRun(sessionId)) {
			return;
		}
	}
	const auto ended = state.lastEnd.has_value()
		&& (state.status == SessionStatus::Idle
			|| state.status == SessionStatus::Failed);
	if (ended) {
		crl::on_main(this, [=] {
			const auto found = findRun(sessionId);
			if (!found || found->finished) {
				return;
			}
			const auto &now = found->session->state();
			if (now.lastEnd
				&& (now.status == SessionStatus::Idle
					|| now.status == SessionStatus::Failed)) {
				finishRun(found);
			}
		});
	}
}

void TaskRunner::handleEvent(
		not_null<Run*> run,
		const EngineEvent &event) {
	if (const auto requested = std::get_if<ToolCallRequested>(&event)) {
		run->calls[requested->call.id] = requested->call;
	} else if (const auto finished = std::get_if<ToolCallFinished>(
			&event)) {
		const auto i = run->calls.find(finished->result.toolUseId);
		if (i != run->calls.end() && !finished->result.error) {
			const auto call = i->second;
			countAction(run, call);
		}
	}
}

void TaskRunner::countAction(
		not_null<Run*> run,
		const ToolUseBlock &call) {
	const auto policy = ToolPolicyByName(call.name);
	if (!policy
		|| policy->risk == Risk::Read
		|| policy->risk == Risk::Forbidden) {
		return;
	}
	const auto now = qint64(base::unixtime::now());
	++run->info.actions;
	auto &times = _actions[run->taskId];
	times.push_back(now);
	times.erase(ranges::remove_if(times, [&](qint64 time) {
		return time <= now - 3600;
	}), end(times));
	const auto found = _manager->tasks().find(run->taskId);
	if (!found) {
		return;
	}
	const auto task = *found;
	const auto sessionId = run->info.sessionId;
	auto peer = (task.scope.kind == ScopeKind::Chat)
		? task.scope.peer
		: PeerKey();
	if (!peer) {
		const auto chats = call.input.value("chats").toArray();
		const auto ref = call.input.contains("chat")
			? call.input.value("chat")
			: chats.isEmpty()
			? QJsonValue()
			: chats.first();
		if (const auto resolved = ResolveChatRef(&session(), ref)) {
			peer = resolved->id.value;
		}
	}
	if (policy->risk == Risk::Visible
		&& task.notify == TaskNotify::Actions) {
		notify(task, sessionId, peer, actionText(call, peer), false);
	}
	const auto perHour = task.limits.actionsPerHour;
	if (perHour > 0 && CountRecentActions(times, now) >= perHour) {
		const auto until = HourlyResumeTime(times, perHour, now);
		pauseTask(
			task.id,
			TaskState::HourlyLimit,
			until,
			tr::lng_agent_notify_hourly_limit(
				tr::now,
				lt_time,
				QDateTime::fromSecsSinceEpoch(until).toString(u"HH:mm"_q)));
		crl::on_main(this, [=] {
			if (const auto found = findRun(sessionId)) {
				found->session->stop();
			}
		});
	}
}

QString TaskRunner::actionText(
		const ToolUseBlock &call,
		PeerKey peer) const {
	const auto chat = PeerTitle(&session(), peer);
	const auto &name = call.name;
	if (name == u"send_message"_q) {
		return tr::lng_agent_notify_sent(
			tr::now,
			lt_chat,
			chat,
			lt_text,
			call.input.value("text").toString().left(kPreviewLimit));
	} else if (name == u"forward_messages"_q) {
		return tr::lng_agent_notify_forwarded(tr::now, lt_chat, chat);
	} else if (name == u"react"_q) {
		return tr::lng_agent_notify_reacted(
			tr::now,
			lt_emoji,
			call.input.value("emoji").toString(),
			lt_chat,
			chat);
	} else if (name == u"edit_message"_q) {
		return tr::lng_agent_notify_edited(tr::now, lt_chat, chat);
	} else if (name == u"delete_messages"_q) {
		return tr::lng_agent_notify_deleted(tr::now, lt_chat, chat);
	} else if (name == u"pin_message"_q) {
		return tr::lng_agent_notify_pinned(tr::now, lt_chat, chat);
	} else if (name == u"send_files"_q) {
		return tr::lng_agent_notify_files(tr::now, lt_chat, chat);
	}
	return tr::lng_agent_notify_action(
		tr::now,
		lt_action,
		Ui::FormatTaskTool(name),
		lt_chat,
		chat);
}

void TaskRunner::finishRun(not_null<Run*> run) {
	run->finished = true;
	const auto &state = run->session->state();
	const auto &record = run->session->record();
	run->info.finished = base::unixtime::now();
	run->info.usage = record.info.usage;
	if (state.status == SessionStatus::Failed) {
		run->info.outcome = RunOutcome::Failed;
		run->info.error = state.error
			? (state.error->text.isEmpty()
				? ErrorKindName(state.error->kind)
				: state.error->text)
			: QString();
	} else if (state.lastEnd == TurnEnd::Interrupted) {
		run->info.outcome = RunOutcome::Interrupted;
	} else {
		run->info.outcome = RunOutcome::Done;
	}
	saveRunInfo(run);
	const auto taskId = run->taskId;
	const auto info = run->info;
	const auto wasWaiting = (run->waitingSince != 0);
	removeRun(run);
	_finished.push_back(info.sessionId);
	while (int(_finished.size()) > kKeepFinishedRuns) {
		_manager->unload(_finished.front());
		_finished.pop_front();
	}
	const auto weak = base::make_weak(this);
	_runningChanges.fire_copy(taskId);
	if (!weak) {
		return;
	} else if (wasWaiting) {
		_waitingChanges.fire({});
		if (!weak) {
			return;
		}
	}
	if (const auto found = _manager->tasks().find(taskId)) {
		const auto task = *found;
		if (info.outcome == RunOutcome::Failed
			&& task.notify == TaskNotify::Actions) {
			notify(
				task,
				info.sessionId,
				info.chat,
				tr::lng_agent_notify_failed(tr::now, lt_error, info.error),
				false);
		}
		const auto today = Today();
		const auto &providers = _manager->providers();
		const auto hit = CheckSpendingLimits(
			task.limits,
			(task.spendingDay == today) ? task.spentToday : SpendingTotals(),
			providers.backgroundDailyCost,
			providers.backgroundDailyTokens,
			_manager->tasks().backgroundToday());
		if (hit == LimitHit::Task && task.state == TaskState::Active) {
			pauseTask(
				task.id,
				TaskState::DailyLimit,
				NextMidnight(),
				tr::lng_agent_notify_daily_limit(tr::now));
		}
	}
	processQueues();
}

TaskRunner::Run *TaskRunner::findRun(const QString &sessionId) const {
	for (const auto &run : _runs) {
		if (run->info.sessionId == sessionId) {
			return run.get();
		}
	}
	return nullptr;
}

TaskRunner::Run *TaskRunner::findTaskRun(const QString &taskId) const {
	for (const auto &run : _runs) {
		if (run->taskId == taskId) {
			return run.get();
		}
	}
	return nullptr;
}

void TaskRunner::saveRunInfo(not_null<Run*> run) {
	const auto dropped = _manager->tasks().saveRun(run->taskId, run->info);
	for (const auto &id : dropped) {
		if (!id.isEmpty() && !findRun(id)) {
			_manager->remove(id);
		}
	}
}

void TaskRunner::removeRun(not_null<Run*> run) {
	const auto i = ranges::find(_runs, run.get(), [](const auto &item) {
		return item.get();
	});
	if (i != end(_runs)) {
		auto taken = std::move(*i);
		_runs.erase(i);
		crl::on_main([holder = std::move(taken)] {});
	}
}

void TaskRunner::runSaved(not_null<Session*> session) {
	if (const auto run = findRun(session->id())) {
		run->info.usage = session->record().info.usage;
	}
}

std::vector<WaitingRun> TaskRunner::waiting(const Scope &scope) const {
	auto result = std::vector<WaitingRun>();
	for (const auto &run : _runs) {
		const auto cards = run->session->state().pendingCards;
		if (!cards) {
			continue;
		}
		const auto task = _manager->tasks().find(run->taskId);
		if (!task
			|| (scope.kind == ScopeKind::Chat && !(task->scope == scope))) {
			continue;
		}
		result.push_back({
			.taskId = run->taskId,
			.sessionId = run->info.sessionId,
			.cards = cards,
			.since = run->waitingSince,
		});
	}
	ranges::sort(result, ranges::less(), &WaitingRun::since);
	return result;
}

rpl::producer<> TaskRunner::waitingChanges() const {
	return _waitingChanges.events();
}

bool TaskRunner::running(const QString &taskId) const {
	return findTaskRun(taskId) != nullptr;
}

rpl::producer<QString> TaskRunner::runningChanges() const {
	return _runningChanges.events();
}

void TaskRunner::taskRemoved(const QString &taskId) {
	_queues.remove(taskId);
	_missed.remove(taskId);
	_actions.remove(taskId);
	auto stopping = std::vector<std::unique_ptr<Run>>();
	for (auto i = begin(_runs); i != end(_runs);) {
		if ((*i)->taskId == taskId) {
			stopping.push_back(std::move(*i));
			i = _runs.erase(i);
		} else {
			++i;
		}
	}
	for (auto &run : stopping) {
		run->lifetime.destroy();
		run->session->stop();
	}
	if (!stopping.empty()) {
		const auto weak = base::make_weak(this);
		_runningChanges.fire_copy(taskId);
		if (!weak) {
			return;
		}
		_waitingChanges.fire({});
	}
}

void TaskRunner::stopAll() {
	_queues.clear();
	_missed.clear();
	_actions.clear();
	auto stopping = base::take(_runs);
	for (auto &run : stopping) {
		run->lifetime.destroy();
		run->session->stop();
	}
	if (!stopping.empty()) {
		_waitingChanges.fire({});
	}
}

void TaskRunner::rescheduleLater() {
	if (_scheduled || _rescheduling) {
		return;
	}
	_scheduled = true;
	crl::on_main(this, [=] {
		reschedule();
	});
}

void TaskRunner::reschedule() {
	_scheduled = false;
	const auto now = qint64(base::unixtime::now());
	const auto zone = QTimeZone::systemTimeZone();
	auto nearest = qint64(0);
	auto updates = std::vector<Task>();
	for (const auto &task : _manager->tasks().list()) {
		if (task.trigger.kind != TriggerKind::Schedule
			|| task.state != TaskState::Active) {
			continue;
		}
		const auto &schedule = task.trigger.schedule;
		const auto from = task.lastFired ? task.lastFired : task.created;
		const auto due = NextFireTime(schedule, task.created, from, zone);
		if (!due) {
			continue;
		} else if (due > now) {
			nearest = nearest ? std::min(nearest, due) : due;
			continue;
		}
		auto updated = task;
		const auto missed = (due < _startedAt);
		if (missed) {
			updated.lastFired = now;
			if (task.catchUp) {
				enqueue(task, Trigger{
					.kind = TriggerKind::Schedule,
					.date = due,
					.missedSchedules = 1,
				});
			}
		} else {
			updated.lastFired = due;
			enqueue(task, Trigger{
				.kind = TriggerKind::Schedule,
				.date = due,
			});
		}
		if (schedule.kind == ScheduleKind::Once) {
			updated.state = TaskState::Finished;
		}
		updates.push_back(std::move(updated));
	}
	if (!updates.empty()) {
		_rescheduling = true;
		for (const auto &task : updates) {
			_manager->tasks().update(task);
		}
		_rescheduling = false;
		rescheduleLater();
	}
	if (nearest) {
		_scheduleTimer.callOnce(std::min(
			crl::time(nearest - now) * 1000,
			kMaxTimerDelay));
	} else {
		_scheduleTimer.cancel();
	}
	refreshPauses();
	processQueues();
}

void TaskRunner::flushMissed() {
	auto missed = base::take(_missed);
	for (auto &[taskId, list] : missed) {
		const auto task = _manager->tasks().find(taskId);
		if (!task || list.empty()) {
			continue;
		}
		const auto copy = *task;
		auto chat = list.front().first;
		for (const auto &[peer, id] : list) {
			if (peer != chat) {
				chat = 0;
			}
		}
		enqueue(copy, Trigger{
			.kind = TriggerKind::Message,
			.chat = chat,
			.date = _startedAt,
			.missed = std::move(list),
		});
	}
	processQueues();
}

void TaskRunner::checkExpired() {
	const auto now = qint64(base::unixtime::now());
	auto expiring = std::vector<QString>();
	auto waiting = false;
	for (const auto &run : _runs) {
		if (!run->waitingSince) {
			continue;
		}
		waiting = true;
		if (run->waitingSince + kTaskCardExpireSeconds <= now) {
			expiring.push_back(run->info.sessionId);
		}
	}
	if (!waiting) {
		_expireTimer.cancel();
		return;
	}
	for (const auto &id : expiring) {
		if (const auto run = findRun(id)) {
			run->session->expireCards();
		}
	}
}

void TaskRunner::refreshPauses() {
	const auto now = qint64(base::unixtime::now());
	auto nearest = qint64(0);
	auto resumed = std::vector<Task>();
	for (const auto &task : _manager->tasks().list()) {
		if (task.state != TaskState::DailyLimit
			&& task.state != TaskState::HourlyLimit) {
			continue;
		} else if (task.pausedUntil <= now) {
			auto copy = task;
			copy.state = TaskState::Active;
			copy.pausedUntil = 0;
			resumed.push_back(std::move(copy));
		} else {
			nearest = nearest
				? std::min(nearest, task.pausedUntil)
				: task.pausedUntil;
		}
	}
	for (const auto &task : resumed) {
		_manager->tasks().update(task);
	}
	if (nearest) {
		_pauseTimer.callOnce(std::min(
			crl::time(nearest - now) * 1000,
			kMaxTimerDelay));
	} else {
		_pauseTimer.cancel();
	}
}

void TaskRunner::pauseTask(
		const QString &taskId,
		TaskState state,
		qint64 until,
		const QString &text) {
	const auto found = _manager->tasks().find(taskId);
	if (!found) {
		return;
	}
	auto task = *found;
	task.state = state;
	task.pausedUntil = until;
	_queues.remove(taskId);
	_manager->tasks().update(task);
	notify(task, QString(), 0, text, true);
	refreshPauses();
}

void TaskRunner::notify(
		const Task &task,
		const QString &sessionId,
		PeerKey peer,
		const QString &text,
		bool sound) {
	ShowTaskNotification(&session(), {
		.taskId = task.id,
		.sessionId = sessionId,
		.peer = peer
			? peer
			: (task.scope.kind == ScopeKind::Chat)
			? task.scope.peer
			: PeerKey(),
		.title = QString::fromUtf8("\xE2\x9C\xA6 ") + task.title,
		.text = text,
		.sound = sound,
	});
}

} // namespace Agent
