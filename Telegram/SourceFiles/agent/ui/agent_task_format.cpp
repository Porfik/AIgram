/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_task_format.h"

#include "agent/agent_manager.h"
#include "agent/agent_task_runner.h"
#include "agent/core/agent_providers.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_spending_format.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"


namespace Agent::Ui {
namespace {

constexpr auto kWeekdayBits = 0x1F; // Monday to Friday.
constexpr auto kWeekendBits = 0x60; // Saturday and Sunday.

[[nodiscard]] QString PeerTitle(not_null<Main::Session*> session, PeerKey key) {
	if (!key) {
		return QString();
	}
	const auto peer = session->data().peerLoaded(PeerId(key));
	if (!peer) {
		return u"id %1"_q.arg(key);
	}
	return peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: peer->name();
}

[[nodiscard]] QStringList PeerTitles(
		not_null<Main::Session*> session,
		const std::vector<PeerKey> &peers) {
	auto result = QStringList();
	for (const auto key : peers) {
		result.append(PeerTitle(session, key));
	}
	return result;
}

[[nodiscard]] QStringList KindNames(uchar kinds) {
	auto result = QStringList();
	if (kinds & kChatKindPrivate) {
		result.append(tr::lng_agent_task_kind_private(tr::now));
	}
	if (kinds & kChatKindGroup) {
		result.append(tr::lng_agent_task_kind_groups(tr::now));
	}
	if (kinds & kChatKindChannel) {
		result.append(tr::lng_agent_task_kind_channels(tr::now));
	}
	if (kinds & kChatKindBot) {
		result.append(tr::lng_agent_task_kind_bots(tr::now));
	}
	return result;
}

[[nodiscard]] QString FolderTitle(
		not_null<Main::Session*> session,
		int folderId) {
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (filter.id() == folderId) {
			return tr::lng_agent_task_in_folder(
				tr::now,
				lt_folder,
				filter.titleText().text);
		}
	}
	return tr::lng_agent_task_in_folder(
		tr::now,
		lt_folder,
		u"id %1"_q.arg(folderId));
}

[[nodiscard]] QStringList FolderTitles(
		not_null<Main::Session*> session,
		const std::vector<int> &folders) {
	auto result = QStringList();
	for (const auto id : folders) {
		result.append(FolderTitle(session, id));
	}
	return result;
}

[[nodiscard]] QString FormatChatSet(
		not_null<Main::Session*> session,
		const ChatSet &set) {
	auto parts = PeerTitles(session, set.peers)
		+ KindNames(set.kinds)
		+ FolderTitles(session, set.folders);
	if (set.triggerChat) {
		parts.append(tr::lng_agent_task_trigger_chat(tr::now));
	}
	return parts.join(u", "_q);
}

[[nodiscard]] QString TimeText(int minutes) {
	return QTime(minutes / 60, minutes % 60).toString(u"HH:mm"_q);
}

[[nodiscard]] QString WeekdayName(int index) {
	switch (index) {
	case 0: return tr::lng_weekday1(tr::now);
	case 1: return tr::lng_weekday2(tr::now);
	case 2: return tr::lng_weekday3(tr::now);
	case 3: return tr::lng_weekday4(tr::now);
	case 4: return tr::lng_weekday5(tr::now);
	case 5: return tr::lng_weekday6(tr::now);
	case 6: return tr::lng_weekday7(tr::now);
	}
	return QString();
}

[[nodiscard]] QString DayNames(uchar days) {
	if (days == kWeekdayBits) {
		return tr::lng_agent_task_weekdays(tr::now);
	} else if (days == kWeekendBits) {
		return tr::lng_agent_task_weekends(tr::now);
	}
	auto result = QStringList();
	for (auto i = 0; i != 7; ++i) {
		if (days & (1 << i)) {
			result.append(WeekdayName(i));
		}
	}
	return result.join(u", "_q);
}

[[nodiscard]] QStringList MediaNames(ushort media) {
	auto result = QStringList();
	const auto append = [&](ushort bit, auto key) {
		if (media & bit) {
			result.append(key(tr::now));
		}
	};
	append(kTaskMediaText, tr::lng_agent_task_media_text);
	append(kTaskMediaPhoto, tr::lng_agent_task_media_photo);
	append(kTaskMediaVideo, tr::lng_agent_task_media_video);
	append(kTaskMediaVoice, tr::lng_agent_task_media_voice);
	append(kTaskMediaRound, tr::lng_agent_task_media_round);
	append(kTaskMediaAudio, tr::lng_agent_task_media_audio);
	append(kTaskMediaDocument, tr::lng_agent_task_media_document);
	append(kTaskMediaSticker, tr::lng_agent_task_media_sticker);
	append(kTaskMediaGif, tr::lng_agent_task_media_gif);
	return result;
}

[[nodiscard]] QString FormatConditions(
		not_null<Main::Session*> session,
		const MessageFilter &filter) {
	auto parts = QStringList();
	if (!filter.senders.empty()) {
		parts.append(tr::lng_agent_task_from(
			tr::now,
			lt_users,
			PeerTitles(session, filter.senders).join(u", "_q)));
	}
	if (!filter.keywords.isEmpty()) {
		auto quoted = QStringList();
		for (const auto &word : filter.keywords) {
			quoted.append(u'"' + word + u'"');
		}
		parts.append(tr::lng_agent_task_keywords(
			tr::now,
			lt_text,
			quoted.join(u", "_q)));
	}
	if (!filter.regex.isEmpty()) {
		parts.append(tr::lng_agent_task_regex(tr::now, lt_text, filter.regex));
	}
	const auto media = MediaNames(filter.media);
	if (!media.isEmpty()) {
		parts.append(tr::lng_agent_task_media(
			tr::now,
			lt_media,
			media.join(u", "_q)));
	}
	if (filter.mentionsOnly) {
		parts.append(tr::lng_agent_task_mentions(tr::now));
	}
	if (!filter.window.always()) {
		auto window = tr::lng_agent_task_window(
			tr::now,
			lt_from,
			TimeText(filter.window.from),
			lt_till,
			TimeText(filter.window.till));
		if (filter.window.days) {
			window += u" "_q + tr::lng_agent_task_on_days(
				tr::now,
				lt_days,
				DayNames(filter.window.days));
		}
		parts.append(window);
	}
	return parts.join(u", "_q);
}

} // namespace

