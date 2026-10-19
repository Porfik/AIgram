/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "apiwrap.h"
#include "base/algorithm.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonArray>

#include <limits>

namespace Agent {
namespace {

constexpr auto kMaxTitleLength = 12;

[[nodiscard]] const Data::ChatFilters &ChatFilters(
		not_null<Main::Session*> session) {
	return session->data().chatsFilters();
}

[[nodiscard]] QString FolderTitle(const Data::ChatFilter &filter) {
	return filter.titleText().text;
}

void AddChatResult(
		QJsonArray &to,
		not_null<PeerData*> peer,
		QString error = {}) {
	auto item = QJsonObject();
	item.insert("chat", QJsonValue(qint64(peer->id.value)));
	item.insert("title", peer->name());
	if (!error.isEmpty()) {
		item.insert("error", error);
	}
	to.push_back(item);
}

void DeliverChatResults(
		not_null<PendingCall*> call,
		const Data::ChatFilter &filter,
		QJsonArray done,
		QJsonArray failed) {
	auto folder = QJsonObject();
	folder.insert("id", QJsonValue(qint64(filter.id())));
	folder.insert("title", FolderTitle(filter));
	auto json = QJsonObject();
	json.insert("folder", folder);
	json.insert("done", done);
	json.insert("failed", failed);
	call->done(std::move(json));
}

[[nodiscard]] std::optional<FilterId> JsonFolderId(
		const QJsonObject &object,
		QString &error) {
	const auto value = object.value("folder_id");
	if (value.isUndefined() || value.isNull()) {
		error = u"folder_id is required."_q;
		return std::nullopt;
	}
	if (!value.isDouble()) {
		error = u"folder_id must be a number."_q;
		return std::nullopt;
	}
	const auto id = value.toInt();
	if (id <= 0) {
		error = u"folder_id must be a positive folder id."_q;
		return std::nullopt;
	}
	return id;
}

[[nodiscard]] QString JsonTitle(
		const QJsonObject &object,
		QString &error) {
	const auto value = object.value("title");
	if (value.isUndefined() || value.isNull()) {
		error = u"title is required."_q;
		return QString();
	}
	const auto title = value.toString().trimmed();
	if (title.isEmpty()) {
		error = u"title must not be empty."_q;
		return QString();
	}
	if (title.size() > kMaxTitleLength) {
		error = u"title must be at most %1 characters."_q
			.arg(kMaxTitleLength);
		return QString();
	}
	return title;
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

[[nodiscard]] std::optional<Data::ChatFilter> FindFolder(
		not_null<Main::Session*> session,
		FilterId id) {
	const auto &list = ChatFilters(session).list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	if (i == end(list)) {
		return std::nullopt;
	}
	return *i;
}

void SendFilterUpdate(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter) {
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(filter.id()),
		filter.tl()
	)).send();
}

void SendFilterRemove(
		not_null<Main::Session*> session,
		FilterId id) {
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag(0)),
		MTP_int(id),
		MTPDialogFilter()
	)).send();
}

// Mirrors Data::ChangedFilter() from boxes/choose_filter_box.cpp.
[[nodiscard]] Data::ChatFilter ChangedFilter(
		const Data::ChatFilter &filter,
		not_null<History*> history,
		bool add) {
	auto always = base::duplicate(filter.always());
	auto pinned = filter.pinned();
	auto never = base::duplicate(filter.never());
	if (add) {
		never.remove(history);
	} else {
		always.remove(history);
		pinned.erase(ranges::remove(pinned, history), end(pinned));
	}
	const auto result = Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.colorIndex(),
		filter.flags(),
		std::move(always),
		std::move(pinned),
		std::move(never));
	if (result.contains(history) == add) {
		return result;
	}
	auto resultAlways = base::duplicate(result.always());
	auto resultPinned = result.pinned();
	auto resultNever = base::duplicate(result.never());
	if (add) {
		resultAlways.insert(history);
	} else {
		resultNever.insert(history);
	}
	return Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.colorIndex(),
		filter.flags(),
		std::move(resultAlways),
		std::move(resultPinned),
		std::move(resultNever));
}

