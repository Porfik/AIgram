/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tools_chat.h"

#include "agent/tools/agent_tool_format.h"
#include "agent/tools/agent_tool_rights.h"
#include "agent/tools/agent_tools.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "base/algorithm.h"
#include "base/timer.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_participant_status.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>

#include <limits>

#include <rpl/rpl.h>

namespace Agent {
namespace {

constexpr auto kDefaultHistoryLimit = 30;
constexpr auto kDefaultSearchLimit = 20;
constexpr auto kDefaultMembersLimit = 50;

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

[[nodiscard]] QString PeerRole(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (chat->amCreator()) {
			return u"owner"_q;
		}
		if (chat->hasAdminRights()) {
			return u"admin"_q;
		}
		return chat->amIn() ? u"member"_q : u"left"_q;
	} else if (const auto channel = peer->asChannel()) {
		if (channel->amCreator()) {
			return u"owner"_q;
		}
		if (channel->hasAdminRights()) {
			return u"admin"_q;
		}
		return channel->amIn()
			? (channel->isBroadcast() ? u"subscriber"_q : u"member"_q)
			: u"left"_q;
	}
	return QString();
}

[[nodiscard]] int PeerMembersCount(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return channel->membersCountKnown() ? channel->membersCount() : 0;
	} else if (const auto chat = peer->asChat()) {
		return std::max(chat->count, 0);
	}
	return peer->isSelf() ? 1 : 2;
}

[[nodiscard]] QStringList PeerAdminRights(not_null<PeerData*> peer) {
	auto result = QStringList();
	if (const auto channel = peer->asChannel()) {
		result = ToolRights::AdminRightNames(channel->adminRights());
	} else if (const auto chat = peer->asChat()) {
		result = ToolRights::AdminRightNames(chat->adminRights());
	}
	return result;
}

[[nodiscard]] PeerData *PeerLinkedChat(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return channel->discussionLinkKnown() ? channel->discussionLink() : nullptr;
	}
	return nullptr;
}

[[nodiscard]] QJsonObject SerializeChatBrief(const ChatBrief &brief) {
	auto chat = QJsonObject();
	chat.insert("id", QJsonValue(qint64(brief.peer)));
	chat.insert("kind", brief.kind);
	chat.insert("title", brief.title);
	if (!brief.username.isEmpty()) {
		chat.insert("username", brief.username);
	}
	if (brief.members > 0) {
		chat.insert("members", QJsonValue(brief.members));
	}
	if (!brief.role.isEmpty()) {
		chat.insert("role", brief.role);
	}
	chat.insert("unread", QJsonValue(brief.unread));
	chat.insert("canWrite", QJsonValue(brief.canWrite));
	return chat;
}

[[nodiscard]] QJsonObject MemberJson(
		not_null<UserData*> user,
		const QString &role) {
	auto result = QJsonObject();
	result.insert("id", QJsonValue(qint64(user->id.value)));
	result.insert("name", user->name());
	result.insert("role", role);
	const auto username = user->username();
	if (!username.isEmpty()) {
		result.insert("username", username);
	}
	if (user->isBot()) {
		result.insert("bot", QJsonValue(true));
	}
	return result;
}

