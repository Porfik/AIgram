/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_format.h"

#include "agent/core/agent_prompts.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Agent::Ui {
namespace {

constexpr auto kMaxResultChars = 3000;

} // namespace

QJsonObject ToolResultObject(
		const std::optional<ToolResultBlock> &result) {
	if (!result || result->error) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(
		UnwrapUntrusted(result->content).toUtf8());
	return document.isObject() ? document.object() : QJsonObject();
}

int64 JsonIdValue(const QJsonValue &value) {
	return value.isString()
		? value.toString().toLongLong()
		: int64(value.toDouble());
}

HistoryItem *ToolResultMessage(
		Main::Session *session,
		const QJsonObject &input,
		const QJsonObject &object) {
	if (!session) {
		return nullptr;
	}
	const auto chat = object.contains(u"chat"_q)
		? object.value(u"chat"_q)
		: input.value(u"chat"_q);
	if (chat.isString() && !chat.toString().isEmpty()
		&& !chat.toString().at(0).isDigit()) {
		return nullptr;
	}
	const auto chatId = chat.isUndefined() ? 0 : JsonIdValue(chat);
	const auto messageId = object.contains(u"message"_q)
		? object.value(u"message"_q)
		: input.value(u"message_id"_q);
	if (!chatId || messageId.isUndefined()) {
		return nullptr;
	}
	return session->data().message(
		PeerId(uint64(chatId)),
		MsgId(JsonIdValue(messageId)));
}

int64 MediaDurationSeconds(HistoryItem *item) {
	if (!item || !item->media()) {
		return 0;
	}
	const auto document = item->media()->document();
	return (document && document->hasDuration())
		? (document->duration() + 500) / 1000
		: 0;
}

int64 ResultDurationSeconds(
		const QJsonObject &object,
		HistoryItem *item) {
	const auto value = object.value(u"duration"_q);
	return value.isDouble() ? int64(value.toDouble()) : MediaDurationSeconds(item);
}

QString WithDuration(QString title, int64 seconds) {
	if (seconds <= 0) {
		return title;
	}
	const auto hours = seconds / 3600;
	const auto minutes = (seconds % 3600) / 60;
	const auto rest = seconds % 60;
	const auto text = hours
		? u"%1:%2:%3"_q.arg(hours)
			.arg(minutes, 2, 10, QChar('0'))
			.arg(rest, 2, 10, QChar('0'))
		: u"%1:%2"_q.arg(minutes).arg(rest, 2, 10, QChar('0'));
	return title + ' ' + text;
}

bool ResultFromMediaModel(const QJsonObject &object) {
	return object.value(u"media_model"_q).toBool();
}

