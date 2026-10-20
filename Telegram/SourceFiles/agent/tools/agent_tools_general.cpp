/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/core/agent_navigation.h"
#include "agent/tools/agent_tool_format.h"
#include "base/flat_set.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QVariant>

#include <limits>
#include <vector>

namespace Agent::GeneralTools {
namespace {

constexpr auto kDefaultListChatsLimit = 50;
constexpr auto kMaxListChatsLimit = 100;
constexpr auto kMaxFindSettingResults = 10;

[[nodiscard]] std::optional<int> JsonInt(
		const QJsonObject &object,
		const QString &key,
		QString &error) {
	const auto value = object.value(key);
	if (value.isUndefined() || value.isNull()) {
		return std::nullopt;
	}
	if (!value.isDouble()) {
		error = u"%1 must be a number."_q.arg(key);
		return std::nullopt;
	}
	return static_cast<int>(value.toDouble());
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

[[nodiscard]] QString KindFilter(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->isBot() ? u"bot"_q : u"user"_q;
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast() ? u"channel"_q : u"group"_q;
	}
	return u"group"_q;
}

struct ChatsSource {
	not_null<Dialogs::MainList*> list;
	FilterId filterId = 0;
};

[[nodiscard]] QJsonObject SerializeChatListEntry(
		not_null<Main::Session*> session,
		not_null<History*> history,
		FilterId filterId) {
	const auto peer = history->peer;
	auto result = QJsonObject();
	result.insert("id", QJsonValue(qint64(peer->id.value)));
	result.insert("title", peer->name());
	result.insert("kind", ToolFormat::PeerKind(peer));
	const auto username = ToolFormat::PeerUsername(peer);
	if (!username.isEmpty()) {
		result.insert("username", username);
	}
	result.insert(
		"unread",
		QJsonValue(
			history->unreadCountKnown() ? history->unreadCount() : 0));
	result.insert(
		"muted",
		QJsonValue(session->data().notifySettings().isMuted(peer)));
	result.insert("pinned", QJsonValue(history->isPinnedDialog(filterId)));
	result.insert("archived", QJsonValue(history->folder() != nullptr));
	if (const auto item = history->lastMessage()) {
		result.insert(
			"last_date",
			QDateTime::fromSecsSinceEpoch(item->date())
				.toString(u"yyyy-MM-dd HH:mm"_q));
	}
	return result;
}

void RunListChats(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto &input = call->input;
	auto error = QString();
	const auto folderValue = JsonInt(input, "folder_id", error);
	const auto limitValue = JsonInt(input, "limit", error);
	const auto offsetValue = JsonInt(input, "offset", error);
	const auto unreadOnly = JsonBool(input, "unread_only", error);
	const auto archived = JsonBool(input, "archived", error);
	const auto kind = input.value("kind").toString().trimmed();
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto kindFilter = QString();
	if (!kind.isEmpty()
		&& kind != u"user"_q
		&& kind != u"bot"_q
		&& kind != u"group"_q
		&& kind != u"channel"_q) {
		call->fail(u"kind must be one of user, bot, group, channel."_q);
		return;
	}
	kindFilter = kind;
	const auto limit = limitValue.value_or(kDefaultListChatsLimit);
	if (limit < 1 || limit > kMaxListChatsLimit) {
		call->fail(u"limit must be between 1 and 100."_q);
		return;
	}
	const auto offset = offsetValue.value_or(0);
	if (offset < 0) {
		call->fail(u"offset must not be negative."_q);
		return;
	}
	const auto folderId = folderValue.value_or(0);
	auto sources = std::vector<ChatsSource>();
	auto complete = true;
	if (folderId > 0) {
		auto &filters = session->data().chatsFilters();
		const auto list = filters.chatsList(FilterId(folderId));
		complete = list->loaded() && filters.loaded();
		sources.push_back({ list, FilterId(folderId) });
	} else {
		const auto main = session->data().chatsList();
		complete = complete && main->loaded();
		sources.push_back({ main, FilterId() });
		if (const auto folder
			= session->data().folderLoaded(Data::Folder::kId)) {
			const auto archive = folder->chatsList();
			complete = complete && archive->loaded();
			sources.push_back({ archive, FilterId() });
		}
	}
	auto seen = base::flat_set<PeerKey>();
	auto chats = QJsonArray();
	auto hasMore = false;
	auto skipped = 0;
	for (const auto &source : sources) {
		if (hasMore) {
			break;
		}
		for (const auto &row : source.list->indexed()->all()) {
			const auto history = row->history();
			if (!history) {
				continue;
			}
			const auto peer = history->peer;
			if (!seen.emplace(peer->id.value).second) {
				continue;
			}
			if (!call->gate->visible(peer->id.value)) {
				continue;
			}
			const auto unread = history->unreadCountKnown()
				? history->unreadCount()
				: 0;
			if (unreadOnly.value_or(false) && unread <= 0) {
				continue;
			}
			if (archived.has_value()
				&& (*archived != (history->folder() != nullptr))) {
				continue;
			}
			if (!kindFilter.isEmpty() && KindFilter(peer) != kindFilter) {
				continue;
			}
			if (skipped < offset) {
				++skipped;
				continue;
			}
			if (int(chats.size()) == limit) {
				hasMore = true;
				break;
			}
			chats.push_back(
				SerializeChatListEntry(session, history, source.filterId));
		}
	}
	auto json = QJsonObject();
	json.insert("chats", chats);
	json.insert("complete", QJsonValue(complete));
	json.insert("has_more", QJsonValue(hasMore));
	call->done(std::move(json));
}

void RunFindSetting(not_null<PendingCall*> call) {
	const auto query = call->input.value("query").toString().trimmed();
	if (query.isEmpty()) {
		call->fail(u"query must not be empty."_q);
		return;
	}
	auto &registry = Settings::Builder::SearchRegistry::Instance();
	const auto raw = registry.collectAll(call->session);
	auto entries = std::vector<SettingEntry>();
	entries.reserve(raw.size());
	for (const auto &entry : raw) {
		entries.push_back(SettingEntry{
			.id = entry.id,
			.title = entry.title,
			.keywords = entry.keywords,
			.path = registry.sectionPath(entry.section),
			.deeplink = entry.deeplink,
			.checked = (entry.checkIcon
				== Settings::Builder::SearchEntryCheckIcon::Checked)
			? std::optional<bool>(true)
			: (entry.checkIcon
				== Settings::Builder::SearchEntryCheckIcon::Unchecked)
			? std::optional<bool>(false)
			: std::nullopt,
		});
	}
	const auto ranked = RankSettings(query, entries, kMaxFindSettingResults);
	auto settings = QJsonArray();
	for (const auto index : ranked) {
		const auto &entry = entries[index];
		auto json = QJsonObject();
		if (!entry.id.isEmpty()) {
			json.insert("id", entry.id);
		}
		json.insert("title", entry.title);
		if (!entry.path.isEmpty()) {
			json.insert("path", entry.path);
		}
		if (entry.checked.has_value()) {
			json.insert("checked", QJsonValue(*entry.checked));
		}
		if (!entry.deeplink.isEmpty()) {
			json.insert("link", entry.deeplink);
		}
		settings.push_back(json);
	}
	auto result = QJsonObject();
	result.insert(
		"language",
		Lang::LanguageIdOrDefault(Lang::Id()));
	result.insert("settings", settings);
	call->done(std::move(result));
}

void RunOpen(not_null<PendingCall*> call) {
	const auto &input = call->input;
	auto error = QString();
	const auto messageIdValue = JsonInt(input, "message_id", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto link = input.value("link").toString().trimmed();
	const auto controller = call->session->tryResolveWindow();
	if (!controller) {
		call->fail(u"No open window to show anything in."_q);
		return;
	}
	if (call->peer) {
		const auto messageId = messageIdValue.value_or(0);
		controller->showPeerHistory(
			call->peer->id,
			Window::SectionShow{ Window::SectionShow::Way::Forward },
			MsgId(messageId));
		call->done({ { "opened", QJsonValue(true) } });
		return;
	}
	if (link.isEmpty()) {
		call->fail(u"Give a link or a chat to open."_q);
		return;
	}
	const auto prefix = u"tg://settings/"_q;
	if (!link.startsWith(prefix, Qt::CaseInsensitive)) {
		call->fail(u"Only tg://settings/... links are supported."_q);
		return;
	}
	if (messageIdValue.has_value()) {
		call->fail(u"message_id is only supported together with a chat."_q);
		return;
	}
	auto context = ClickHandlerContext();
	context.sessionWindow = base::make_weak(controller);
	if (!Core::App().openLocalUrl(link, QVariant::fromValue(context))) {
		call->fail(u"The link could not be opened."_q);
		return;
	}
	call->done({ { "opened", QJsonValue(true) } });
}

void RunContinueInGeneral(not_null<PendingCall*> call) {
	const auto request = call->input.value("request").toString().trimmed();
	if (request.isEmpty()) {
		call->fail(u"request must not be empty."_q);
		return;
	}
	const auto peer = call->peer;
	if (!peer) {
		call->fail(u"This tool works only inside a chat."_q);
		return;
	}
	auto card = Card{
		.toolUseId = call->toolUseId,
		.kind = CardKind::Handoff,
		.status = CardStatus::Done,
		.text = request,
		.proposedText = request,
	};
	card.targets.push_back(CardTarget{
		.peer = peer->id.value,
		.messageId = 0,
		.title = peer->name(),
	});
	call->finishCard(std::move(card));
}

std::vector<ToolEntry> BuildEntries() {
	auto result = std::vector<ToolEntry>();
	auto listProperties = QJsonObject();
	listProperties.insert("folder_id", IntProperty(
		0,
		std::numeric_limits<int>::max(),
		u"Only chats of the folder (filter) with this id."_q));
	listProperties.insert("unread_only", BoolProperty(
		u"Only chats with unread messages."_q));
	listProperties.insert("kind", StringProperty(
		u"One of user, bot, group, channel."_q));
	listProperties.insert("archived", BoolProperty(
		u"Only archived chats (true) or only non-archived ones (false)."_q));
	listProperties.insert("limit", IntProperty(
		1,
		kMaxListChatsLimit,
		u"How many chats to return, 1..100, default 50."_q));
	listProperties.insert("offset", IntProperty(
		0,
		std::numeric_limits<int>::max(),
		u"Skip this many matching chats."_q));
	auto findProperties = QJsonObject();
	findProperties.insert("query", StringProperty(
		u"Text to search for in setting titles, keywords and sections."_q));
	auto openProperties = QJsonObject();
	openProperties.insert("link", StringProperty(
		u"A tg://settings/... deep link as returned by find_setting."_q));
	openProperties.insert("message_id", MessageIdProperty(
		u"Show this message after opening the chat."_q));
	auto handoffProperties = QJsonObject();
	handoffProperties.insert("request", StringProperty(
		u"What the general agent should do next."_q));
	auto push = [&](const QString &name,
			const QString &description,
			QJsonObject schema,
			ToolPolicy policy,
			ChatParam chatParam,
			bool untrusted,
			auto run) {
		result.push_back(ToolEntry{
			.name = name,
			.description = description,
			.inputSchema = std::move(schema),
			.policy = policy,
			.chatParam = chatParam,
			.untrusted = untrusted,
			.run = run,
		});
	};
	push(
		u"list_chats"_q,
		u"List the account's chats with titles, kinds, unread counts"
		" and last message dates. Filters narrow the list; page with"
		" limit and offset. complete=false means the chat list is"
		" still loading and the result may miss chats."_q,
		ToolSchema(std::move(listProperties), QJsonArray()),
		ToolPolicy{ .general = true, .chat = false },
		ChatParam::None,
		true,
		RunListChats);
	push(
		u"find_setting"_q,
		u"Search the app settings. The query must be in the interface"
		" language of the app, the language of the returned titles;"
		" try synonyms or shorter words when nothing is found."
		" Returns matching settings entries with their paths, check"
		" state and deep links to open."_q,
		ToolSchema(
			std::move(findProperties),
			QJsonArray{ QJsonValue(u"query"_q) }),
		ToolPolicy{ .general = true, .chat = false, .background = false },
		ChatParam::None,
		false,
		RunFindSetting);
	push(
		u"open"_q,
		u"Open a settings deep link from find_setting, or a chat"
		" (optionally at a message) on screen."_q,
		ToolSchema(std::move(openProperties), QJsonArray()),
		ToolPolicy{ .general = true, .chat = false, .background = false },
		ChatParam::OptionalOne,
		false,
		RunOpen);
	push(
		u"continue_in_general"_q,
		u"Hand the current conversation over to the general agent."
		" Use it when the user wants something outside this chat."_q,
		ToolSchema(
			std::move(handoffProperties),
			QJsonArray{ QJsonValue(u"request"_q) }),
		ToolPolicy{ .general = false, .chat = true, .background = false },
		ChatParam::None,
		false,
		RunContinueInGeneral);
	return result;
}

} // namespace

std::vector<ToolEntry> Entries() {
	static const auto entries = BuildEntries();
	return {
		begin(entries),
		end(entries),
	};
}

} // namespace Agent::GeneralTools