[[nodiscard]] QJsonObject BuildPeerInfoJson(
		not_null<PeerData*> peer,
		not_null<const PolicyGate*> gate) {
	auto result = QJsonObject();
	result.insert("id", QJsonValue(qint64(peer->id.value)));
	result.insert("kind", ToolFormat::PeerKind(peer));
	result.insert("title", peer->name());
	const auto username = ToolFormat::PeerUsername(peer);
	if (!username.isEmpty()) {
		result.insert("username", username);
	}
	if (!peer->about().isEmpty()) {
		result.insert("description", peer->about());
	}
	const auto members = PeerMembersCount(peer);
	if (members > 0) {
		result.insert("members", QJsonValue(members));
	}
	const auto role = PeerRole(peer);
	if (!role.isEmpty()) {
		result.insert("role", role);
	}
	const auto rights = PeerAdminRights(peer);
	if (!rights.isEmpty()) {
		auto list = QJsonArray();
		for (const auto &right : rights) {
			list.push_back(right);
		}
		result.insert("admin_rights", list);
	}
	if (const auto channel = peer->asChannel()) {
		if (const auto seconds = channel->slowmodeSeconds(); seconds > 0) {
			result.insert("slow_mode", QJsonValue(seconds));
		}
	}
	if (const auto linked = PeerLinkedChat(peer);
			linked && gate->visible(linked->id.value)) {
		auto json = QJsonObject();
		json.insert("id", QJsonValue(qint64(linked->id.value)));
		json.insert("title", linked->name());
		result.insert("linked_chat", json);
	}
	if (const auto pinned = Data::ResolveTopPinnedId(peer, MsgId(0), PeerId(0))) {
		result.insert("pinned_message_id", QJsonValue(qint64(pinned.msg.bare)));
	}
	if (const auto user = peer->asUser()) {
		const auto phone = user->phone();
		if (!phone.isEmpty()) {
			result.insert("phone", phone);
		}
	}
	return result;
}

struct ParsedSlice {
	std::vector<not_null<HistoryItem*>> items;
	int total = -1;
	int nextRate = 0;
};

[[nodiscard]] ParsedSlice ParseMessagesSlice(
		not_null<Main::Session*> session,
		const MTPmessages_Messages &result) {
	auto parsed = ParsedSlice();
	const auto append = [&](const QVector<MTPMessage> &list) {
		for (const auto &message : list) {
			const auto peerId = PeerFromMessage(message);
			const auto peer = session->data().peerLoaded(peerId);
			if (!peer) {
				continue;
			}
			if (const auto item = session->data().addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing)) {
				parsed.items.push_back(item);
			}
		}
	};
	result.match(
		[&](const MTPDmessages_messagesNotModified &) {},
		[&](const MTPDmessages_messages &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			append(data.vmessages().v);
		},
		[&](const MTPDmessages_messagesSlice &data) {
			parsed.total = data.vcount().v;
			parsed.nextRate = data.vnext_rate().value_or_empty();
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			append(data.vmessages().v);
		},
		[&](const MTPDmessages_channelMessages &data) {
			parsed.total = data.vcount().v;
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			append(data.vmessages().v);
		});
	return parsed;
}

enum class Page : uchar {
	Newest,
	Around,
	Before,
	After,
};

void DeliverMessagesPage(
		not_null<PendingCall*> call,
		ParsedSlice parsed,
		int limit,
		Page page,
		bool withNextOffset) {
	auto messages = QJsonArray();
	const auto &items = parsed.items;
	for (auto i = items.rbegin(); i != items.rend(); ++i) {
		messages.push_back(ToolFormat::SerializeMessage(*i));
	}
	const auto full = int(items.size()) == limit;
	auto hasOlder = false;
	auto hasNewer = false;
	switch (page) {
	case Page::Newest:
		hasOlder = full;
		break;
	case Page::Around:
		hasOlder = hasNewer = full;
		break;
	case Page::Before:
		hasNewer = true;
		hasOlder = full;
		break;
	case Page::After:
		hasOlder = true;
		hasNewer = full;
		break;
	}
	auto json = QJsonObject();
	json.insert("messages", messages);
	json.insert("has_older", QJsonValue(hasOlder));
	json.insert("has_newer", QJsonValue(hasNewer));
	if (withNextOffset && !items.empty() && full) {
		json.insert(
			"next_offset_id",
			QJsonValue(qint64(items.back()->id.bare)));
	}
	call->done(std::move(json));
}