QString ToolTitleBaseText(
		const ToolUseBlock &call,
		const std::optional<ToolResultBlock> &result,
		Main::Session *session) {
	const auto &name = call.name;
	if (name == u"get_context"_q) {
		return tr::lng_agent_tool_get_context(tr::now);
	} else if (name == u"read_history"_q) {
		return tr::lng_agent_tool_read_history(tr::now);
	} else if (name == u"search_messages"_q) {
		return call.input.value("chat").isUndefined()
			? tr::lng_agent_tool_search_global(tr::now)
			: tr::lng_agent_tool_search_messages(tr::now);
	} else if (name == u"get_peer_info"_q) {
		return tr::lng_agent_tool_get_peer_info(tr::now);
	} else if (name == u"list_members"_q) {
		const auto filter = call.input.value(u"filter"_q).toString();
		if (filter == u"admins"_q) {
			return tr::lng_agent_tool_list_admins(tr::now);
		} else if (filter == u"banned"_q
			|| filter == u"restricted"_q) {
			return tr::lng_agent_tool_list_banned(tr::now);
		}
		return tr::lng_agent_tool_list_members(tr::now);
	} else if (name == u"list_chats"_q) {
		return tr::lng_agent_tool_list_chats(tr::now);
	} else if (name == u"find_setting"_q) {
		return tr::lng_agent_tool_find_setting(tr::now);
	} else if (name == u"open"_q) {
		return tr::lng_agent_tool_open(tr::now);
	} else if (name == u"load_tools"_q) {
		return tr::lng_agent_tool_load_tools(tr::now);
	} else if (name == u"list_folders"_q) {
		return tr::lng_agent_tool_list_folders(tr::now);
	} else if (name == u"read_chat_memory"_q) {
		return tr::lng_agent_tool_read_chat_memory(tr::now);
	} else if (name == u"view_media"_q) {
		const auto object = ToolResultObject(result);
		const auto type = object.value(u"type"_q).toString();
		if (type == u"photo"_q) {
			return tr::lng_agent_tool_view_photo(tr::now);
		} else if (type == u"video"_q
			|| type == u"round_video"_q
			|| type == u"gif"_q) {
			return tr::lng_agent_tool_view_video(tr::now);
		} else if (!object.value(u"attachment"_q).toString().isEmpty()) {
			return tr::lng_agent_tool_view_attachment(tr::now);
		}
		return tr::lng_agent_tool_view_media(tr::now);
	} else if (name == u"listen_audio"_q) {
		const auto object = ToolResultObject(result);
		const auto type = object.value(u"type"_q).toString();
		const auto title = (type == u"voice"_q || type == u"round_video"_q)
			? tr::lng_agent_tool_listen_voice(tr::now)
			: tr::lng_agent_tool_listen_audio(tr::now);
		return WithDuration(
			std::move(title),
			ResultDurationSeconds(
				object,
				ToolResultMessage(session, call.input, object)));
	} else if (name == u"transcribe_voice"_q) {
		const auto object = ToolResultObject(result);
		return WithDuration(
			tr::lng_agent_tool_transcribe(tr::now),
			MediaDurationSeconds(
				ToolResultMessage(session, call.input, object)));
	} else if (name == u"read_file"_q) {
		const auto fileName = ToolResultObject(result)
			.value(u"name"_q).toString();
		return fileName.isEmpty()
			? tr::lng_agent_tool_read_file_unknown(tr::now)
			: tr::lng_agent_tool_read_file(tr::now, lt_file, fileName);
	} else if (name == u"list_agent_files"_q) {
		return tr::lng_agent_tool_list_agent_files(tr::now);
	} else if (name == u"draft_files"_q) {
		return tr::lng_agent_tool_draft_files(tr::now);
	} else if (name == u"list_tasks"_q) {
		return tr::lng_agent_tool_list_tasks(tr::now);
	} else if (name == u"notify"_q) {
		return tr::lng_agent_tool_notify(tr::now);
	} else if (name == u"list_invite_links"_q) {
		return tr::lng_agent_tool_list_invite_links(tr::now);
	} else if (name == u"list_join_requests"_q) {
		return tr::lng_agent_tool_list_join_requests(tr::now);
	} else if (name == u"list_contacts"_q) {
		return tr::lng_agent_tool_list_contacts(tr::now);
	} else if (name == u"list_blocked"_q) {
		return tr::lng_agent_tool_list_blocked(tr::now);
	} else if (name == u"list_emoji_statuses"_q) {
		return tr::lng_agent_tool_list_emoji_statuses(tr::now);
	} else if (name == u"summarize_message"_q) {
		return tr::lng_agent_tool_summarize(tr::now);
	} else if (name == u"translate"_q) {
		return tr::lng_agent_tool_translate(tr::now);
	} else if (name == u"ai_compose"_q) {
		return tr::lng_agent_tool_ai_compose(tr::now);
	} else if (name == u"find_methods"_q) {
		return tr::lng_agent_tool_find_methods(tr::now);
	} else if (name == u"describe_method"_q) {
		const auto method = call.input.value(u"name"_q).toString().trimmed();
		return method.isEmpty()
			? tr::lng_agent_tool_other(tr::now, lt_name, name)
			: tr::lng_agent_tool_describe_method(
				tr::now,
				lt_name,
				method.left(64));
	} else if (name == u"call_method"_q) {
		const auto method = call.input.value(u"method"_q).toString().trimmed();
		return method.isEmpty()
			? tr::lng_agent_tool_other(tr::now, lt_name, name)
			: tr::lng_agent_tool_call_method(
				tr::now,
				lt_name,
				method.left(64));
	}
	return tr::lng_agent_tool_other(tr::now, lt_name, name);
}

QString ToolTitleText(
		const ToolUseBlock &call,
		const std::optional<ToolResultBlock> &result,
		Main::Session *session) {
	auto title = ToolTitleBaseText(call, result, session);
	if (ResultFromMediaModel(ToolResultObject(result))) {
		title += u" "_q + tr::lng_agent_tool_media_model(tr::now);
	}
	return title;
}

bool IsMediaTool(const QString &name) {
	return name == u"view_media"_q
		|| name == u"listen_audio"_q
		|| name == u"transcribe_voice"_q
		|| name == u"read_file"_q;
}

MediaBlock ResultMediaBlock(const QJsonObject &object) {
	auto block = MediaBlock();
	block.attachment = object.value(u"attachment"_q).toString();
	if (!block.attachment.isEmpty()) {
		return block;
	}
	const auto chat = object.value(u"chat"_q);
	if (chat.isString() && !chat.toString().isEmpty()
		&& !chat.toString().at(0).isDigit()) {
		return block;
	}
	block.peer = chat.isUndefined() ? 0 : uint64(JsonIdValue(chat));
	block.messageId = object.value(u"message"_q).isUndefined()
		? 0
		: JsonIdValue(object.value(u"message"_q));
	return block;
}

