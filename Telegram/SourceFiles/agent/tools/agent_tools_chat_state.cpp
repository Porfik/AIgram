/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "apiwrap.h"
#include "menu/menu_mark_as_read.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonArray>

#include <limits>

namespace Agent {
namespace {

constexpr auto kMaxMuteHours = 24 * 365;

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
		QJsonArray done,
		QJsonArray failed) {
	auto json = QJsonObject();
	json.insert("done", done);
	json.insert("failed", failed);
	call->done(std::move(json));
}

[[nodiscard]] bool CanArchive(not_null<History*> history) {
	const auto peer = history->peer;
	if (history->useTopPromotion()) {
		return false;
	}
	if (const auto channel = peer->asChannel()) {
		if (channel->isCommunity()) {
			return false;
		}
	}
	if (const auto info = history->communityListInfo()) {
		if (info->collapsedInDialogs()) {
			return false;
		}
	}
	if (peer->isNotificationsUser() || peer->isSelf()) {
		if (!history->folderKnown() || !history->folder()) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool CanPin(not_null<History*> history) {
	if (history->useTopPromotion()) {
		return false;
	}
	if (const auto info = history->communityListInfo()) {
		if (info->collapsedInDialogs()) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] MTPInputDialogPeer TogglePinPeerInput(
		not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		if (channel->isCommunity()) {
			return MTP_inputDialogPeerCommunity(channel->inputChannel());
		}
	}
	return MTP_inputDialogPeer(peer->input());
}

[[nodiscard]] MTPmessages_ToggleDialogPin ToggleDialogPinRequest(
		not_null<PeerData*> peer,
		bool pinned) {
	const auto flags = pinned
		? MTPmessages_ToggleDialogPin::Flag::f_pinned
		: MTPmessages_ToggleDialogPin::Flag(0);
	return MTPmessages_ToggleDialogPin(
		MTP_flags(flags),
		TogglePinPeerInput(peer));
}

struct MuteRestore {
	PeerKey peer = 0;
	std::optional<TimeId> muteUntil;
};

void RunMarkAsRead(not_null<PendingCall*> call) {
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_mark_read(tr::now);
	card.detail = u"mark_read"_q;
	for (const auto &peer : call->peers) {
		card.targets.push_back(PeerTarget(peer));
	}
	call->confirm(std::move(card), [=](Card decided) {
		auto done = QJsonArray();
		auto failed = QJsonArray();
		for (const auto &target : decided.targets) {
			if (!target.checked) {
				continue;
			}
			const auto peer = call->session->data().peerLoaded(
				PeerId(target.peer));
			if (!peer) {
				continue;
			}
			const auto history = call->session->data().history(peer);
			if (MarkAsReadMenu::IsUnreadThread(history)) {
				MarkAsReadMenu::MarkAsReadThread(history);
			}
			AddChatResult(done, peer);
		}
		DeliverChatResults(call, std::move(done), std::move(failed));
	});
}

void RunSetMute(not_null<PendingCall*> call) {
	auto error = QString();
	const auto muted = JsonBool(call->input, "muted", error);
	const auto forHours = JsonInt(call->input, "for_hours", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	if (!muted.has_value()) {
		call->fail(u"muted is required, true or false."_q);
		return;
	}
	if (forHours.has_value() && !*muted) {
		call->fail(u"for_hours is used only with muted = true."_q);
		return;
	}
	if (forHours.has_value()
		&& (*forHours < 1 || *forHours > kMaxMuteHours)) {
		call->fail(
			u"for_hours must be between 1 and %1."_q.arg(kMaxMuteHours));
		return;
	}
	const auto mute = *muted
		? Data::MuteValue{
			.unmute = false,
			.forever = !forHours.has_value(),
			.period = forHours.value_or(0) * 3600,
		}
		: Data::MuteValue{ .unmute = true };
	const auto settings = &call->session->data().notifySettings();
	auto done = QJsonArray();
	auto failed = QJsonArray();
	auto restore = std::vector<MuteRestore>();
	for (const auto &peer : call->peers) {
		if (settings->muteUnknown(peer)) {
			settings->request(peer);
		} else if (settings->isMuted(peer) == *muted) {
			AddChatResult(done, peer);
			continue;
		}
		restore.push_back({ peer->id.value, peer->notify().muteUntil() });
		settings->update(peer, mute);
		AddChatResult(done, peer);
	}
	if (!restore.empty()) {
		const auto session = call->session;
		call->undoable([=](Fn<void(QString error)> finished) {
			for (const auto &state : restore) {
				const auto peer = session->data().peerLoaded(
					PeerId(state.peer));
				if (!peer) {
					continue;
				}
				const auto until = state.muteUntil;
				const auto back = (!until || *until <= base::unixtime::now())
					? Data::MuteValue{ .unmute = true }
					: (*until >= std::numeric_limits<int>::max())
					? Data::MuteValue{ .forever = true }
					: Data::MuteValue{
						.period = int(*until - base::unixtime::now()),
					};
				session->data().notifySettings().update(peer, back);
			}
			finished({});
		});
	}
	DeliverChatResults(call, std::move(done), std::move(failed));
}

void RunSetArchived(not_null<PendingCall*> call) {
	auto error = QString();
	const auto archived = JsonBool(call->input, "archived", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	if (!archived.has_value()) {
		call->fail(u"archived is required, true or false."_q);
		return;
	}
	auto done = QJsonArray();
	auto failed = QJsonArray();
	auto restore = std::vector<std::pair<PeerKey, bool>>();
	for (const auto &peer : call->peers) {
		const auto history = call->session->data().history(peer);
		if (!CanArchive(history)) {
			AddChatResult(failed, peer, u"This chat can't be archived."_q);
			continue;
		}
		const auto was = (history->folder() != nullptr);
		if (was == *archived) {
			AddChatResult(done, peer);
			continue;
		}
		restore.emplace_back(peer->id.value, was);
		call->session->api().toggleHistoryArchived(history, *archived, {});
		AddChatResult(done, peer);
	}
	if (!restore.empty()) {
		const auto session = call->session;
		call->undoable([=](Fn<void(QString error)> finished) {
			for (const auto &[peerId, was] : restore) {
				const auto peer = session->data().peerLoaded(
					PeerId(peerId));
				if (!peer) {
					continue;
				}
				session->api().toggleHistoryArchived(
					session->data().history(peer),
					was,
					{});
			}
			finished({});
		});
	}
	DeliverChatResults(call, std::move(done), std::move(failed));
}

void RunSetPinned(not_null<PendingCall*> call) {
	auto error = QString();
	const auto pinned = JsonBool(call->input, "pinned", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	if (!pinned.has_value()) {
		call->fail(u"pinned is required, true or false."_q);
		return;
	}
	auto done = QJsonArray();
	auto failed = QJsonArray();
	auto restore = std::vector<std::pair<PeerKey, bool>>();
	for (const auto &peer : call->peers) {
		const auto history = call->session->data().history(peer);
		const auto owner = &call->session->data();
		if (!history->folderKnown()) {
			AddChatResult(
				failed,
				peer,
				u"This chat is not loaded yet, try again later."_q);
			continue;
		} else if (!CanPin(history)) {
			AddChatResult(failed, peer, u"This chat can't be pinned."_q);
			continue;
		}
		const auto wasPinned = history->isPinnedDialog(FilterId());
		if (wasPinned == *pinned) {
			AddChatResult(done, peer);
			continue;
		}
		if (*pinned && !owner->pinnedCanPin(history)) {
			const auto limit = Data::PremiumLimits(
				call->session
			).dialogsPinnedCurrent();
			AddChatResult(
				failed,
				peer,
				u"The pinned chats limit is reached (%1)."_q.arg(limit));
			continue;
		}
		restore.emplace_back(peer->id.value, wasPinned);
		owner->setChatPinned(history, FilterId(), *pinned);
		const auto session = call->session;
		session->api().request(
			ToggleDialogPinRequest(peer, *pinned)
		).done([=] {
			session->data().notifyPinnedDialogsOrderUpdated();
		}).send();
		AddChatResult(done, peer);
	}
	if (!restore.empty()) {
		const auto session = call->session;
		call->undoable([=](Fn<void(QString error)> finished) {
			for (const auto &[peerId, was] : restore) {
				const auto peer = session->data().peerLoaded(
					PeerId(peerId));
				if (!peer) {
					continue;
				}
				const auto history = session->data().history(peer);
				const auto owner = &session->data();
				if (history->isPinnedDialog(FilterId()) != was) {
					owner->setChatPinned(history, FilterId(), was);
					session->api().request(
						ToggleDialogPinRequest(peer, was)
					).send();
				}
			}
			finished({});
		});
	}
	DeliverChatResults(call, std::move(done), std::move(failed));
}

} // namespace

namespace ChatStateTools {

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto push = [&](
			const QString &name,
			const QString &description,
			QJsonObject schema,
			auto run) {
		result.push_back(ToolEntry{
			.name = name,
			.description = description,
			.inputSchema = std::move(schema),
			.policy = ToolPolicy{
				.risk = Risk::Reversible,
				.general = true,
				.chat = true,
			},
			.chatParam = ChatParam::Many,
			.action = true,
			.run = run,
		});
	};
	result.push_back(ToolEntry{
		.name = u"mark_read"_q,
		.description = u"Mark chats as read. The senders see read"
			" receipts, so the user confirms it on a card."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::Many,
		.run = RunMarkAsRead,
	});
	auto muteProperties = QJsonObject();
	muteProperties.insert(
		"muted",
		BoolProperty(u"True to mute the chats, false to unmute them."_q));
	muteProperties.insert("for_hours", IntProperty(
		1,
		kMaxMuteHours,
		u"Mute for this many hours instead of forever, only with"
		" muted = true."_q));
	push(
		u"set_mute"_q,
		u"Mute or unmute notifications for chats. Pass muted = true"
		" and optionally for_hours, or muted = false to unmute."_q,
		ToolSchema(
			std::move(muteProperties),
			QJsonArray{ QJsonValue(u"muted"_q) }),
		RunSetMute);
	auto archivedProperties = QJsonObject();
	archivedProperties.insert("archived", BoolProperty(
		u"True to move the chats to the archive, false to unarchive"
		" them."_q));
	push(
		u"set_archived"_q,
		u"Move chats to the archive or back to the main chat list."_q,
		ToolSchema(
			std::move(archivedProperties),
			QJsonArray{ QJsonValue(u"archived"_q) }),
		RunSetArchived);
	auto pinnedProperties = QJsonObject();
	pinnedProperties.insert("pinned", BoolProperty(
		u"True to pin the chats at the top of the chat list, false to"
		" unpin them."_q));
	push(
		u"set_pinned"_q,
		u"Pin chats at the top of the chat list or unpin them."_q,
		ToolSchema(
			std::move(pinnedProperties),
			QJsonArray{ QJsonValue(u"pinned"_q) }),
		RunSetPinned);
	return result;
}

} // namespace ChatStateTools

} // namespace Agent
