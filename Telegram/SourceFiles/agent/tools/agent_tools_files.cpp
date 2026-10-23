/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_files.h"
#include "agent/agent_media_loader.h"
#include "agent/core/agent_media.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/random.h"
#include "core/mime_type.h"
#include "data/data_changes.h"
#include "data/data_chat_participant_status.h"
#include "data/data_drafts.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Agent::FileTools {
namespace {

constexpr auto kListLimit = 200;
const auto kFilesGroup = u"files"_q;

struct FileRef {
	QString ref;
	QString path;
	QString name;
	QString mimeType;
	int64 size = 0;
};

[[nodiscard]] QJsonObject ChatJson(not_null<PeerData*> peer) {
	auto result = QJsonObject();
	result.insert("chat", QJsonValue(qint64(peer->id.value)));
	result.insert("title", peer->name());
	return result;
}

[[nodiscard]] QString TempFolder(not_null<Main::Session*> session) {
	const auto result = session->local().tempDirectory()
		+ u"agent/%1/"_q.arg(base::RandomValue<uint64>());
	QDir().mkpath(result);
	return result;
}

[[nodiscard]] std::optional<std::vector<FileRef>> ResolveFiles(
		not_null<PendingCall*> call,
		QString &error) {
	const auto value = call->input.value("files");
	if (!value.isArray()
		|| value.toArray().isEmpty()
		|| value.toArray().size() > 10) {
		error = u"files must be 1 to 10 attachment ids (\"a1\") or agent"
			" folder file names."_q;
		return std::nullopt;
	}
	const auto session = call->session;
	auto temp = QString();
	auto result = std::vector<FileRef>();
	for (const auto &item : value.toArray()) {
		const auto ref = item.toString().trimmed();
		if (AttachmentIndex(ref) > 0) {
			const auto block = call->readAttachment(ref);
			if (!block) {
				error = u"There is no attachment %1 in this session."_q
					.arg(ref);
				return std::nullopt;
			}
			if (temp.isEmpty()) {
				temp = TempFolder(session);
			}
			const auto path = temp + ref + u'-' + QFileInfo(
				block->name.isEmpty() ? u"file"_q : block->name).fileName();
			auto file = QFile(path);
			if (!file.open(QIODevice::WriteOnly)
				|| file.write(block->bytes) != block->bytes.size()) {
				error = u"Could not prepare attachment %1."_q.arg(ref);
				return std::nullopt;
			}
			result.push_back({
				.ref = ref,
				.path = path,
				.name = block->name,
				.mimeType = block->mimeType,
				.size = block->size,
			});
			continue;
		}
		const auto path = ResolveAgentFile(session, ref);
		if (path.isEmpty()) {
			error = u"%1 is neither an attachment of this session nor a"
				" file in the agent folder. Call list_agent_files."_q
				.arg(ref.isEmpty() ? u"(empty)"_q : ref);
			return std::nullopt;
		}
		const auto info = QFileInfo(path);
		result.push_back({
			.ref = ref,
			.path = path,
			.name = info.fileName(),
			.mimeType = Core::MimeTypeForFile(info).name(),
			.size = info.size(),
		});
	}
	return result;
}

[[nodiscard]] QStringList Paths(const std::vector<FileRef> &files) {
	auto result = QStringList();
	for (const auto &file : files) {
		result.push_back(file.path);
	}
	return result;
}

void SetCaptionDraft(not_null<History*> history, const QString &caption) {
	const auto current = history->localDraft(MsgId(), PeerId());
	if (caption.isEmpty()
		|| (current && !current->textWithTags.text.trimmed().isEmpty())) {
		return;
	}
	history->setLocalDraft(std::make_unique<Data::Draft>(
		TextWithTags{ caption, TextWithTags::Tags() },
		FullReplyTo(),
		SuggestOptions(),
		MessageCursor{
			int(caption.size()),
			int(caption.size()),
			Ui::kQFixedMax,
		},
		Data::WebPageDraft()));
	history->clearLocalEditDraft(MsgId(), PeerId());
	history->session().changes().entryUpdated(
		history->threadFor(MsgId(), PeerId()),
		Data::EntryUpdate::Flag::LocalDraftSet);
}

void WithItems(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		std::vector<MsgId> ids,
		Fn<void(std::vector<not_null<HistoryItem*>>)> ready) {
	const auto session = call->session;
	const auto finish = [=] {
		auto items = std::vector<not_null<HistoryItem*>>();
		for (const auto id : ids) {
			const auto item = session->data().message(peer->id, id);
			if (!item) {
				call->fail(u"Message %1 was not found in %2. Read the"
					" history to get valid message ids."_q
					.arg(id.bare)
					.arg(peer->name()));
				return;
			}
			items.push_back(item);
		}
		ready(std::move(items));
	};
	auto missing = std::vector<MsgId>();
	for (const auto id : ids) {
		if (!session->data().message(peer->id, id)) {
			missing.push_back(id);
		}
	}
	if (missing.empty()) {
		finish();
		return;
	}
	const auto left = std::make_shared<int>(int(missing.size()));
	for (const auto id : missing) {
		session->api().requestMessageData(
			peer,
			id,
			crl::guard(call.get(), [=] {
				if (!--*left) {
					finish();
				}
			}));
	}
}

struct SaveResult {
	not_null<HistoryItem*> item;
	QString path;
	QString error;
};

[[nodiscard]] QString SaveName(
		not_null<HistoryItem*> item,
		const MessageMedia &media) {
	if (media.type == MessageMediaType::Photo) {
		return u"photo_%1.jpg"_q.arg(item->id.bare);
	}
	return media.name.isEmpty()
		? u"file_%1"_q.arg(item->id.bare)
		: media.name;
}

void SaveItems(
		not_null<PendingCall*> call,
		std::vector<not_null<HistoryItem*>> items,
		Fn<void(std::vector<SaveResult>)> done) {
	const auto session = call->session;
	const auto results = std::make_shared<std::vector<SaveResult>>();
	for (const auto &item : items) {
		const auto media = FindMessageMedia(item);
		if (!media) {
			results->push_back({
				item,
				QString(),
				u"The message has no photo or file."_q,
			});
			continue;
		}
		const auto path = UniqueAgentFilePath(session, SaveName(item, *media));
		auto reserve = QFile(path);
		if (!reserve.open(QIODevice::WriteOnly)) {
			results->push_back({ item, QString(), u"Could not create"
				" the file in the agent folder."_q });
			continue;
		}
		reserve.close();
		results->push_back({ item, path, QString() });
	}
	const auto left = std::make_shared<int>(0);
	for (const auto &result : *results) {
		if (result.error.isEmpty()) {
			++*left;
		}
	}
	if (!*left) {
		done(*results);
		return;
	}
	for (auto i = 0; i != int(results->size()); ++i) {
		const auto &result = (*results)[i];
		if (!result.error.isEmpty()) {
			continue;
		}
		call->lifetime.make_state<MediaSaver>(
			result.item,
			result.path,
			[=](QString error) {
				if (!error.isEmpty()) {
					(*results)[i].error = error;
					(*results)[i].path = QString();
				}
				if (!--*left) {
					done(*results);
				}
			});
	}
}

void SaveWithoutCard(
		not_null<PendingCall*> call,
		std::vector<not_null<HistoryItem*>> items) {
	SaveItems(call, std::move(items), [=](std::vector<SaveResult> results) {
		auto done = QJsonArray();
		auto failed = QJsonArray();
		auto paths = QStringList();
		for (const auto &result : results) {
			auto entry = ChatJson(result.item->history()->peer);
			entry.insert("message", QJsonValue(qint64(result.item->id.bare)));
			if (result.error.isEmpty()) {
				entry.insert("file", QFileInfo(result.path).fileName());
				entry.insert("path", result.path);
				done.push_back(entry);
				paths.push_back(result.path);
			} else {
				entry.insert("error", result.error);
				failed.push_back(entry);
			}
		}
		if (!paths.isEmpty()) {
			call->undoable([=](Fn<void(QString error)> finished) {
				for (const auto &path : paths) {
					QFile::remove(path);
				}
				finished(QString());
			});
		}
		auto json = QJsonObject();
		json.insert("done", done);
		json.insert("failed", failed);
		call->done(std::move(json));
	});
}

void RunSaveFiles(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	auto error = QString();
	const auto ids = ParseMessageIds(call->input, u"message_ids"_q, error);
	if (!peer) {
		call->fail(u"chat is required."_q);
		return;
	} else if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto session = call->session;
	WithItems(call, not_null(peer), ids, [=](
			std::vector<not_null<HistoryItem*>> items) {
		auto total = int64(0);
		auto card = Card{
			.kind = CardKind::Download,
			.detail = AgentFolderPath(session),
		};
		for (const auto &item : items) {
			const auto media = FindMessageMedia(item);
			const auto size = media ? media->size : int64(0);
			total += size;
			card.targets.push_back(CardTarget{
				.peer = peer->id.value,
				.messageId = item->id.bare,
				.title = peer->name(),
				.preview = media ? SaveName(item, *media) : QString(),
			});
			card.files.push_back(CardFile{
				.name = media ? SaveName(item, *media) : QString(),
				.mimeType = media ? media->mimeType : QString(),
				.size = size,
			});
		}
		if (total <= kAutoDownloadBytes) {
			SaveWithoutCard(call, std::move(items));
			return;
		}
		card.amount = total;
		call->confirm(std::move(card), [=](Card decided) {
			auto checked = std::vector<not_null<HistoryItem*>>();
			for (auto i = 0; i != int(items.size()); ++i) {
				if (decided.targets[i].checked) {
					checked.push_back(items[i]);
				}
			}
			SaveItems(call, checked, [=](std::vector<SaveResult> results) {
				auto card = decided;
				auto saved = false;
				for (const auto &result : results) {
					for (auto &target : card.targets) {
						if (target.messageId == result.item->id.bare) {
							target.error = result.error;
						}
					}
					saved = saved || result.error.isEmpty();
				}
				card.status = saved ? CardStatus::Done : CardStatus::Failed;
				call->finishCard(std::move(card));
			});
		});
	});
}

[[nodiscard]] QString SendToChat(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		const QStringList &paths,
		const QString &caption,
		bool uncompressed) {
	const auto history = session->data().history(peer);
	if (!Data::CanSendAnyOf(history, Data::FilesSendRestrictions())) {
		return u"You can't send files to this chat."_q;
	}
	auto list = Storage::PrepareMediaList(
		paths,
		st::sendMediaPreviewSize,
		session->user()->isPremium());
	if (list.error != Ui::PreparedList::Error::None || list.files.empty()) {
		return u"Could not prepare the files."_q;
	}
	auto way = Ui::SendFilesWay();
	way.setGroupFiles(true);
	way.setSendImagesAsPhotos(!uncompressed);
	auto groups = Ui::DivideByGroups(std::move(list), way, false);
	if (!caption.isEmpty() && !groups.empty()) {
		auto &group = groups.back();
		auto &files = group.list.files;
		if (!files.empty()) {
			auto &captioned = (group.type == Ui::AlbumType::PhotoVideo)
				? files.front()
				: files.back();
			captioned.caption = TextWithTags{ caption, TextWithTags::Tags() };
		}
	}
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = Api::SendAction(history);
	action.clearDraft = false;
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session->api().sendFiles(std::move(group.list), type, album, action);
	}
	return QString();
}