QString FormatTaskTrigger(
		not_null<Main::Session*> session,
		const Task &task) {
	if (task.trigger.kind == TriggerKind::Schedule) {
		return FormatTaskSchedule(task.trigger.schedule);
	}
	const auto chats = FormatChatSet(session, task.trigger.filter.chats);
	auto text = tr::lng_agent_task_message_in(tr::now, lt_chats, chats);
	const auto conditions = FormatConditions(session, task.trigger.filter);
	if (conditions.isEmpty()) {
		return text;
	}
	return text + u", "_q + conditions;
}

QString FormatTaskSchedule(const Schedule &schedule) {
	switch (schedule.kind) {
	case ScheduleKind::Interval:
		if (!(schedule.everyMinutes % 60)) {
			return tr::lng_agent_task_every_hours(
				tr::now,
				lt_count,
				schedule.everyMinutes / 60);
		}
		return tr::lng_agent_task_every_minutes(
			tr::now,
			lt_count,
			schedule.everyMinutes);
	case ScheduleKind::Times: {
		auto times = QStringList();
		for (const auto minutes : schedule.times) {
			times.append(TimeText(minutes));
		}
		if (!schedule.days) {
			return tr::lng_agent_task_times(
				tr::now,
				lt_time,
				times.join(u", "_q));
		}
		return tr::lng_agent_task_times_days(
			tr::now,
			lt_time,
			times.join(u", "_q),
			lt_days,
			DayNames(schedule.days));
	}
	case ScheduleKind::Once:
		break;
	}
	const auto local = QDateTime::fromSecsSinceEpoch(schedule.at);
	return tr::lng_agent_task_once(
		tr::now,
		lt_date,
		langDayOfMonthFull(local.date()),
		lt_time,
		local.time().toString(u"HH:mm"_q));
}

QString FormatTaskFrame(
		not_null<Main::Session*> session,
		const Task &task) {
	if (task.frame.tools.empty()) {
		return tr::lng_agent_task_frame_none(tr::now);
	}
	auto tools = QStringList();
	for (const auto &tool : task.frame.tools) {
		tools.append(FormatTaskTool(tool));
	}
	const auto chats = FormatChatSet(session, task.frame.chats);
	return tr::lng_agent_task_frame_actions(
		tr::now,
		lt_action,
		tools.join(u", "_q),
		lt_chats,
		chats.isEmpty() ? tr::lng_agent_task_frame_nowhere(tr::now) : chats);
}

