/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "agent/agent_task_runner.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_task.h"

#include "base/algorithm.h"
#include "base/unixtime.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>

#include <optional>

namespace Agent {
namespace {

constexpr auto kTaskNotifyMaxChars = 1000;

[[nodiscard]] const Data::ChatFilters &ChatFilters(
		not_null<Main::Session*> session) {
	return session->data().chatsFilters();
}

[[nodiscard]] bool FolderHidden(
		not_null<Main::Session*> session,
		FilterId id) {
	return session->agent().exclusions().folderExcluded(id);
}

[[nodiscard]] QString FolderTitle(const Data::ChatFilter &filter) {
	return filter.titleText().text;
}

[[nodiscard]] bool IsActionTool(const QString &name) {
	static const auto tools = [] {
		auto result = base::flat_set<QString>();
		const auto collect = [&](std::vector<ToolEntry> entries) {
			for (auto &entry : entries) {
				if ((entry.action || entry.policy.risk == Risk::Visible)
					&& entry.policy.background) {
					result.emplace(entry.name);
				}
			}
		};
		collect(ActionTools::Entries());
		collect(ChatStateTools::Entries());
		collect(FolderTools::Entries());
		collect(FileTools::Entries());
		return result;
	}();
	return tools.contains(name);
}

TaskRefs MakeTaskRefs(not_null<PendingCall*> call) {
	auto result = TaskRefs();
	result.chat = [session = call->session, gate = call->gate](
			const QJsonValue &value) {
		const auto peer = ResolveChatRef(session, value);
		return (peer && gate->visible(peer->id.value))
			? peer->id.value
			: PeerKey(0);
	};
	result.folder = [session = call->session](
			const QJsonValue &value) -> std::optional<int> {
		const auto &list = ChatFilters(session).list();
		const auto byId = [&](int id) -> std::optional<int> {
			if (id <= 0) {
				return std::nullopt;
			}
			const auto i = ranges::find(list, id, &Data::ChatFilter::id);
			if (i == end(list) || FolderHidden(session, id)) {
				return std::nullopt;
			}
			return id;
		};
		if (value.isDouble()) {
			return byId(value.toInt());
		}
		const auto text = value.toString().trimmed();
		if (text.isEmpty()) {
			return std::nullopt;
		}
		auto ok = false;
		const auto number = text.toInt(&ok);
		if (ok) {
			return byId(number);
		}
		for (const auto &filter : list) {
			if (filter.id()
				&& !FolderHidden(session, filter.id())
				&& !FolderTitle(filter).compare(text, Qt::CaseInsensitive)) {
				return filter.id();
			}
		}
		return std::nullopt;
	};
	result.actionTool = IsActionTool;
	result.zone = QTimeZone::systemTimeZone();
	result.now = base::unixtime::now();
	return result;
}

[[nodiscard]] QString JsonTaskId(
		const QJsonObject &object,
		QString &error) {
	const auto id = object.value("task").toString().trimmed();
	if (id.isEmpty()) {
		error = u"task is required: the id of a task from list_tasks."_q;
	}
	return id;
}

[[nodiscard]] std::optional<bool> JsonBool(
		const QJsonObject &object,
		const QString &key,
		QString &error) {
	const auto value = object.value(key);
	if (value.isUndefined() || value.isNull()) {
		return std::nullopt;
	}
	if (!value.isBool()) {
		error = u"%1 must be a boolean."_q.arg(key);
		return std::nullopt;
	}
	return value.toBool();
}

[[nodiscard]] QString UnknownTaskText(const QString &id) {
	return u"Unknown task: %1."_q.arg(id);
}

[[nodiscard]] const Task *FindScopeTask(
		not_null<PendingCall*> call,
		const QString &id) {
	const auto task = call->session->agent().tasks().find(id);
	if (!task) {
		return nullptr;
	}
	const auto &scope = call->gate->scope();
	return ((scope.kind == ScopeKind::Chat) && !(task->scope == scope))
		? nullptr
		: task;
}

void RunCreateTask(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto refs = MakeTaskRefs(call);
	const auto parsed = ParseTaskInput(
		call->input,
		call->gate->scope(),
		refs);
	if (!parsed.task) {
		call->fail(parsed.error);
		return;
	}
	if (int(session->agent().tasks().list().size()) >= kTaskMaxTasks) {
		call->fail(u"The task limit is reached (%1)."_q.arg(kTaskMaxTasks));
		return;
	}
	auto task = *parsed.task;
	task.id = GenerateTaskId();
	task.created = refs.now;
	auto card = Card();
	card.toolUseId = call->toolUseId;
	card.kind = CardKind::CreateTask;
	card.text = task.instruction;
	card.proposedText = task.instruction;
	card.task = SerializeTask(task);
	call->confirm(std::move(card), [=, task = task](Card decided) mutable {
		task.instruction = decided.text;
		session->agent().tasks().add(task);
		auto json = QJsonObject();
		json.insert("task", TaskToJson(task, refs.zone));
		json.insert("created", QJsonValue(true));
		call->done(std::move(json));
	});
}

void RunUpdateTask(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = JsonTaskId(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto task = FindScopeTask(call, id);
	if (!task) {
		call->fail(UnknownTaskText(id));
		return;
	}
	const auto session = call->session;
	const auto base = *task;
	const auto refs = MakeTaskRefs(call);
	const auto parsed = ParseTaskUpdate(base, call->input, refs);
	if (!parsed.task) {
		call->fail(parsed.error);
		return;
	}
	const auto &next = *parsed.task;
	if (next.trigger == base.trigger
		&& next.frame == base.frame
		&& next.title == base.title
		&& next.instruction == base.instruction
		&& next.notify == base.notify
		&& next.catchUp == base.catchUp
		&& next.limits == base.limits) {
		call->fail(u"Nothing to change."_q);
		return;
	}
	auto card = Card();
	card.toolUseId = call->toolUseId;
	card.kind = CardKind::UpdateTask;
	card.text = next.instruction;
	card.proposedText = next.instruction;
	card.task = SerializeTask(next);
	card.previousTask = SerializeTask(base);
	call->confirm(
		std::move(card),
		[=, next = next](Card decided) mutable {
			const auto stored = session->agent().tasks().find(id);
			if (!stored) {
				call->fail(UnknownTaskText(id));
				return;
			}
			auto task = next;
			task.state = stored->state;
			task.pausedUntil = stored->pausedUntil;
			task.lastFired = stored->lastFired;
			task.lastSeen = stored->lastSeen;
			task.spendingDay = stored->spendingDay;
			task.spentToday = stored->spentToday;
			task.instruction = decided.text;
			session->agent().tasks().update(task);
			auto json = QJsonObject();
			json.insert("task", TaskToJson(task, refs.zone));
			json.insert("updated", QJsonValue(true));
			call->done(std::move(json));
		});
}

void AddNamedChat(
		QJsonArray &to,
		std::vector<PeerKey> &seen,
		not_null<Main::Session*> session,
		PeerKey peer) {
	if (!peer || ranges::contains(seen, peer)) {
		return;
	}
	seen.push_back(peer);
	auto item = QJsonObject();
	item.insert("id", QJsonValue(qint64(peer)));
	if (const auto loaded = session->data().peerLoaded(PeerId(peer))) {
		item.insert("title", loaded->name());
	}
	to.push_back(item);
}

void RunListTasks(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto scope = call->gate->scope();
	const auto &runner = session->agent().runner();
	const auto zone = QTimeZone::systemTimeZone();
	auto tasks = QJsonArray();
	for (const auto &task : session->agent().tasks().list(scope)) {
		auto json = TaskToJson(task, zone);
		json.insert("running", QJsonValue(runner.running(task.id)));
		auto chats = QJsonArray();
		auto seen = std::vector<PeerKey>();
		for (const auto peer : task.trigger.filter.chats.peers) {
			AddNamedChat(chats, seen, session, peer);
		}
		for (const auto peer : task.trigger.filter.senders) {
			AddNamedChat(chats, seen, session, peer);
		}
		for (const auto peer : task.frame.chats.peers) {
			AddNamedChat(chats, seen, session, peer);
		}
		json.insert("chats", chats);
		tasks.push_back(json);
	}
	auto result = QJsonObject();
	result.insert("tasks", tasks);
	call->done(std::move(result));
}

void InsertTaskRef(QJsonObject &json, const Task &task) {
	auto item = QJsonObject();
	item.insert("id", task.id);
	item.insert("title", task.title);
	json.insert("task", item);
}

void RunPauseTask(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = JsonTaskId(call->input, error);
	const auto paused = error.isEmpty()
		? JsonBool(call->input, "paused", error)
		: std::nullopt;
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto task = FindScopeTask(call, id);
	if (!task) {
		call->fail(UnknownTaskText(id));
		return;
	}
	const auto session = call->session;
	const auto previous = *task;
	auto updated = previous;
	updated.state = *paused ? TaskState::Paused : TaskState::Active;
	if (!*paused) {
		updated.pausedUntil = 0;
	}
	session->agent().tasks().update(updated);
	call->undoable([=](Fn<void(QString error)> finished) {
		session->agent().tasks().update(previous);
		finished({});
	});
	auto json = QJsonObject();
	InsertTaskRef(json, updated);
	json.insert("state", *paused ? u"paused"_q : u"active"_q);
	call->done(std::move(json));
}

void RunDeleteTask(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = JsonTaskId(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto task = FindScopeTask(call, id);
	if (!task) {
		call->fail(UnknownTaskText(id));
		return;
	}
	const auto session = call->session;
	const auto copy = *task;
	session->agent().removeTask(id);
	call->undoable([=](Fn<void(QString error)> finished) {
		session->agent().tasks().add(copy);
		finished({});
	});
	auto json = QJsonObject();
	InsertTaskRef(json, copy);
	json.insert("deleted", QJsonValue(true));
	call->done(std::move(json));
}

void RunNotify(not_null<PendingCall*> call) {
	const auto text = call->input.value("text").toString();
	if (text.isEmpty()) {
		call->fail(u"text is required, 1 to 1000 characters."_q);
		return;
	} else if (text.size() > kTaskNotifyMaxChars) {
		call->fail(u"text must be at most %1 characters."_q
			.arg(kTaskNotifyMaxChars));
		return;
	}
	if (!call->notify) {
		call->fail(u"Notifications are not available here."_q);
		return;
	}
	call->notify(text);
	auto json = QJsonObject();
	json.insert("notified", QJsonValue(true));
	call->done(std::move(json));
}

} // namespace

namespace TaskTools {

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	const auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	push(ToolEntry{
		.name = u"create_task"_q,
		.description = u"Create a background task that runs on new"
			" messages matching a filter, on a schedule or once at a set"
			" time. A confirmation card shows the trigger, the instruction"
			" (which the user can edit), the allowed actions and chats, the"
			" limits and the notification mode before the task starts."_q,
		.inputSchema = TaskInputSchema(true, false),
		.generalSchema = TaskInputSchema(false, false),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.background = false,
		},
		.group = u"tasks"_q,
		.untrusted = false,
		.run = RunCreateTask,
	});
	push(ToolEntry{
		.name = u"update_task"_q,
		.description = u"Change an existing background task by its id: the"
			" trigger, the instruction, the allowed actions and chats, the"
			" limits or the notification mode. A confirmation card shows the"
			" changes before they are applied."_q,
		.inputSchema = TaskInputSchema(true, true),
		.generalSchema = TaskInputSchema(false, true),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.background = false,
		},
		.group = u"tasks"_q,
		.untrusted = false,
		.run = RunUpdateTask,
	});
	push(ToolEntry{
		.name = u"list_tasks"_q,
		.description = u"List background tasks of this agent with their ids,"
			" triggers, instructions, allowed actions and chats, limits and"
			" states."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.background = false,
		},
		.group = u"tasks"_q,
		.untrusted = false,
		.run = RunListTasks,
	});
	push(ToolEntry{
		.name = u"pause_task"_q,
		.description = u"Pause or resume a background task by its id. A"
			" paused task triggers nothing."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				auto task = StringProperty(
					u"Task id from list_tasks."_q);
				auto paused = BoolProperty(
					u"True to pause the task, false to resume it."_q);
				properties.insert("task", task);
				properties.insert("paused", paused);
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"task"_q), QJsonValue(u"paused"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.background = false,
		},
		.group = u"tasks"_q,
		.untrusted = false,
		.action = true,
		.run = RunPauseTask,
	});
	push(ToolEntry{
		.name = u"delete_task"_q,
		.description = u"Delete a background task by its id, with its run"
			" log. It can be undone, without the run log."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				auto task = StringProperty(
					u"Task id from list_tasks."_q);
				properties.insert("task", task);
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"task"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.background = false,
		},
		.group = u"tasks"_q,
		.untrusted = false,
		.action = true,
		.run = RunDeleteTask,
	});
	push(ToolEntry{
		.name = u"notify"_q,
		.description = u"Show the user a system notification about this task"
			" run with a short text. Use it only for something important the"
			" user must know now, one or two sentences."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				auto text = StringProperty(
					u"The notification text, 1 to 1000 characters."_q);
				text.insert("minLength", QJsonValue(1));
				text.insert("maxLength", QJsonValue(kTaskNotifyMaxChars));
				properties.insert("text", text);
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"text"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.interactive = false,
		},
		.untrusted = false,
		.run = RunNotify,
	});
	return result;
}

} // namespace TaskTools
} // namespace Agent