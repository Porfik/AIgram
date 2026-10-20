/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_media.h"
#include "agent/core/agent_policy.h"
#include "agent/core/agent_types.h"
#include "base/weak_ptr.h"
#include "mtproto/core_types.h"
#include "mtproto/sender.h"

#include <rpl/lifetime.h>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

#include <optional>
#include <vector>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Agent {

enum class ChatParam : uchar {
	None,
	One,
	OptionalOne,
	Many,
};

enum class MediaNeed : uchar {
	None,
	Images,
	Audio,
};

struct PendingCall;

struct ToolEntry {
	QString name;
	QString description;
	QString generalDescription;
	QJsonObject inputSchema;
	ToolPolicy policy;
	ChatParam chatParam = ChatParam::None;
	QString group;
	bool untrusted = true;
	bool action = false;
	MediaNeed needs = MediaNeed::None;
	Fn<void(not_null<PendingCall*>)> run;
};

struct PendingCall final : base::has_weak_ptr {
	explicit PendingCall(not_null<Main::Session*> session);

	not_null<Main::Session*> session;
	const PolicyGate *gate = nullptr;
	MTP::Sender api;
	rpl::lifetime lifetime;
	QString toolUseId;
	PeerData *peer = nullptr;
	std::vector<not_null<PeerData*>> peers;
	QJsonObject input;
	Fn<void(ToolResultBlock)> result;
	Fn<void(QJsonObject)> done;
	Fn<void(QString)> fail;
	Fn<void(Card card, Fn<void(Card decided)> execute)> confirm;
	Fn<void(Card card)> finishCard;
	Fn<void(Fn<void(Fn<void(QString error)> finished)> undo)> undoable;
	Fn<void(QString group)> loadGroup;
	QString sessionId;
	MediaAbilities mainAbilities;
	std::optional<MediaAbilities> helperAbilities;
	Fn<void(QJsonObject json, std::vector<MediaBlock> media)> doneMedia;
	Fn<void(
		MediaBlock media,
		QString question,
		Fn<void(QString text, QString error)> done)> askMediaModel;
	Fn<std::optional<MediaBlock>(QString id)> readAttachment;
	Fn<void(QString text)> notify;
	mtpRequestId historiesRequestId = 0;

	void setRequestId(mtpRequestId id);
	void cancel();

};

[[nodiscard]] PeerData *ResolveChatRef(
	not_null<Main::Session*> session,
	const QJsonValue &value);

[[nodiscard]] QJsonObject ToolSchema(
	QJsonObject properties,
	QJsonArray required);
[[nodiscard]] QJsonObject StringProperty(QString description);
[[nodiscard]] QJsonObject BoolProperty(QString description);
[[nodiscard]] QJsonObject IntProperty(
	int minimum,
	int maximum,
	QString description);
[[nodiscard]] QJsonObject MessageIdProperty(QString description);
[[nodiscard]] QJsonObject MessageIdsProperty(QString description);

namespace ChatTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace ChatTools

namespace GeneralTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace GeneralTools

namespace ActionTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace ActionTools

namespace ChatStateTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace ChatStateTools

namespace FolderTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace FolderTools

namespace MemoryTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace MemoryTools

namespace MediaTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace MediaTools

namespace FileTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace FileTools

namespace TaskTools {
[[nodiscard]] std::vector<ToolEntry> Entries();
} // namespace TaskTools

} // namespace Agent