QString FormatTaskLimits(const TaskLimits &limits, bool costKnown) {
	if (costKnown) {
		return tr::lng_agent_task_limits_cost(
			tr::now,
			lt_count,
			limits.actionsPerHour,
			lt_cost,
			FormatCost(limits.dailyCost));
	}
	return tr::lng_agent_task_limits_tokens(
		tr::now,
		lt_count,
		limits.actionsPerHour,
		lt_amount,
		Lang::FormatCountDecimal(limits.dailyTokens));
}

QString FormatTaskNotify(const Task &task) {
	return (task.notify == TaskNotify::Actions)
		? tr::lng_agent_task_notify_actions(tr::now)
		: tr::lng_agent_task_notify_log(tr::now);
}

QString FormatTaskModel(not_null<Manager*> manager, const Task &task) {
	const auto &providers = manager->providers();
	if (const auto provider = FindProvider(providers, task.providerId)) {
		return ModelTitle(*provider);
	}
	const auto background = BackgroundProvider(providers, QString());
	return tr::lng_agent_task_model_default(
		tr::now,
		lt_model,
		background ? ModelTitle(*background) : QString());
}

QString FormatTaskState(not_null<Manager*> manager, const Task &task) {
	if (manager->runner().running(task.id)) {
		return tr::lng_agent_task_state_running(tr::now);
	}
	for (const auto &run : manager->runner().waiting(task.scope)) {
		if (run.taskId == task.id) {
			return tr::lng_agent_task_state_waiting(tr::now);
		}
	}
	switch (task.state) {
	case TaskState::Active:
		return tr::lng_agent_task_state_active(tr::now);
	case TaskState::Paused:
		return tr::lng_agent_task_state_paused(tr::now);
	case TaskState::DailyLimit:
		return tr::lng_agent_task_state_daily(tr::now);
	case TaskState::HourlyLimit:
		return tr::lng_agent_task_state_hourly(
			tr::now,
			lt_time,
			QDateTime::fromSecsSinceEpoch(task.pausedUntil)
				.time().toString(u"HH:mm"_q));
	case TaskState::Finished:
		return tr::lng_agent_task_state_finished(tr::now);
	}
	return QString();
}

bool TaskCostKnown(not_null<Manager*> manager, const Task &task) {
	const auto provider = manager->taskProvider(task);
	if (!provider) {
		return false;
	}
	return IsOpenRouter(*provider) || provider->priceInput > 0.;
}

QString FormatTaskRunTrigger(
		not_null<Main::Session*> session,
		const TaskRun &run) {
	if (run.kind == TriggerKind::Schedule) {
		return (run.missed > 0)
			? tr::lng_agent_run_missed_schedule(tr::now)
			: tr::lng_agent_run_schedule(tr::now);
	} else if (run.missed > 0) {
		return tr::lng_agent_run_missed(tr::now, lt_count, run.missed);
	} else if (run.sender) {
		return tr::lng_agent_run_message(
			tr::now,
			lt_user,
			PeerTitle(session, run.sender),
			lt_chat,
			PeerTitle(session, run.chat));
	}
	return tr::lng_agent_run_message_chat(
		tr::now,
		lt_chat,
		PeerTitle(session, run.chat));
}

QString FormatTaskRunOutcome(const TaskRun &run) {
	switch (run.outcome) {
	case RunOutcome::Running:
		return tr::lng_agent_run_running(tr::now);
	case RunOutcome::Waiting:
		return tr::lng_agent_run_waiting(tr::now);
	case RunOutcome::Done:
		return (run.actions > 0)
			? tr::lng_agent_run_actions(tr::now, lt_count, run.actions)
			: tr::lng_agent_run_nothing(tr::now);
	case RunOutcome::Failed:
		return tr::lng_agent_run_failed(tr::now, lt_error, run.error);
	case RunOutcome::Interrupted:
		return tr::lng_agent_run_interrupted(tr::now);
	}
	return QString();
}