QString ResultTranscriptText(
		const QString &name,
		const QJsonObject &object) {
	if (name == u"listen_audio"_q) {
		return object.value(u"transcript"_q).toString();
	} else if (name == u"view_media"_q) {
		return object.value(u"description"_q).toString();
	} else if (name == u"transcribe_voice"_q) {
		return object.value(u"text"_q).toString();
	} else if (name == u"read_file"_q
		&& object.value(u"media_model"_q).toBool()) {
		return object.value(u"text"_q).toString();
	}
	return QString();
}

QString CutForDisplay(const QString &text) {
	if (text.size() <= kMaxResultChars) {
		return text;
	}
	auto cut = text.left(kMaxResultChars);
	while (!cut.isEmpty() && cut.back().isHighSurrogate()) {
		cut.chop(1);
	}
	return cut + QChar(0x2026);
}

QString IndentedJson(const QJsonDocument &document) {
	const auto lines = QString::fromUtf8(
		document.toJson(QJsonDocument::Indented)).trimmed().split('\n');
	auto result = QStringList();
	result.reserve(lines.size());
	for (const auto &line : lines) {
		auto spaces = 0;
		while (spaces < line.size() && line[spaces] == ' ') {
			++spaces;
		}
		result.push_back(QString(spaces / 2, ' ') + line.mid(spaces));
	}
	return result.join('\n');
}

QString PrettyJson(const QString &text) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || document.isNull()) {
		return text;
	}
	return IndentedJson(document);
}

QString ArgumentsText(const ToolUseBlock &call) {
	if (!call.inputError.isEmpty()) {
		return QString::fromUtf8(call.rawInput);
	} else if (call.input.isEmpty()) {
		return QString();
	}
	return IndentedJson(QJsonDocument(call.input));
}

QString ResultText(const ToolResultBlock &result) {
	return CutForDisplay(PrettyJson(UnwrapUntrusted(result.content)));
}

TextWithEntities Monospace(QString text) {
	auto entities = EntitiesInText();
	if (!text.isEmpty()) {
		entities.push_back(EntityInText(EntityType::Code, 0, text.size()));
	}
	return { std::move(text), std::move(entities) };
}

ParsedAction ParseActionResult(
		const std::optional<ToolResultBlock> &result) {
	auto parsed = ParsedAction();
	if (!result || result->error) {
		return parsed;
	}
	const auto document = QJsonDocument::fromJson(
		UnwrapUntrusted(result->content).toUtf8());
	if (!document.isObject()) {
		return parsed;
	}
	const auto object = document.object();
	parsed.folder = object.value("folder").toObject()
		.value("title").toString();
	parsed.fact = object.value("fact").toObject()
		.value("text").toString();
	parsed.taskTitle = object.value("task").toObject()
		.value("title").toString();
	parsed.taskState = object.value("state").toString();
	parsed.name = object.value("name").toString();
	for (const auto value : object.value("done").toArray()) {
		const auto entry = value.toObject();
		const auto title = entry.value("title").toString();
		if (!title.isEmpty()) {
			parsed.done.push_back(title);
		}
		const auto file = entry.value("file").toString();
		if (!file.isEmpty()) {
			parsed.saved.push_back(file);
		}
		if (parsed.savedPath.isEmpty()) {
			parsed.savedPath = entry.value("path").toString();
		}
	}
	for (const auto value : object.value("failed").toArray()) {
		const auto entry = value.toObject();
		const auto title = entry.value("title").toString();
		const auto error = entry.value("error").toString();
		if (!title.isEmpty()) {
			parsed.failedTitles.push_back(title);
		}
		if (!error.isEmpty()) {
			parsed.failed.push_back(tr::lng_agent_action_failed(
				tr::now,
				lt_error,
				title.isEmpty() ? error : title + u": "_q + error));
		} else if (!title.isEmpty()) {
			parsed.failed.push_back(title);
		}
	}
	return parsed;
}