void OnFullPeer(not_null<PendingCall*> call, Fn<void()> ready) {
	const auto peer = call->peer;
	if (peer->isFullLoaded()) {
		ready();
		return;
	}
	const auto once = call->lifetime.make_state<bool>(false);
	const auto trigger = [call, once, ready = std::move(ready)] {
		if (*once) {
			return;
		}
		*once = true;
		ready();
	};
	peer->session().api().requestFullPeer(peer);
	peer->session().changes().peerUpdates(
		peer,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::on_next([trigger](const Data::PeerUpdate &) {
		trigger();
	}, call->lifetime);
	call->lifetime.make_state<base::Timer>(trigger)->callOnce(5 * 1000);
}

void DeliverGlobalSearch(
		not_null<PendingCall*> call,
		const QString &query,
		int minDate,
		int maxDate,
		const QString &cursor,
		int limit) {
	auto offsetRate = 0;
	auto offsetPeer = (PeerData*)nullptr;
	auto offsetId = 0;
	if (!cursor.isEmpty()) {
		const auto parts = cursor.split('_');
		if (parts.size() != 3) {
			call->fail(u"Malformed search offset."_q);
			return;
		}
		auto okRate = false;
		auto okPeer = false;
		auto okId = false;
		offsetRate = parts[0].toInt(&okRate);
		const auto peerId = parts[1].toULongLong(&okPeer);
		offsetId = parts[2].toInt(&okId);
		if (!okRate || !okPeer || !okId) {
			call->fail(u"Malformed search offset."_q);
			return;
		}
		offsetPeer = call->session->data().peerLoaded(PeerId(peerId));
		if (!offsetPeer) {
			call->fail(u"Unknown chat in the search offset."_q);
			return;
		}
	}
	const auto finish = [call, limit](const MTPmessages_Messages &result) {
		const auto parsed = ParseMessagesSlice(call->session, result);
		auto messages = QJsonArray();
		for (auto i = parsed.items.rbegin(); i != parsed.items.rend(); ++i) {
			const auto peer = (*i)->history()->peer;
			if (!call->gate->visible(peer->id.value)) {
				continue;
			}
			messages.push_back(ToolFormat::SerializeMessageWithChat(*i));
		}
		auto json = QJsonObject();
		json.insert("messages", messages);
		json.insert("has_older", QJsonValue(int(parsed.items.size()) == limit));
		if (int(parsed.items.size()) == limit && !parsed.items.empty()) {
			const auto last = parsed.items.back();
			json.insert("next_offset", u"%1_%2_%3"_q
				.arg(parsed.nextRate)
				.arg(last->history()->peer->id.value)
				.arg(last->id.bare));
		}
		call->done(std::move(json));
	};
	call->api.request(MTPmessages_SearchGlobal(
		MTP_flags(MTPmessages_SearchGlobal::Flag()),
		MTP_int(0),
		MTPInputChannel(),
		MTP_string(query),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(minDate),
		MTP_int(maxDate),
		MTP_int(offsetRate),
		(offsetPeer
			? offsetPeer->input()
			: MTP_inputPeerEmpty()),
		MTP_int(offsetId),
		MTP_int(limit)
	)).done([=](const MTPmessages_Messages &result) {
		finish(result);
	}).fail([=](const MTP::Error &error) {
		call->fail(error.type());
	}).send();
}

void DeliverBasicGroupMembers(not_null<PendingCall*> call) {
	const auto chat = call->peer->asChat();
	auto members = QJsonArray();
	for (const auto &user : chat->participants) {
		const auto role = (user->id.value == chat->creator.bare)
			? u"owner"_q
			: chat->admins.contains(user)
			? u"admin"_q
			: u"member"_q;
		members.push_back(MemberJson(user, role));
	}
	auto json = QJsonObject();
	json.insert("members", members);
	json.insert("has_more", QJsonValue(false));
	call->done(std::move(json));
}

} // namespace

namespace ChatTools {

void RunGetContext(not_null<PendingCall*> call) {
	const auto session = call->session;
	auto result = QJsonObject();
	const auto now = QDateTime::currentDateTime();
	result.insert(
		"now",
		now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate));
	result.insert("weekday", QLocale::c().toString(now.date(), u"dddd"_q));
	const auto user = session->user();
	auto account = QJsonObject();
	account.insert("id", QJsonValue(qint64(user->id.value)));
	account.insert("name", user->name());
	const auto username = user->username();
	if (!username.isEmpty()) {
		account.insert("username", username);
	}
	result.insert("account", account);
	if (const auto peer = call->peer) {
		result.insert("chat", SerializeChatBrief(MakeChatBrief(peer)));
	}
	call->done(std::move(result));
}