void RunListFolders(not_null<PendingCall*> call) {
	const auto &list = ChatFilters(call->session).list();
	History *history = nullptr;
	if (call->peer) {
		history = call->session->data().history(call->peer);
	}
	auto folders = QJsonArray();
	for (const auto &filter : list) {
		if (!filter.id()) {
			continue;
		}
		auto item = QJsonObject();
		item.insert("id", QJsonValue(qint64(filter.id())));
		item.insert("title", FolderTitle(filter));
		item.insert("count", QJsonValue(int(filter.always().size())));
		if (history) {
			item.insert("in", QJsonValue(filter.contains(history)));
		}
		folders.push_back(item);
	}
	auto json = QJsonObject();
	json.insert("folders", folders);
	call->done(std::move(json));
}

void RunCreateFolder(not_null<PendingCall*> call) {
	auto error = QString();
	const auto title = JsonTitle(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto session = call->session;
	if (!ChatFilters(session).loaded()) {
		call->fail(u"Folders are not loaded yet, try again later."_q);
		return;
	}
	const auto &list = ChatFilters(session).list();
	const auto chatsLimit = Data::PremiumLimits(
		session).dialogFiltersChatsCurrent();
	if (int(call->peers.size()) > chatsLimit) {
		call->fail(u"A folder holds at most %1 chats."_q.arg(chatsLimit));
		return;
	}
	const auto limit = Data::PremiumLimits(session).dialogFiltersCurrent();
	if (int(list.size()) >= limit) {
		call->fail(u"The folder limit is reached (%1)."_q.arg(limit));
		return;
	}
	auto existing = std::vector<FilterId>();
	existing.reserve(list.size());
	for (const auto &filter : list) {
		existing.push_back(filter.id());
	}
	auto id = FilterId(2);
	while (ranges::contains(existing, id)) {
		++id;
	}
	auto always = base::flat_set<not_null<History*>>();
	auto done = QJsonArray();
	for (const auto &peer : call->peers) {
		always.insert(session->data().history(peer));
		AddChatResult(done, peer);
	}
	const auto filter = Data::ChatFilter(
		id,
		Data::ChatFilterTitle{ .text = { title }, .isStatic = false },
		QString(),
		std::nullopt,
		Data::ChatFilter::Flags(),
		std::move(always),
		std::vector<not_null<History*>>(),
		base::flat_set<not_null<History*>>());
	session->data().chatsFilters().set(filter);
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(id),
		filter.tl()
	)).done(crl::guard(call.get(), [=]() mutable {
		call->undoable([=](Fn<void(QString error)> finished) {
			session->data().chatsFilters().remove(id);
			SendFilterRemove(session, id);
			finished({});
		});
		auto folder = QJsonObject();
		folder.insert("id", QJsonValue(qint64(id)));
		folder.insert("title", title);
		auto json = QJsonObject();
		json.insert("folder", folder);
		json.insert("done", done);
		json.insert("failed", QJsonArray());
		call->done(std::move(json));
	})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
		session->data().chatsFilters().remove(id);
		call->fail(error.type());
	})).send();
}

void RunRenameFolder(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = JsonFolderId(call->input, error);
	const auto title = error.isEmpty()
		? JsonTitle(call->input, error)
		: QString();
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto session = call->session;
	if (!ChatFilters(session).loaded()) {
		call->fail(u"Folders are not loaded yet, try again later."_q);
		return;
	}
	const auto was = FindFolder(session, *id);
	if (!was) {
		call->fail(u"Unknown folder_id."_q);
		return;
	}
	if (FolderTitle(*was) == title) {
		auto folder = QJsonObject();
		folder.insert("id", QJsonValue(qint64(*id)));
		folder.insert("title", title);
		auto json = QJsonObject();
		json.insert("folder", folder);
		call->done(std::move(json));
		return;
	}
	const auto updated = was->withTitle(
		Data::ChatFilterTitle{ .text = { title }, .isStatic = false });
	session->data().chatsFilters().set(updated);
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(*id),
		updated.tl()
	)).done(crl::guard(call.get(), [=]() mutable {
		call->undoable([=](Fn<void(QString error)> finished) {
			session->data().chatsFilters().set(*was);
			SendFilterUpdate(session, *was);
			finished({});
		});
		auto folder = QJsonObject();
		folder.insert("id", QJsonValue(qint64(*id)));
		folder.insert("title", title);
		auto json = QJsonObject();
		json.insert("folder", folder);
		call->done(std::move(json));
	})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
		session->data().chatsFilters().set(*was);
		call->fail(error.type());
	})).send();
}