void RunSendFiles(not_null<PendingCall*> call) {
	auto error = QString();
	const auto files = ResolveFiles(call, error);
	if (!files) {
		call->fail(error);
		return;
	}
	const auto caption = call->input.value("caption").toString().trimmed();
	auto card = Card{
		.kind = CardKind::SendFiles,
		.text = caption,
		.proposedText = caption,
	};
	for (const auto &peer : call->peers) {
		card.targets.push_back(CardTarget{
			.peer = peer->id.value,
			.title = peer->name(),
		});
	}
	for (const auto &file : *files) {
		card.files.push_back(CardFile{
			.ref = file.ref,
			.name = file.name,
			.mimeType = file.mimeType,
			.size = file.size,
		});
		if (MediaKindForMime(file.mimeType) == MediaKind::Image) {
			card.optionAvailable = true;
		}
	}
	const auto session = call->session;
	const auto paths = Paths(*files);
	call->confirm(std::move(card), [=](Card decided) {
		auto sent = false;
		for (auto &target : decided.targets) {
			if (!target.checked) {
				continue;
			}
			const auto peer = session->data().peerLoaded(
				PeerId(target.peer));
			target.error = peer
				? SendToChat(
					session,
					not_null(peer),
					paths,
					decided.text.trimmed(),
					decided.option)
				: u"The chat is not loaded."_q;
			sent = sent || target.error.isEmpty();
		}
		decided.status = sent ? CardStatus::Done : CardStatus::Failed;
		call->finishCard(std::move(decided));
	});
}

