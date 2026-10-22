/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"
#include "agent/tools/agent_tool_rights.h"

#include "apiwrap.h"
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_participant_status.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonArray>
#include <QtCore/QDateTime>

namespace Agent::AdminMemberTools {
namespace {

constexpr auto kMaxUsers = 50;
constexpr auto kMaxTitleLength = 16;
constexpr auto kForwardMessagesOnAdd = 100;
constexpr auto kPreviewLimit = 100;
constexpr auto kMaxMessageIds = 100;
constexpr auto kDefaultRequestsLimit = 50;

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

[[nodiscard]] bool HasJsonBool(
		const QJsonObject &object,
		const QString &key,
		QString &error,
		bool &out) {
	const auto value = object.value(key);
	if (value.isUndefined() || value.isNull()) {
		return false;
	}
	if (!value.isBool()) {
		error = u"%1 must be a boolean."_q.arg(key);
		out = false;
		return true;
	}
	out = value.toBool();
	return true;
}

[[nodiscard]] QJsonObject NamesProperty(QString description) {
	auto items = QJsonObject();
	items.insert("type", QJsonValue(u"string"_q));
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"array"_q));
	result.insert("description", std::move(description));
	result.insert("items", std::move(items));
	result.insert("minItems", QJsonValue(1));
	return result;
}

[[nodiscard]] QString Preview(not_null<HistoryItem*> item) {
	auto text = item->notificationText().text;
	text.replace('\n', ' ');
	return text.left(kPreviewLimit);
}

[[nodiscard]] std::vector<MsgId> ParseMessageIds(
		const QJsonObject &input,
		const QString &key,
		QString &error) {
	const auto value = input.value(key);
	if (!value.isArray()) {
		error = u"%1 must be an array of message ids."_q.arg(key);
		return {};
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > kMaxMessageIds) {
		error = u"%1 must contain 1 to %2 message ids."_q
			.arg(key)
			.arg(kMaxMessageIds);
		return {};
	}
	auto result = std::vector<MsgId>();
	for (const auto &item : list) {
		if (!item.isDouble() || item.toDouble() < 1) {
			error = u"%1 must contain message ids."_q.arg(key);
			return {};
		}
		const auto id = MsgId(qint64(item.toDouble()));
		if (!ranges::contains(result, id)) {
			result.push_back(id);
		}
	}
	return result;
}