void RunSetFolderChats(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = JsonFolderId(call->input, error);
	const auto included = error.isEmpty()
		? JsonBool(call->input, "included", error)
		: std::nullopt;
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto session = call->session;
	if (!ChatFilters(session).loaded()) {
		call->fail(u"Folders are not loaded yet, try again later."_q);
		return;
	}
	const auto was = FindFolder(session, *id);
	if (!was) {
		call->fail(u"Unknown folder_id."_q);
		return;
	}
	if (was->chatlist()) {
		call->fail(u"Shared folders are managed in Settings."_q);
		return;
	}
	const auto limit = Data::PremiumLimits(
		session).dialogFiltersChatsCurrent();
	auto filter = *was;
	auto done = QJsonArray();
	auto failed = QJsonArray();
	for (const auto &peer : call->peers) {
		const auto history = session->data().history(peer);
		if (filter.contains(history) == *included) {
			AddChatResult(done, peer);
			continue;
		}
		if (*included
			&& int(filter.always().size()) >= limit
			&& !filter.always().contains(history)) {
			AddChatResult(
				failed,
				peer,
				u"The folder chats limit is reached (%1)."_q.arg(limit));
			continue;
		}
		filter = ChangedFilter(filter, history, *included);
		AddChatResult(done, peer);
	}
	if (filter == *was) {
		DeliverChatResults(call, filter, std::move(done), std::move(failed));
		return;
	}
	session->data().chatsFilters().set(filter);
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(*id),
		filter.tl()
	)).done(crl::guard(call.get(), [=]() mutable {
		call->undoable([=](Fn<void(QString error)> finished) {
			session->data().chatsFilters().set(*was);
			SendFilterUpdate(session, *was);
			finished({});
		});
		DeliverChatResults(call, filter, done, failed);
	})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
		session->data().chatsFilters().set(*was);
		call->fail(error.type());
	})).send();
}

void RunReorderFolders(not_null<PendingCall*> call) {
	const auto value = call->input.value("order");
	if (!value.isArray()) {
		call->fail(u"order must be an array of folder ids."_q);
		return;
	}
	const auto session = call->session;
	if (!ChatFilters(session).loaded()) {
		call->fail(u"Folders are not loaded yet, try again later."_q);
		return;
	}
	const auto &list = ChatFilters(session).list();
	auto ids = std::vector<FilterId>();
	for (const auto &item : value.toArray()) {
		if (!item.isDouble()) {
			call->fail(u"order must be an array of folder ids."_q);
			return;
		}
		const auto id = FilterId(item.toInt());
		if (id <= 0 || !ranges::contains(list, id, &Data::ChatFilter::id)) {
			call->fail(u"Unknown folder id in order."_q);
			return;
		}
		ids.push_back(id);
	}
	auto sortedIds = ids;
	ranges::sort(sortedIds);
	auto sortedCurrent = std::vector<FilterId>();
	sortedCurrent.reserve(list.size());
	for (const auto &filter : list) {
		if (filter.id() != 0) {
			sortedCurrent.push_back(filter.id());
		}
	}
	ranges::sort(sortedCurrent);
	if (sortedIds != sortedCurrent) {
		call->fail(u"order must contain every folder id exactly once."_q);
		return;
	}
	auto wasOrder = std::vector<FilterId>();
	wasOrder.reserve(list.size());
	for (const auto &filter : list) {
		wasOrder.push_back(filter.id());
	}
	auto wanted = ids;
	if (!ranges::contains(wanted, FilterId(0))) {
		auto position = 0;
		for (const auto &filter : list) {
			if (!filter.id()) {
				break;
			}
			++position;
		}
		wanted.insert(begin(wanted) + position, FilterId(0));
	}
	auto mtpIds = QVector<MTPint>();
	mtpIds.reserve(int(wanted.size()));
	for (const auto id : wanted) {
		mtpIds.push_back(MTP_int(id));
	}
	const auto wrapped = MTP_vector<MTPint>(mtpIds);
	session->data().chatsFilters().apply(
		MTP_updateDialogFilterOrder(wrapped));
	session->api().request(MTPmessages_UpdateDialogFiltersOrder(wrapped)
	).done(crl::guard(call.get(), [=]() mutable {
		call->undoable([=](Fn<void(QString error)> finished) {
			session->data().chatsFilters().saveOrder(wasOrder);
			finished({});
		});
		auto order = QJsonArray();
		for (const auto id : ids) {
			order.push_back(QJsonValue(qint64(id)));
		}
		auto json = QJsonObject();
		json.insert("order", order);
		call->done(std::move(json));
	})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
		auto restore = QVector<MTPint>();
		restore.reserve(int(wasOrder.size()));
		for (const auto id : wasOrder) {
			restore.push_back(MTP_int(id));
		}
		session->data().chatsFilters().apply(
			MTP_updateDialogFilterOrder(MTP_vector<MTPint>(restore)));
		call->fail(error.type());
	})).send();
}