void RunDraftFiles(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	if (!peer) {
		call->fail(u"This tool works only inside a chat."_q);
		return;
	}
	auto error = QString();
	const auto files = ResolveFiles(call, error);
	if (!files) {
		call->fail(error);
		return;
	}
	const auto session = call->session;
	const auto history = session->data().history(peer);
	if (!Data::CanSendAnyOf(history, Data::FilesSendRestrictions())) {
		call->fail(u"You can't send files to this chat."_q);
		return;
	}
	const auto window = session->tryResolveWindow(peer);
	if (!window || !window->content()) {
		call->fail(u"There is no open window to show the send window"
			" in."_q);
		return;
	}
	SetCaptionDraft(
		history,
		call->input.value("caption").toString().trimmed());
	if (!window->content()->sendPaths(history, Paths(*files))) {
		call->fail(u"Could not open the send window in this chat."_q);
		return;
	}
	auto json = ChatJson(peer);
	json.insert("opened", QJsonValue(true));
	call->done(std::move(json));
}

void RunListAgentFiles(not_null<PendingCall*> call) {
	const auto session = call->session;
	auto files = QJsonArray();
	for (const auto &file : ListAgentFolder(session)) {
		if (files.size() >= kListLimit) {
			break;
		}
		auto entry = QJsonObject();
		entry.insert("name", file.name);
		entry.insert("size", QJsonValue(qint64(file.size)));
		entry.insert(
			"modified",
			file.modified.toString(u"yyyy-MM-dd HH:mm"_q));
		files.push_back(entry);
	}
	auto json = QJsonObject();
	json.insert("folder", AgentFolderPath(session));
	json.insert("files", files);
	call->done(std::move(json));
}