QString ActionMainText(
		const QString &name,
		const QJsonObject &input,
		const QString &chats,
		const QString &folder,
		const QString &fact,
		const ParsedAction &parsed) {
	if (name == u"mark_read"_q) {
		return tr::lng_agent_action_mark_read(tr::now, lt_chats, chats);
	} else if (name == u"set_mute"_q) {
		return input.value("muted").toBool(true)
			? tr::lng_agent_action_mute(tr::now, lt_chats, chats)
			: tr::lng_agent_action_unmute(tr::now, lt_chats, chats);
	} else if (name == u"set_archived"_q) {
		return input.value("archived").toBool(true)
			? tr::lng_agent_action_archive(tr::now, lt_chats, chats)
			: tr::lng_agent_action_unarchive(tr::now, lt_chats, chats);
	} else if (name == u"set_pinned"_q) {
		return input.value("pinned").toBool(true)
			? tr::lng_agent_action_pin_chat(tr::now, lt_chats, chats)
			: tr::lng_agent_action_unpin_chat(tr::now, lt_chats, chats);
	} else if (name == u"set_draft"_q) {
		return tr::lng_agent_action_draft(tr::now, lt_chats, chats);
	} else if (name == u"create_folder"_q) {
		return tr::lng_agent_action_folder_create(
			tr::now,
			lt_folder,
			folder);
	} else if (name == u"rename_folder"_q) {
		return tr::lng_agent_action_folder_rename(
			tr::now,
			lt_folder,
			folder);
	} else if (name == u"set_folder_chats"_q) {
		return input.value("included").toBool(true)
			? tr::lng_agent_action_folder_add(
				tr::now,
				lt_chats,
				chats,
				lt_folder,
				folder)
			: tr::lng_agent_action_folder_remove(
				tr::now,
				lt_chats,
				chats,
				lt_folder,
				folder);
	} else if (name == u"reorder_folders"_q) {
		return tr::lng_agent_action_folder_order(tr::now);
	} else if (name == u"memory_add"_q) {
		return tr::lng_agent_action_memory_add(
			tr::now,
			lt_text,
			fact.isEmpty() ? input.value("text").toString() : fact);
	} else if (name == u"memory_update"_q) {
		return tr::lng_agent_action_memory_update(
			tr::now,
			lt_text,
			fact.isEmpty() ? input.value("text").toString() : fact);
	} else if (name == u"memory_remove"_q) {
		return tr::lng_agent_action_memory_remove(tr::now, lt_text, fact);
	} else if (name == u"save_files"_q) {
		return tr::lng_agent_action_saved(
			tr::now,
			lt_file,
			parsed.saved.isEmpty()
				? parsed.chats()
				: parsed.saved.join(u", "_q));
	} else if (name == u"pause_task"_q) {
		const auto title = parsed.taskTitle.isEmpty()
			? input.value("task").toString()
			: parsed.taskTitle;
		return (parsed.taskState == u"active"_q)
			? tr::lng_agent_action_task_resume(tr::now, lt_task, title)
			: tr::lng_agent_action_task_pause(tr::now, lt_task, title);
	} else if (name == u"delete_task"_q) {
		return tr::lng_agent_action_task_delete(
			tr::now,
			lt_task,
			parsed.taskTitle.isEmpty()
				? input.value("task").toString()
				: parsed.taskTitle);
	} else if (name == u"rename_contact"_q) {
		return tr::lng_agent_action_rename_contact(
			tr::now,
			lt_name,
			parsed.name);
	}
	return name;
}

QString CardTitleText(CardKind kind) {
	switch (kind) {
	case CardKind::Send:
		return tr::lng_agent_card_title_send(tr::now);
	case CardKind::Transcribe:
		return tr::lng_agent_card_title_transcribe(tr::now);
	case CardKind::Download:
		return tr::lng_agent_card_title_download(tr::now);
	case CardKind::SendFiles:
		return tr::lng_agent_card_title_send_files(tr::now);
	case CardKind::Draft:
		return tr::lng_agent_card_title_draft(tr::now);
	case CardKind::Forward:
		return tr::lng_agent_card_title_forward(tr::now);
	case CardKind::Edit:
		return tr::lng_agent_card_title_edit(tr::now);
	case CardKind::Delete:
		return tr::lng_agent_card_title_delete(tr::now);
	case CardKind::React:
		return tr::lng_agent_card_title_react(tr::now);
	case CardKind::PinMessage:
		return tr::lng_agent_card_title_pin(tr::now);
	case CardKind::UnpinMessage:
		return tr::lng_agent_card_title_unpin(tr::now);
	case CardKind::DeleteFolder:
		return tr::lng_agent_card_title_delete_folder(tr::now);
	case CardKind::Handoff:
		return tr::lng_agent_card_title_handoff(tr::now);
	case CardKind::CreateTask:
		return tr::lng_agent_card_title_create_task(tr::now);
	case CardKind::UpdateTask:
		return tr::lng_agent_card_title_update_task(tr::now);
	case CardKind::Action:
		return tr::lng_agent_card_title_action(tr::now);
	case CardKind::Method:
		return tr::lng_agent_card_title_method(tr::now);
	}
	return QString();
}

} // namespace Agent::Ui