void RunReadHistory(not_null<PendingCall*> call) {
	const auto &input = call->input;
	const auto peer = call->peer;
	auto error = QString();
	const auto around = JsonInt(input, "around_id", error);
	const auto before = JsonInt(input, "before_id", error);
	const auto after = JsonInt(input, "after_id", error);
	const auto limitValue = JsonInt(input, "limit", error);
	const auto topic = JsonInt(input, "topic_id", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto given = (around.has_value() ? 1 : 0)
		+ (before.has_value() ? 1 : 0)
		+ (after.has_value() ? 1 : 0);
	if (given > 1) {
		call->fail(u"Give at most one of around_id, before_id, after_id."_q);
		return;
	}
	const auto limit = limitValue.value_or(kDefaultHistoryLimit);
	if (limit < 1 || limit > 100) {
		call->fail(u"limit must be between 1 and 100."_q);
		return;
	}
	const auto forum = (peer->asChannel() && peer->asChannel()->isForum());
	if (topic.has_value() && !forum) {
		call->fail(u"topic_id is only supported in forums."_q);
		return;
	}
	const auto page = around
		? Page::Around
		: before
		? Page::Before
		: after
		? Page::After
		: Page::Newest;
	auto offsetId = MsgId();
	auto addOffset = 0;
	auto minId = MsgId();
	if (around) {
		offsetId = MsgId(*around);
		addOffset = -limit / 2;
	} else if (before) {
		offsetId = MsgId(*before);
	} else if (after) {
		offsetId = MsgId(*after);
		addOffset = -limit;
		minId = MsgId(*after);
	}
	const auto topicId = topic.value_or(0);
	const auto offsetIdInt = static_cast<int>(offsetId.bare);
	const auto minIdInt = static_cast<int>(minId.bare);
	const auto finish = [call, page, limit](const MTPmessages_Messages &result) {
		DeliverMessagesPage(
			call,
			ParseMessagesSlice(call->session, result),
			limit,
			page,
			false);
	};
	const auto fail = [call](const MTP::Error &error) {
		call->fail(error.type());
	};
	auto &histories = call->session->data().histories();
	const auto history = call->session->data().history(peer);
	const auto requestId = histories.sendRequest(
		history,
		Data::Histories::RequestType::History,
		[=](Fn<void()> finishRequest) {
			const auto send = [&](auto &&request) {
				return call->api.request(std::move(request))
					.done([=](const MTPmessages_Messages &result) {
						finishRequest();
						finish(result);
					})
					.fail([=](const MTP::Error &error) {
						finishRequest();
						fail(error);
					})
					.send();
			};
			if (topic) {
				return send(MTPmessages_GetReplies(
					peer->input(),
					MTP_int(topicId),
					MTP_int(offsetIdInt),
					MTP_int(0),
					MTP_int(addOffset),
					MTP_int(limit),
					MTP_int(0),
					MTP_int(minIdInt),
					MTP_long(0)));
			}
			return send(MTPmessages_GetHistory(
				peer->input(),
				MTP_int(offsetIdInt),
				MTP_int(0),
				MTP_int(addOffset),
				MTP_int(limit),
				MTP_int(0),
				MTP_int(minIdInt),
				MTP_long(0)));
		});
	call->setRequestId(requestId);
}

void RunSearchMessages(not_null<PendingCall*> call) {
	const auto &input = call->input;
	const auto peer = call->peer;
	const auto query = input.value("query").toString().trimmed();
	if (query.isEmpty()) {
		call->fail(u"query must not be empty."_q);
		return;
	}
	auto error = QString();
	const auto from = input.value("from").toString().trimmed();
	const auto minDateString = input.value("min_date").toString();
	const auto maxDateString = input.value("max_date").toString();
	const auto limitValue = JsonInt(input, "limit", error);
	const auto offset = JsonInt(input, "offset_id", error);
	const auto cursor = input.value("offset").toString().trimmed();
	const auto topic = JsonInt(input, "topic_id", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto limit = limitValue.value_or(kDefaultSearchLimit);
	if (limit < 1 || limit > 50) {
		call->fail(u"limit must be between 1 and 50."_q);
		return;
	}
	auto fromPeer = MTPInputPeer();
	const auto fromGiven = !from.isEmpty();
	if (fromGiven && from != u"me") {
		UserData *fromUser = nullptr;
		auto ok = false;
		const auto number = from.toLongLong(&ok);
		if (ok) {
			fromUser = call->session->data().userLoaded(UserId(number));
		} else {
			const auto name = from.startsWith(u'@') ? from.mid(1) : from;
			const auto named = call->session->data().peerByUsername(name);
			fromUser = named ? named->asUser() : nullptr;
		}
		if (!fromUser) {
			call->fail(u"Unknown user: %1."_q.arg(from));
			return;
		}
		fromPeer = fromUser->input();
	} else if (fromGiven) {
		fromPeer = MTP_inputPeerSelf();
	}
	const auto parseDate = [&](const QString &value, bool end) {
		if (value.isEmpty()) {
			return 0;
		}
		const auto date = QDate::fromString(value, u"yyyy-MM-dd"_q);
		if (!date.isValid()) {
			error = u"Bad date %1, expected YYYY-MM-DD."_q.arg(value);
			return 0;
		}
		const auto start = date.startOfDay().toSecsSinceEpoch();
		return end
			? int(date.addDays(1).startOfDay().toSecsSinceEpoch() - 1)
			: int(start);
	};
	const auto minDate = parseDate(minDateString, false);
	const auto maxDate = error.isEmpty() ? parseDate(maxDateString, true) : 0;
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	if (!peer) {
		if (fromGiven) {
			call->fail(u"from is only supported when searching a single chat."_q);
			return;
		} else if (topic) {
			call->fail(u"topic_id is only supported in forums."_q);
			return;
		} else if (offset.has_value()) {
			call->fail(
				u"offset_id is only supported when searching a single chat."_q);
			return;
		}
		DeliverGlobalSearch(
			call,
			query,
			minDate,
			maxDate,
			cursor,
			limit);
		return;
	}
	if (!cursor.isEmpty()) {
		call->fail(u"offset is only supported when searching all chats."_q);
		return;
	}
	const auto topicId = topic.value_or(0);
	const auto flags = (fromGiven ? MTPmessages_Search::Flag::f_from_id : MTPmessages_Search::Flag())
		| (topic ? MTPmessages_Search::Flag::f_top_msg_id : MTPmessages_Search::Flag());
	const auto finish = [call, limit](const MTPmessages_Messages &result) {
		DeliverMessagesPage(
			call,
			ParseMessagesSlice(call->session, result),
			limit,
			Page::Newest,
			true);
	};
	const auto fail = [call](const MTP::Error &error) {
		call->fail(error.type());
	};
	auto &histories = call->session->data().histories();
	const auto history = call->session->data().history(peer);
	const auto requestId = histories.sendRequest(
		history,
		Data::Histories::RequestType::History,
		[=](Fn<void()> finishRequest) {
			return call->api.request(MTPmessages_Search(
				MTP_flags(flags),
				peer->input(),
				MTP_string(query),
				fromPeer,
				MTPInputPeer(),
				MTPVector<MTPReaction>(),
				topic ? MTP_int(topicId) : MTPint(),
				MTP_inputMessagesFilterEmpty(),
				MTP_int(minDate),
				MTP_int(maxDate),
				MTP_int(offset.value_or(0)),
				MTP_int(0),
				MTP_int(limit),
				MTP_int(0),
				MTP_int(0),
				MTP_long(0)
			)).done([=](const MTPmessages_Messages &result) {
				finishRequest();
				finish(result);
			}).fail([=](const MTP::Error &error) {
				finishRequest();
				fail(error);
			}).send();
		});
	call->setRequestId(requestId);
}

void RunGetPeerInfo(not_null<PendingCall*> call) {
	OnFullPeer(call, [call] {
		call->done(BuildPeerInfoJson(call->peer, call->gate));
	});
}

void RunListMembers(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	if (const auto user = peer->asUser()) {
		auto members = QJsonArray();
		members.push_back(MemberJson(user, u"member"_q));
		members.push_back(
			MemberJson(call->session->user(), u"member"_q));
		auto json = QJsonObject();
		json.insert("members", members);
		json.insert("has_more", QJsonValue(false));
		call->done(std::move(json));
		return;
	}
	if (const auto chat = peer->asChat()) {
		if (chat->participants.empty() && !peer->isFullLoaded()) {
			OnFullPeer(call, [call] {
				DeliverBasicGroupMembers(call);
			});
		} else {
			DeliverBasicGroupMembers(call);
		}
		return;
	}
	const auto channel = peer->asChannel();
	if (!channel) {
		call->fail(u"This chat has no members to list."_q);
		return;
	}
	const auto &input = call->input;
	const auto query = input.value("query").toString().trimmed();
	auto error = QString();
	const auto limitValue = JsonInt(input, "limit", error);
	const auto offset = JsonInt(input, "offset", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto limit = limitValue.value_or(kDefaultMembersLimit);
	if (limit < 1 || limit > 100) {
		call->fail(u"limit must be between 1 and 100."_q);
		return;
	}
	const auto offsetValue = offset.value_or(0);
	const auto filter = query.isEmpty()
		? MTP_channelParticipantsRecent()
		: MTP_channelParticipantsSearch(MTP_string(query));
	call->api.request(MTPchannels_GetParticipants(
		channel->inputChannel(),
		std::move(filter),
		MTP_int(offsetValue),
		MTP_int(limit),
		MTP_long(0)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		if (result.type() == mtpc_channels_channelParticipantsNotModified) {
			call->fail(u"channels.channelParticipantsNotModified"_q);
			return;
		}
		const auto &data = result.c_channels_channelParticipants();
		call->session->data().processUsers(data.vusers());
		const auto parsed = Api::ChatParticipants::Parse(channel, data);
		auto members = QJsonArray();
		for (const auto &participant : parsed.list) {
			const auto user = call->session->data().userLoaded(
				participant.userId());
			if (!user) {
				continue;
			}
			const auto role = participant.isCreator()
				? u"owner"_q
				: participant.isCreatorOrAdmin()
				? u"admin"_q
				: u"member"_q;
			members.push_back(MemberJson(user, role));
		}
		auto json = QJsonObject();
		json.insert("members", members);
		json.insert(
			"has_more",
			QJsonValue(int(parsed.list.size()) == limit));
		call->done(std::move(json));
	}).fail([=](const MTP::Error &error) {
		call->fail(error.type());
	}).send();
}

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto empty = QJsonObject();
	auto historyProperties = QJsonObject();
	historyProperties.insert("around_id", MessageIdProperty(
		u"Read a window centered on this message id."_q));
	historyProperties.insert("before_id", MessageIdProperty(
		u"Read only messages older than this id."_q));
	historyProperties.insert("after_id", MessageIdProperty(
		u"Read only messages newer than this id."_q));
	historyProperties.insert("limit", IntProperty(
		1,
		100,
		u"How many messages to return, 1..100, default 30."_q));
	historyProperties.insert("topic_id", MessageIdProperty(
		u"Forum topic root message id."_q));
	auto searchProperties = QJsonObject();
	searchProperties.insert("query", StringProperty(
		u"Text to search for."_q));
	searchProperties.insert("from", StringProperty(
		u"\"me\", a numeric user id or @username, resolved among"
		" locally known users only."_q));
	searchProperties.insert("min_date", StringProperty(
		u"Only messages sent on or after this local date YYYY-MM-DD."_q));
	searchProperties.insert("max_date", StringProperty(
		u"Only messages sent on or before this local date YYYY-MM-DD."_q));
	searchProperties.insert("limit", IntProperty(
		1,
		50,
		u"How many messages to return, 1..50, default 20."_q));
	searchProperties.insert("offset_id", MessageIdProperty(
		u"Pagination in one chat: search older than this message id."_q));
	searchProperties.insert("offset", StringProperty(
		u"Pagination for the all-chats search: pass the next_offset"
		" string from the previous page here. Only valid when no"
		" chat is given."_q));
	searchProperties.insert("topic_id", MessageIdProperty(
		u"Forum topic root message id."_q));
	auto membersProperties = QJsonObject();
	membersProperties.insert("query", StringProperty(
		u"Search members by name or username."_q));
	membersProperties.insert("limit", IntProperty(
		1,
		100,
		u"How many members to return, 1..100, default 50."_q));
	membersProperties.insert("offset", IntProperty(
		0,
		std::numeric_limits<int>::max(),
		u"Skip this many members."_q));
	auto push = [&](const QString &name,
			const QString &description,
			const QString &generalDescription,
			QJsonObject schema,
			ChatParam chatParam,
			auto run) {
		result.push_back(ToolEntry{
			.name = name,
			.description = description,
			.generalDescription = generalDescription,
			.inputSchema = std::move(schema),
			.policy = ToolPolicy{
				.general = true,
				.chat = true,
			},
			.chatParam = chatParam,
			.run = run,
		});
	};
	push(
		u"get_context"_q,
		u"Get the current time and a summary of this chat."_q,
		u"Get the current time and your account details."_q,
		ToolSchema(std::move(empty), QJsonArray()),
		ChatParam::None,
		RunGetContext);
	push(
		u"read_history"_q,
		u"Read messages of this chat, oldest to newest. Give at most"
		" one of around_id, before_id, after_id. Reading never marks"
		" anything read."_q,
		u"Read messages of the chat, oldest to newest. Give at most"
		" one of around_id, before_id, after_id. Reading never marks"
		" anything read."_q,
		ToolSchema(std::move(historyProperties), QJsonArray()),
		ChatParam::One,
		RunReadHistory);
	push(
		u"search_messages"_q,
		u"Search messages in this chat, newest matches first. Page"
		" older results with offset_id from next_offset_id."_q,
		u"Search messages in the chat, or in all chats when no chat"
		" is given, newest matches first. Page older results with"
		" offset_id from next_offset_id in one chat, or with offset"
		" from next_offset when searching all chats."_q,
		ToolSchema(
			std::move(searchProperties),
			QJsonArray{ QJsonValue(u"query"_q) }),
		ChatParam::OptionalOne,
		RunSearchMessages);
	push(
		u"get_peer_info"_q,
		u"Get details about this chat: title, kind, description,"
		" members, your role and rights."_q,
		u"Get details about the chat: title, kind, description,"
		" members, your role and rights."_q,
		ToolSchema(QJsonObject(), QJsonArray()),
		ChatParam::One,
		RunGetPeerInfo);
	push(
		u"list_members"_q,
		u"List members of this chat with names, usernames and roles."_q,
		u"List members of the chat with names, usernames and roles."_q,
		ToolSchema(std::move(membersProperties), QJsonArray()),
		ChatParam::One,
		RunListMembers);
	return result;
}



} // namespace ChatTools

ChatBrief MakeChatBrief(not_null<PeerData*> peer) {
	const auto history = peer->owner().history(peer);
	return ChatBrief{
		.peer = peer->id.value,
		.title = peer->name(),
		.kind = ToolFormat::PeerKind(peer),
		.username = ToolFormat::PeerUsername(peer),
		.members = PeerMembersCount(peer),
		.role = PeerRole(peer),
		.unread = history->unreadCountKnown()
			? history->unreadCount()
			: 0,
		.canWrite = Data::CanSendAnything(peer),
	};
}

std::vector<PeerName> NameCandidates(not_null<Main::Session*> session) {
	auto result = std::vector<PeerName>();
	const auto collect = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : list->indexed()->all()) {
			if (const auto history = row->history()) {
				const auto peer = history->peer;
				result.push_back(PeerName{
					.peer = peer->id.value,
					.title = peer->name(),
					.username = ToolFormat::PeerUsername(peer),
				});
			}
		}
	};
	collect(session->data().chatsList());
	if (const auto folder = session->data().folderLoaded(Data::Folder::kId)) {
		collect(folder->chatsList());
	}
	return result;
}

QString AccountLine(not_null<Main::Session*> session) {
	const auto user = session->user();
	auto result = user->name();
	result += u" ("_q;
	if (!user->username().isEmpty()) {
		result += u"@%1, "_q.arg(user->username());
	}
	result += u"id %1)"_q.arg(user->id.value);
	return result;
}

} // namespace Agent