[[nodiscard]] QJsonObject FilesProperty() {
	auto item = QJsonObject();
	item.insert("type", QJsonValue(u"string"_q));
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"array"_q));
	result.insert("items", item);
	result.insert("minItems", QJsonValue(1));
	result.insert("maxItems", QJsonValue(10));
	result.insert("description", QJsonValue(u"1 to 10 files: attachment"
		" ids of this session like \"a1\", or file names from"
		" list_agent_files."_q));
	return result;
}

} // namespace

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto saveProperties = QJsonObject();
	saveProperties.insert("message_ids", MessageIdsProperty(
		u"Ids of the messages whose photos or files to save."_q));
	auto sendProperties = QJsonObject();
	sendProperties.insert("files", FilesProperty());
	sendProperties.insert("caption", StringProperty(
		u"Optional caption for the files."_q));
	auto draftProperties = sendProperties;
	result.push_back(ToolEntry{
		.name = u"save_files"_q,
		.description = u"Save photos and files of messages to the agent"
			" folder on the user's computer; this is how you download files"
			" or save them to the PC. Give message ids of one chat. Above"
			" 20 MB in total the user confirms the download; saving can be"
			" undone. There is no tool to delete files."_q,
		.generalDescription = u"Save photos and files of messages in a"
			" chat to the agent folder on the user's disk. Above 20 MB in"
			" total the user confirms the download."_q,
		.inputSchema = ToolSchema(
			std::move(saveProperties),
			QJsonArray{ QJsonValue(u"message_ids"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Reversible },
		.chatParam = ChatParam::One,
		.group = kFilesGroup,
		.action = true,
		.run = RunSaveFiles,
	});
	result.push_back(ToolEntry{
		.name = u"send_files"_q,
		.description = u"Send files to chats: attachments of this session"
			" (a1, a2, ...) or agent folder files by name (see"
			" list_agent_files), with an optional caption. The user"
			" confirms it on a card. To save files of messages use"
			" save_files."_q,
		.generalDescription = u"Send files the user attached in this"
			" session or files from the agent folder to chats, with an"
			" optional caption. The user confirms in a card and can edit"
			" the caption."_q,
		.inputSchema = ToolSchema(
			std::move(sendProperties),
			QJsonArray{ QJsonValue(u"files"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = false,
			.outbound = true,
			.chatTask = true,
		},
		.chatParam = ChatParam::Many,
		.group = kFilesGroup,
		.run = RunSendFiles,
	});
	result.push_back(ToolEntry{
		.name = u"draft_files"_q,
		.description = u"Open the usual send window in this chat with"
			" attachments of this session or agent folder files and a"
			" caption; the user sends them."_q,
		.inputSchema = ToolSchema(
			std::move(draftProperties),
			QJsonArray{ QJsonValue(u"files"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = false,
			.chat = true,
			.background = false,
		},
		.group = kFilesGroup,
		.untrusted = false,
		.run = RunDraftFiles,
	});
	result.push_back(ToolEntry{
		.name = u"list_agent_files"_q,
		.description = u"List the files in the agent folder on the user's"
			" computer, newest first, with sizes and dates. There is no"
			" tool to delete them: the user deletes files by hand."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.group = kFilesGroup,
		.run = RunListAgentFiles,
	});
	return result;
}

} // namespace Agent::FileTools