// WHY: requestMessageData has no single ready callback for a set of ids,
// so count the loads down before reporting the messages as missing.
void WithMessages(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		std::vector<MsgId> ids,
		Fn<void(HistoryItemsList)> ready) {
	const auto session = call->session;
	const auto finish = [=] {
		auto items = HistoryItemsList();
		for (const auto id : ids) {
			const auto item = session->data().message(peer->id, id);
			if (!item) {
				call->fail(u"Message %1 was not found in %2. Read the"
					u" history to get valid message ids."_q
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

void RunDeleteChatMessages(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	auto error = QString();
	const auto ids = ParseMessageIds(call->input, "message_ids", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	WithMessages(call, peer, ids, [=](HistoryItemsList items) {
		auto card = Card{
			.kind = CardKind::Change,
			.text = tr::lng_agent_card_title_delete(tr::now),
			.detail = u"delete_chat_messages"_q,
		};
		card.rows.push_back({
			.label = tr::lng_agent_card_row_chat(tr::now),
			.value = PeerTitle(peer),
		});
		for (const auto &item : items) {
			if (!item->canDelete()) {
				call->fail(u"Message %1 can't be deleted: you need the"
					u" delete_messages admin right for messages of other"
					u" members."_q.arg(item->id.bare));
				return;
			}
			const auto sender = item->originalSender();
			card.targets.push_back(CardTarget{
				.peer = peer->id.value,
				.messageId = item->id.bare,
				.title = sender ? PeerTitle(sender) : PeerTitle(peer),
				.preview = Preview(item),
			});
		}
		call->confirm(std::move(card), [=](Card decided) {
			auto list = MessageIdsList();
			for (auto &target : decided.targets) {
				if (!target.checked) {
					continue;
				}
				if (const auto item = call->session->data().message(
						PeerId(target.peer),
						MsgId(target.messageId))) {
					list.push_back(item->fullId());
				} else {
					target.error = u"The message was already deleted."_q;
				}
			}
			if (!list.empty()) {
				call->session->data().histories().deleteMessages(
					list,
					true);
				call->session->data().sendHistoryChangeNotifications();
			}
			decided.status = TargetsStatus(decided);
			call->finishCard(std::move(decided));
		});
	});
}

void RunDeleteMemberHistory(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	const auto channel = peer->asChannel();
	if (!channel || !channel->isMegagroup()) {
		call->fail(u"Deleting a member's message history works only in"
			u" supergroups."_q);
		return;
	} else if (!channel->canDeleteMessages()) {
		call->fail(u"You are missing the admin right delete_messages"
			u" needed for deleting messages."_q);
		return;
	}
	auto error = QString();
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = tr::lng_agent_card_title_delete_member_history(tr::now),
		.detail = u"delete_member_history"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			call->session->api().deleteAllFromParticipant(
				channel,
				user);
			done(QString());
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

void RunListJoinRequests(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	auto error = QString();
	const auto limitValue = JsonInt(call->input, "limit", error);
	const auto query = call->input.value("query").toString().trimmed();
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto limit = limitValue.value_or(kDefaultRequestsLimit);
	if (limit < 1 || limit > 100) {
		call->fail(u"limit must be between 1 and 100."_q);
		return;
	}
	using Flag = MTPmessages_GetChatInviteImporters::Flag;
	const auto flags = Flag::f_requested
		| (query.isEmpty() ? Flag(0) : Flag::f_q);
	call->api.request(MTPmessages_GetChatInviteImporters(
		MTP_flags(flags),
		peer->input(),
		MTPstring(),
		MTP_string(query),
		MTP_int(0),
		MTP_inputUserEmpty(),
		MTP_int(limit)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		const auto &data = result.data();
		call->session->data().processUsers(data.vusers());
		auto requests = QJsonArray();
		for (const auto &importer : data.vimporters().v) {
			importer.match([&](const MTPDchatInviteImporter &d) {
				const auto user = call->session->data().userLoaded(
					d.vuser_id());
				if (!user) {
					return;
				}
				auto item = QJsonObject();
				item.insert("id", QJsonValue(qint64(user->id.value)));
				item.insert("name", user->name());
				const auto username = user->username();
				if (!username.isEmpty()) {
					item.insert("username", username);
				}
				item.insert("date", base::unixtime::parse(
					d.vdate().v).toString(u"yyyy-MM-dd HH:mm"_q));
				const auto about = qs(d.vabout().value_or_empty());
				if (!about.isEmpty()) {
					item.insert("about", about);
				}
				requests.push_back(std::move(item));
			});
		}
		auto json = QJsonObject();
		json.insert("requests", requests);
		json.insert("total", QJsonValue(data.vcount().v));
		call->done(std::move(json));
	}).fail([=](const MTP::Error &error) {
		call->fail(MtpErrorText(error));
	}).send();
}

void RunAnswerJoinRequests(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	auto error = QString();
	auto approve = false;
	const auto hasApprove = HasJsonBool(
		call->input,
		"approve",
		error,
		approve);
	if (!error.isEmpty() || !hasApprove) {
		call->fail(error.isEmpty()
			? u"approve is required, true or false."_q
			: error);
		return;
	}
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = approve
			? tr::lng_agent_card_title_approve_requests(tr::now)
			: tr::lng_agent_card_title_decline_requests(tr::now),
		.detail = u"answer_join_requests"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			using Flag = MTPmessages_HideChatJoinRequest::Flag;
			call->api.request(MTPmessages_HideChatJoinRequest(
				MTP_flags(approve ? Flag::f_approved : Flag(0)),
				peer->input(),
				user->inputUser()
			)).done([=](const MTPUpdates &result) {
				call->session->api().applyUpdates(result);
				done(QString());
			}).fail([=](const MTP::Error &error) {
				done(MtpErrorText(error));
			}).send();
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

[[nodiscard]] QString UntilText(TimeId until) {
	return ChannelData::IsRestrictedForever(until)
		? tr::lng_agent_card_value_forever(tr::now)
		: langDateTime(base::unixtime::parse(until));
}

[[nodiscard]] std::optional<TimeId> ParseUntil(
		const QJsonObject &input,
		QString &error) {
	const auto value = input.value("until");
	if (value.isUndefined() || value.isNull()) {
		return std::nullopt;
	}
	return ParseLocalTime(value, error);
}

[[nodiscard]] ChatRestrictionsInfo KickRights(
		not_null<UserData*> user,
		TimeId until) {
	return ChatRestrictionsInfo(
		ChannelData::KickedRestrictedRights(user).flags,
		until);
}

[[nodiscard]] ChatRestrictionsInfo NoRestrictions() {
	return ChatRestrictionsInfo();
}

[[nodiscard]] QString InviteForbiddenText(
		const MTPmessages_InvitedUsers &result,
		not_null<UserData*> user) {
	const auto &data = result.data();
	for (const auto &missing : data.vmissing_invitees().v) {
		if (missing.data().vuser_id().v == user->id.value) {
			return u"The user's privacy settings don't allow adding them"
				u" to this chat (USER_PRIVACY_RESTRICTED)."_q;
		}
	}
	return QString();
}

void InviteUserToPeer(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		Fn<void(QString error)> done) {
	if (const auto chat = peer->asChat()) {
		call->api.request(MTPmessages_AddChatUser(
			chat->inputChat(),
			user->inputUser(),
			MTP_int(kForwardMessagesOnAdd)
		)).done([=](const MTPmessages_InvitedUsers &result) {
			call->session->api().applyUpdates(result.data().vupdates());
			done(InviteForbiddenText(result, user));
		}).fail([=](const MTP::Error &error) {
			done(MtpErrorText(error));
		}).send();
	} else if (const auto channel = peer->asChannel()) {
		call->api.request(MTPchannels_InviteToChannel(
			channel->inputChannel(),
			MTP_vector<MTPInputUser>(QVector<MTPInputUser>(
				1,
				user->inputUser()))
		)).done([=](const MTPmessages_InvitedUsers &result) {
			call->session->api().applyUpdates(result.data().vupdates());
			done(InviteForbiddenText(result, user));
		}).fail([=](const MTP::Error &error) {
			done(MtpErrorText(error));
		}).send();
	}
}

void RunAddMembers(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	const auto invite = peer->asChat()
		? peer->asChat()->canAddMembers()
		: peer->asChannel()->canAddMembers();
	if (!invite) {
		call->fail(u"You are missing the admin right invite_users needed"
			u" for adding members."_q);
		return;
	}
	auto error = QString();
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = tr::lng_agent_card_title_add_members(tr::now),
		.detail = u"add_members"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			InviteUserToPeer(call, peer, user, done);
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

void KickFromPeer(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		bool ban,
		TimeId until,
		Fn<void(QString error)> done) {
	if (const auto chat = peer->asChat()) {
		call->api.request(MTPmessages_DeleteChatUser(
			MTP_flags(0),
			chat->inputChat(),
			user->inputUser()
		)).done([=](const MTPUpdates &result) {
			call->session->api().applyUpdates(result);
			done(QString());
		}).fail([=](const MTP::Error &error) {
			done(MtpErrorText(error));
		}).send();
		return;
	}
	const auto channel = peer->asChannel();
	const auto editBanned = [=](
			ChatRestrictionsInfo rights,
			Fn<void(QString error)> next) {
		call->api.request(MTPchannels_EditBanned(
			channel->inputChannel(),
			user->input(),
			RestrictionsToMTP(rights)
		)).done([=](const MTPUpdates &result) {
			call->session->api().applyUpdates(result);
			channel->applyEditBanned(user, NoRestrictions(), rights);
			next(QString());
		}).fail([=](const MTP::Error &error) {
			next(MtpErrorText(error));
		}).send();
	};
	editBanned(
		KickRights(
			user,
			ban ? until : ChannelData::kRestrictUntilForever),
		[=](
			QString error) {
		if (!error.isEmpty() || ban) {
			done(error);
			return;
		}
		editBanned(NoRestrictions(), done);
	});
}

void RunRemoveMembers(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	auto error = QString();
	auto ban = false;
	const auto hasBan = HasJsonBool(call->input, "ban", error, ban);
	auto until = std::optional<TimeId>();
	if (error.isEmpty()) {
		until = ParseUntil(call->input, error);
	}
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (until.has_value() && (!hasBan || !ban)) {
		call->fail(u"until is used only with ban = true."_q);
		return;
	} else if (ban && peer->asChat()) {
		call->fail(u"Banning works only in supergroups and channels;"
			u" without ban the users are removed from the basic"
			u" group."_q);
		return;
	}
	const auto canBan = peer->asChat()
		? peer->asChat()->canBanMembers()
		: peer->asChannel()->canBanMembers();
	if (!canBan) {
		call->fail(u"You are missing the admin right ban_users needed"
			u" for removing members."_q);
		return;
	}
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = ban
			? tr::lng_agent_card_title_ban_members(tr::now)
			: tr::lng_agent_card_title_remove_members(tr::now),
		.detail = u"remove_members"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	if (ban) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_until(tr::now),
			.value = until.has_value()
				? UntilText(*until)
				: tr::lng_agent_card_value_forever(tr::now),
		});
	}
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			KickFromPeer(
				call,
				peer,
				user,
				ban,
				until.value_or(ChannelData::kRestrictUntilForever),
				done);
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

void RunUnbanMembers(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	const auto channel = peer->asChannel();
	if (!channel) {
		call->fail(u"Lifting bans and restrictions works only in"
			u" supergroups and channels, not in basic groups."_q);
		return;
	} else if (!channel->canBanMembers()) {
		call->fail(u"You are missing the admin right ban_users needed"
			u" for lifting bans."_q);
		return;
	}
	auto error = QString();
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = tr::lng_agent_card_title_unban_members(tr::now),
		.detail = u"unban_members"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			call->api.request(MTPchannels_EditBanned(
				channel->inputChannel(),
				user->input(),
				RestrictionsToMTP(NoRestrictions())
			)).done([=](const MTPUpdates &result) {
				call->session->api().applyUpdates(result);
				channel->applyEditBanned(
					user,
					NoRestrictions(),
					NoRestrictions());
				done(QString());
			}).fail([=](const MTP::Error &error) {
				done(MtpErrorText(error));
			}).send();
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

void RunRestrictMembers(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	const auto channel = peer->asChannel();
	if (!channel || !channel->isMegagroup()) {
		call->fail(u"Restricting members works only in supergroups."_q);
		return;
	} else if (!channel->canBanMembers()) {
		call->fail(u"You are missing the admin right ban_users needed"
			u" for restricting members."_q);
		return;
	}
	auto error = QString();
	const auto denied = ToolRights::ParseRestrictions(
		call->input.value("denied"),
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (!denied.has_value() || !*denied) {
		call->fail(u"denied is required: a non-empty array of"
			u" restriction names."_q);
		return;
	} else if (*denied & ChatRestriction::ViewMessages) {
		call->fail(u"view_messages can't be used in denied. To ban a"
			u" member use remove_members with ban instead."_q);
		return;
	}
	const auto until = ParseUntil(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto untilValue = until.value_or(
		ChannelData::kRestrictUntilForever);
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = tr::lng_agent_card_title_restrict_members(tr::now),
		.detail = u"restrict_members"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	card.rows.push_back({
		.label = tr::lng_agent_card_row_denied(tr::now),
		.value = ToolRights::RestrictionNames(*denied).join(u", "_q),
	});
	card.rows.push_back({
		.label = tr::lng_agent_card_row_until(tr::now),
		.value = UntilText(untilValue),
	});
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			call->api.request(MTPchannels_EditBanned(
				channel->inputChannel(),
				user->input(),
				RestrictionsToMTP(
					ChatRestrictionsInfo(*denied, untilValue))
			)).done([=](const MTPUpdates &result) {
				call->session->api().applyUpdates(result);
				channel->applyEditBanned(
					user,
					NoRestrictions(),
					ChatRestrictionsInfo(*denied, untilValue));
				done(QString());
			}).fail([=](const MTP::Error &error) {
				done(MtpErrorText(error));
			}).send();
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

[[nodiscard]] QJsonObject TargetResults(const Card &card) {
	auto done = QJsonArray();
	auto failed = QJsonArray();
	for (const auto &target : card.targets) {
		if (!target.checked) {
			continue;
		}
		auto item = QJsonObject();
		item.insert("id", QJsonValue(qint64(target.peer)));
		item.insert("name", target.title);
		if (!target.error.isEmpty()) {
			item.insert("error", target.error);
		}
		(target.error.isEmpty() ? done : failed)
			.push_back(std::move(item));
	}
	auto json = QJsonObject();
	json.insert("done", done);
	json.insert("failed", failed);
	return json;
}

void EditPeerAdmin(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		ChatAdminRights newRights,
		const QString &rank,
		bool withRankFlag,
		Fn<void(QString error)> done) {
	if (const auto chat = peer->asChat()) {
		call->api.request(MTPmessages_EditChatAdmin(
			chat->inputChat(),
			user->inputUser(),
			MTP_bool(newRights != ChatAdminRights())
		)).done([=](const MTPBool &result) {
			chat->applyEditAdmin(user, newRights != ChatAdminRights());
			done(QString());
		}).fail([=](const MTP::Error &error) {
			done(MtpErrorText(error));
		}).send();
		return;
	}
	const auto channel = peer->asChannel();
	using Flag = MTPchannels_EditAdmin::Flag;
	const auto flags = withRankFlag ? Flag::f_rank : Flag(0);
	call->api.request(MTPchannels_EditAdmin(
		MTP_flags(flags),
		channel->inputChannel(),
		user->inputUser(),
		AdminRightsToMTP(ChatAdminRightsInfo(newRights)),
		withRankFlag ? MTP_string(rank) : MTPstring()
	)).done([=](const MTPUpdates &result) {
		call->session->api().applyUpdates(result);
		channel->applyEditAdmin(
			user,
			ChatAdminRightsInfo(),
			ChatAdminRightsInfo(newRights),
			rank);
		done(QString());
	}).fail([=](const MTP::Error &error) {
		done(MtpErrorText(error));
	}).send();
}

void RunPromoteDemoteAdmins(not_null<PendingCall*> call, bool promote) {
	const auto peer = call->peer;
	const auto channel = peer->asChannel();
	const auto canAdmin = channel
		? channel->canAddAdmins()
		: peer->asChat()->canAddAdmins();
	if (!canAdmin) {
		call->fail(u"You are missing the admin right add_admins needed"
			u" for this action."_q);
		return;
	}
	auto error = QString();
	auto rights = std::optional<ChatAdminRights>();
	if (promote) {
		rights = ToolRights::ParseAdminRights(
			call->input.value("rights"),
			error);
		if (!error.isEmpty()) {
			call->fail(error);
			return;
		} else if (!rights.has_value() || !*rights) {
			call->fail(u"rights is required: a non-empty array of"
				u" admin right names."_q);
			return;
		}
		rights = *rights | ChatAdminRight::Other;
	}
	const auto title = call->input.value("title").toString();
	if (title.size() > kMaxTitleLength) {
		call->fail(u"title must be at most %1 characters."_q
			.arg(kMaxTitleLength));
		return;
	}
	const auto users = ResolveUserRefs(
		call,
		call->input.value("users"),
		kMaxUsers,
		error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	auto card = Card{
		.kind = CardKind::Change,
		.text = promote
			? tr::lng_agent_card_title_promote_admins(tr::now)
			: tr::lng_agent_card_title_demote_admins(tr::now),
		.detail = promote
			? u"promote_admins"_q
			: u"demote_admins"_q,
	};
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	if (promote) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_rights(tr::now),
			.value = ToolRights::AdminRightNames(*rights).join(u", "_q),
		});
		card.rows.push_back({
			.label = tr::lng_agent_card_row_admin_title(tr::now),
			.value = title.isEmpty()
				? tr::lng_agent_card_value_none(tr::now)
				: title,
		});
	}
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, std::move(decided), [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded."_q);
				return;
			}
			EditPeerAdmin(
				call,
				peer,
				user,
				rights.value_or(ChatAdminRights()),
				title,
				promote && !title.isEmpty(),
				done);
		}, [=](Card finished) {
			const auto basic = (peer->asChannel() == nullptr);
			if (!basic || finished.status != CardStatus::Done) {
				call->finishCard(std::move(finished));
				return;
			}
			auto json = TargetResults(finished);
			json.insert("note", u"In basic groups admins have all"
				u" rights and there is no admin title."_q);
			call->done(std::move(json));
		});
	});
}

void RunPromoteAdmins(not_null<PendingCall*> call) {
	RunPromoteDemoteAdmins(call, true);
}

void RunDemoteAdmins(not_null<PendingCall*> call) {
	RunPromoteDemoteAdmins(call, false);
}

} // namespace

[[nodiscard]] ToolEntry ChangeEntry(
		const QString &name,
		const QString &description,
		QJsonObject schema,
		QJsonArray required,
		void (*run)(not_null<PendingCall*> call)) {
	return ToolEntry{
		.name = name,
		.description = description,
		.inputSchema = ToolSchema(std::move(schema), std::move(required)),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = [](not_null<PeerData*> peer) {
			return peer->isChat() || peer->isChannel();
		},
		.forPeerError = u"This tool works only in groups and channels."_q,
		.run = run,
	};
}

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto users = QJsonObject();
	users.insert("users", UsersProperty(kMaxUsers, u"The users to act on."_q));
	auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	auto add = users;
	push(ChangeEntry(
		u"add_members"_q,
		u"Add users to this group or channel, one request per user."
		u" The user confirms it on a card."_q,
		std::move(add),
		QJsonArray{ QJsonValue(u"users"_q) },
		RunAddMembers));
	auto remove = users;
	remove.insert("ban", BoolProperty(
		u"True to ban the users instead of just removing them"
		u" (default false)."_q));
	remove.insert("until", StringProperty(
		u"Local date and time YYYY-MM-DD HH:MM when the ban ends,"
		u" only with ban = true. Forever when not set."_q));
	push(ChangeEntry(
		u"remove_members"_q,
		u"Remove users from this group or channel; with ban = true they also"
		" can't come back (until a date when given). To only take some"
		" rights away use restrict_members. The user confirms it on a card."_q,
		std::move(remove),
		QJsonArray{ QJsonValue(u"users"_q) },
		RunRemoveMembers));
	auto unban = users;
	push(ChangeEntry(
		u"unban_members"_q,
		u"Lift a ban or restriction from users, so they can join and"
		u" write again. The user confirms it on a card."_q,
		std::move(unban),
		QJsonArray{ QJsonValue(u"users"_q) },
		RunUnbanMembers));
	auto restrict = users;
	restrict.insert("denied", NamesProperty(
		u"Which member rights to deny: a non-empty array of names"
		u" from: "_q + ToolRights::AllRestrictionNames().join(u", "_q)
			+ u" (without view_messages)."_q));
	restrict.insert("until", StringProperty(
		u"Local date and time YYYY-MM-DD HH:MM when the restriction"
		u" ends. Forever when not set."_q));
	push(ChangeEntry(
		u"restrict_members"_q,
		u"Restrict users in this supergroup: deny some of their rights"
		u" for a time. The user confirms it on a card."_q,
		std::move(restrict),
		QJsonArray{ QJsonValue(u"users"_q), QJsonValue(u"denied"_q) },
		RunRestrictMembers));
	auto promote = users;
	promote.insert("rights", NamesProperty(
		u"Which admin rights to grant: a non-empty array of names"
		u" from: "_q + ToolRights::AllAdminRightNames().join(u", "_q)
			+ u". Manage chat is always granted."_q));
	promote.insert("title", StringProperty(
		u"Optional admin rank, at most 16 characters. Basic groups"
		u" have no ranks."_q));
	push(ChangeEntry(
		u"promote_admins"_q,
		u"Make users admins of this group or channel with the given"
		u" rights and an optional title. The user confirms it on a"
		u" card."_q,
		std::move(promote),
		QJsonArray{ QJsonValue(u"users"_q), QJsonValue(u"rights"_q) },
		RunPromoteAdmins));
	auto demote = users;
	push(ChangeEntry(
		u"demote_admins"_q,
		u"Dismiss users from being admins of this group or channel."
		u" The user confirms it on a card."_q,
		std::move(demote),
		QJsonArray{ QJsonValue(u"users"_q) },
		RunDemoteAdmins));
	auto messages = QJsonObject();
	messages.insert("message_ids", MessageIdsProperty(
		u"Ids of the messages to delete, 1 to 100, of anyone in the"
		u" chat."_q));
	push(ChangeEntry(
		u"delete_chat_messages"_q,
		u"Delete messages of anyone in this group or channel, for everyone,"
		" as an admin. For the user's own messages use delete_messages. The"
		" user confirms it on a card."_q,
		std::move(messages),
		QJsonArray{ QJsonValue(u"message_ids"_q) },
		RunDeleteChatMessages));
	auto history = users;
	push(ChangeEntry(
		u"delete_member_history"_q,
		u"Delete every message a user has sent in this supergroup."
		u" The user confirms it on a card."_q,
		std::move(history),
		QJsonArray{ QJsonValue(u"users"_q) },
		RunDeleteMemberHistory));
	auto requests = QJsonObject();
	requests.insert("query", StringProperty(
		u"Search the requests by name or username."_q));
	requests.insert("limit", IntProperty(
		1,
		100,
		u"How many requests to return, 1..100, default 50."_q));
	result.push_back(ToolEntry{
		.name = u"list_join_requests"_q,
		.description = u"List join requests of this group or channel"
			u" waiting for admin approval."_q,
		.inputSchema = ToolSchema(std::move(requests), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = [](not_null<PeerData*> peer) {
			return peer->isChat() || peer->isChannel();
		},
		.forPeerError = u"This tool works only in groups and channels."_q,
		.run = RunListJoinRequests,
	});
	auto answer = QJsonObject();
	answer.insert("users", UsersProperty(kMaxUsers, u"The requesting users."_q));
	answer.insert("approve", BoolProperty(
		u"True to approve the requests, false to dismiss them."_q));
	push(ChangeEntry(
		u"answer_join_requests"_q,
		u"Approve or dismiss join requests of this group or channel,"
		u" one user at a time. The user confirms it on a card."_q,
		std::move(answer),
		QJsonArray{ QJsonValue(u"users"_q), QJsonValue(u"approve"_q) },
		RunAnswerJoinRequests));
	return result;
}

} // namespace Agent::AdminMemberTools