QString FormatTaskTool(const QString &tool) {
	if (tool == u"send_message"_q) {
		return tr::lng_agent_task_tool_send(tr::now);
	} else if (tool == u"forward_messages"_q) {
		return tr::lng_agent_task_tool_forward(tr::now);
	} else if (tool == u"react"_q) {
		return tr::lng_agent_task_tool_react(tr::now);
	} else if (tool == u"edit_message"_q) {
		return tr::lng_agent_task_tool_edit(tr::now);
	} else if (tool == u"delete_messages"_q) {
		return tr::lng_agent_task_tool_delete(tr::now);
	} else if (tool == u"pin_message"_q) {
		return tr::lng_agent_task_tool_pin(tr::now);
	} else if (tool == u"mark_read"_q) {
		return tr::lng_agent_task_tool_mark_read(tr::now);
	} else if (tool == u"set_mute"_q) {
		return tr::lng_agent_task_tool_mute(tr::now);
	} else if (tool == u"set_archived"_q) {
		return tr::lng_agent_task_tool_archive(tr::now);
	} else if (tool == u"set_pinned"_q) {
		return tr::lng_agent_task_tool_pin_chat(tr::now);
	} else if (tool == u"create_folder"_q
		|| tool == u"rename_folder"_q
		|| tool == u"set_folder_chats"_q
		|| tool == u"reorder_folders"_q
		|| tool == u"delete_folder"_q
		|| tool == u"list_folders"_q) {
		return tr::lng_agent_task_tool_folders(tr::now);
	} else if (tool == u"save_files"_q) {
		return tr::lng_agent_task_tool_save_files(tr::now);
	} else if (tool == u"send_files"_q) {
		return tr::lng_agent_task_tool_send_files(tr::now);
	} else if (tool == u"add_members"_q) {
		return tr::lng_agent_task_tool_add_members(tr::now);
	} else if (tool == u"remove_members"_q) {
		return tr::lng_agent_task_tool_remove_members(tr::now);
	} else if (tool == u"unban_members"_q) {
		return tr::lng_agent_task_tool_unban_members(tr::now);
	} else if (tool == u"restrict_members"_q) {
		return tr::lng_agent_task_tool_restrict_members(tr::now);
	} else if (tool == u"promote_admins"_q) {
		return tr::lng_agent_task_tool_promote_admins(tr::now);
	} else if (tool == u"demote_admins"_q) {
		return tr::lng_agent_task_tool_demote_admins(tr::now);
	} else if (tool == u"delete_chat_messages"_q) {
		return tr::lng_agent_task_tool_delete_chat_messages(tr::now);
	} else if (tool == u"delete_member_history"_q) {
		return tr::lng_agent_task_tool_delete_member_history(tr::now);
	} else if (tool == u"answer_join_requests"_q) {
		return tr::lng_agent_task_tool_answer_join_requests(tr::now);
	} else if (tool == u"edit_chat_info"_q) {
		return tr::lng_agent_task_tool_edit_chat_info(tr::now);
	} else if (tool == u"set_chat_photo"_q) {
		return tr::lng_agent_task_tool_set_chat_photo(tr::now);
	} else if (tool == u"set_chat_permissions"_q) {
		return tr::lng_agent_task_tool_set_chat_permissions(tr::now);
	} else if (tool == u"create_chat"_q) {
		return tr::lng_agent_task_tool_create_chat(tr::now);
	} else if (tool == u"join_chat"_q) {
		return tr::lng_agent_task_tool_join_chat(tr::now);
	} else if (tool == u"leave_chat"_q) {
		return tr::lng_agent_task_tool_leave_chat(tr::now);
	} else if (tool == u"add_contact"_q) {
		return tr::lng_agent_task_tool_add_contact(tr::now);
	} else if (tool == u"rename_contact"_q) {
		return tr::lng_agent_task_tool_rename_contact(tr::now);
	} else if (tool == u"delete_contacts"_q) {
		return tr::lng_agent_task_tool_delete_contacts(tr::now);
	} else if (tool == u"set_blocked"_q) {
		return tr::lng_agent_task_tool_set_blocked(tr::now);
	} else if (tool == u"update_profile"_q) {
		return tr::lng_agent_task_tool_update_profile(tr::now);
	} else if (tool == u"set_profile_photo"_q) {
		return tr::lng_agent_task_tool_set_profile_photo(tr::now);
	} else if (tool == u"set_emoji_status"_q) {
		return tr::lng_agent_task_tool_set_emoji_status(tr::now);
	} else if (tool == u"create_invite_link"_q
		|| tool == u"edit_invite_link"_q
		|| tool == u"revoke_invite_links"_q) {
		return tr::lng_agent_task_tool_invite_links(tr::now);
	}
	return tool;
}

} // namespace Agent::Ui