void RunDeleteFolder(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = JsonFolderId(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto session = call->session;
	const auto was = FindFolder(session, *id);
	if (!was) {
		call->fail(u"Unknown folder_id."_q);
		return;
	}
	if (was->chatlist()) {
		call->fail(u"Shared folders are managed in Settings."_q);
		return;
	}
	auto card = Card();
	card.toolUseId = call->toolUseId;
	card.kind = CardKind::DeleteFolder;
	card.text = FolderTitle(*was);
	call->confirm(std::move(card), [=](Card decided) {
		session->data().chatsFilters().remove(*id);
		session->api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag(0)),
			MTP_int(*id),
			MTPDialogFilter()
		)).done(crl::guard(call.get(), [=]() mutable {
			decided.status = CardStatus::Done;
			call->finishCard(decided);
		})).fail(crl::guard(call.get(), [=](
				const MTP::Error &error) mutable {
			decided.status = CardStatus::Failed;
			call->finishCard(decided);
		})).send();
	});
}

} // namespace

namespace FolderTools {

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto folderIdProperty = IntProperty(
		1,
		std::numeric_limits<int>::max(),
		u"Folder id from list_folders."_q);
	auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	push(ToolEntry{
		.name = u"list_folders"_q,
		.description = u"List chat folders with their ids, titles and chat"
			" counts. In a chat scope also reports whether this chat is in"
			" each folder."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = true,
			.chat = true,
		},
		.group = u"folders"_q,
		.untrusted = false,
		.run = RunListFolders,
	});
	push(ToolEntry{
		.name = u"create_folder"_q,
		.description = u"Create a new chat folder with the given title and"
			" chats."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("title", StringProperty(
					u"Folder title, up to 12 characters."_q));
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"title"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = false,
		},
		.chatParam = ChatParam::Many,
		.group = u"folders"_q,
		.action = true,
		.run = RunCreateFolder,
	});
	push(ToolEntry{
		.name = u"rename_folder"_q,
		.description = u"Rename a chat folder by its id."_q,
		.inputSchema = ToolSchema(
			[&] {
				auto properties = QJsonObject();
				properties.insert("folder_id", folderIdProperty);
				properties.insert("title", StringProperty(
					u"New folder title, up to 12 characters."_q));
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"folder_id"_q), QJsonValue(u"title"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = false,
		},
		.group = u"folders"_q,
		.action = true,
		.run = RunRenameFolder,
	});
	push(ToolEntry{
		.name = u"set_folder_chats"_q,
		.description = u"Include or exclude chats in a chat folder by its"
			" id. In a chat scope acts on the agent's own chat."_q,
		.inputSchema = ToolSchema(
			[&] {
				auto properties = QJsonObject();
				properties.insert("folder_id", folderIdProperty);
				properties.insert("included", BoolProperty(
					u"True to include the chats in the folder, false to"
					" exclude them."_q));
				return properties;
			}(),
			QJsonArray{
				QJsonValue(u"folder_id"_q),
				QJsonValue(u"included"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::Many,
		.group = u"folders"_q,
		.action = true,
		.run = RunSetFolderChats,
	});
	push(ToolEntry{
		.name = u"reorder_folders"_q,
		.description = u"Set the order of all chat folders by giving every"
			" folder id in the desired order."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				auto order = QJsonObject();
				order.insert("type", QJsonValue(u"array"_q));
				order.insert(
					"description",
					u"Every folder id in the desired order."_q);
				order.insert("items", IntProperty(
					1,
					std::numeric_limits<int>::max(),
					u"Folder id."_q));
				properties.insert("order", order);
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"order"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = false,
		},
		.group = u"folders"_q,
		.action = true,
		.run = RunReorderFolders,
	});
	push(ToolEntry{
		.name = u"delete_folder"_q,
		.description = u"Delete a chat folder by its id. The chats are not"
			" deleted, only the folder is removed."_q,
		.inputSchema = ToolSchema(
			[&] {
				auto properties = QJsonObject();
				properties.insert("folder_id", folderIdProperty);
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"folder_id"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = false,
		},
		.group = u"folders"_q,
		.run = RunDeleteFolder,
	});
	return result;
}

} // namespace FolderTools

} // namespace Agent
