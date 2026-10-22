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
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/timer.h"
#include "base/unixtime.h"
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
#include "lang/lang_keys.h"
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
constexpr auto kHistoryMaxTextChars = 1500;
constexpr auto kMaxReadIds = 50;

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

[[nodiscard]] QStringList PeerDefaultRestrictions(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return ToolRights::RestrictionNames(channel->defaultRestrictions());
	} else if (const auto chat = peer->asChat()) {
		return ToolRights::RestrictionNames(chat->defaultRestrictions());
	}
	return {};
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
	const auto defaultDenied = PeerDefaultRestrictions(peer);
	if (!defaultDenied.isEmpty()) {
		auto list = QJsonArray();
		for (const auto &name : defaultDenied) {
			list.push_back(name);
		}
		result.insert("member_permissions", list);
	}
	if (const auto channel = peer->asChannel()) {
		if (const auto seconds = channel->slowmodeSeconds(); seconds > 0) {
			result.insert("slow_mode", QJsonValue(seconds));
		}
	}
	if (const auto linked = PeerLinkedChat(peer);
			linked
				&& gate->visible(linked->id.value)
				&& gate->inFrame(linked->id.value)) {
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

[[nodiscard]] std::vector<MsgId> ParseHistoryIds(
		const QJsonObject &input,
		QString &error) {
	const auto value = input.value("ids");
	if (value.isUndefined() || value.isNull()) {
		return {};
	} else if (!value.isArray()) {
		error = u"ids must be an array of message ids."_q;
		return {};
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > kMaxReadIds) {
		error = u"ids must contain 1 to %1 message ids."_q.arg(kMaxReadIds);
		return {};
	}
	auto result = std::vector<MsgId>();
	for (const auto &item : list) {
		if (!item.isDouble() || item.toDouble() < 1) {
			error = u"ids must contain message ids."_q;
			return {};
		}
		const auto id = MsgId(qint64(item.toDouble()));
		if (!ranges::contains(result, id)) {
			result.push_back(id);
		}
	}
	return result;
}

[[nodiscard]] std::optional<int> ParseHistoryDate(
		const QString &value,
		QString &error) {
	const auto date = QDate::fromString(value, u"yyyy-MM-dd"_q);
	if (date.isValid()) {
		return int(date.startOfDay().toSecsSinceEpoch());
	}
	const auto dateTime = QDateTime::fromString(value, u"yyyy-MM-dd HH:mm"_q);
	if (!dateTime.isValid()) {
		error = u"Bad from_date %1, expected YYYY-MM-DD or"
			" YYYY-MM-DD HH:MM."_q.arg(value);
		return std::nullopt;
	}
	return int(dateTime.toSecsSinceEpoch());
}

void DeliverHistoryPage(
		not_null<PendingCall*> call,
		ParsedSlice parsed,
		int limit,
		Page page) {
	auto messages = QJsonArray();
	const auto &items = parsed.items;
	for (auto i = items.rbegin(); i != items.rend(); ++i) {
		call->markSeen((*i)->fullId());
		messages.push_back(ToolFormat::SerializeMessage(
			*i,
			kHistoryMaxTextChars));
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
	call->done(std::move(json));
}

void DeliverMessagesByIds(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		const std::vector<MsgId> &ids) {
	const auto session = call->session;
	const auto finish = [=] {
		auto found = base::flat_map<int64, not_null<HistoryItem*>>();
		auto missing = QJsonArray();
		for (const auto id : ids) {
			const auto item = session->data().message(peer->id, id);
			if (item) {
				found.emplace(id.bare, item);
			} else {
				missing.push_back(QJsonValue(qint64(id.bare)));
			}
		}
		auto messages = QJsonArray();
		for (const auto &[id, item] : found) {
			call->markSeen(item->fullId());
			messages.push_back(ToolFormat::SerializeMessage(item));
		}
		auto json = QJsonObject();
		json.insert("messages", messages);
		json.insert("missing", missing);
		call->done(std::move(json));
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
	const auto left = call->lifetime.make_state<int>(int(missing.size()));
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

struct SearchHit {
	FullMsgId id;
	TimeId date = 0;
	QJsonObject json;
};

struct SearchState {
	std::vector<QString> queries;
	std::vector<QString> chatTypes;
	std::vector<not_null<PeerData*>> targets;
	bool global = false;
	bool countsMode = false;
	bool anyFull = false;
	bool prepared = false;
	int limit = 0;
	int context = 0;
	std::vector<Fn<void(Fn<void()>)>> tasks;
	int taskIndex = 0;
	Fn<void()> next;
	std::vector<SearchHit> hits;
	std::vector<QJsonObject> seen;
	std::vector<QJsonObject> failed;
	base::flat_set<FullMsgId> found;
	base::flat_map<PeerId, int> countsMap;
	int lastOffsetId = 0;
	int lastOffsetRate = 0;
	FullMsgId lastGlobalId;
};

[[nodiscard]] QString SearchKindName(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->isBot() ? u"bot"_q : u"private"_q;
	}
	return (ToolFormat::PeerKind(peer) == u"channel"_q)
		? u"channel"_q
		: u"group"_q;
}

void ProcessSearchHit(
		not_null<PendingCall*> call,
		not_null<SearchState*> state,
		not_null<HistoryItem*> item,
		int queryIndex) {
	const auto peer = item->history()->peer;
	const auto id = FullMsgId(peer->id, item->id);
	if (state->found.contains(id)) {
		return;
	}
	state->found.insert(id);
	if (call->seen(id)) {
		auto json = QJsonObject();
		json.insert("chat", QJsonValue(qint64(peer->id.value)));
		json.insert("id", QJsonValue(qint64(item->id.bare)));
		state->seen.push_back(std::move(json));
		return;
	}
	const auto text = ToolFormat::EntitiesToMarkdown(item->originalText());
	auto serialized = ToolFormat::SerializeMessage(item);
	auto json = QJsonObject();
	if (state->targets.size() != 1) {
		auto chat = QJsonObject();
		chat.insert("id", QJsonValue(qint64(peer->id.value)));
		chat.insert("title", peer->name());
		json.insert("chat", chat);
	}
	json.insert("id", serialized.value("id"));
	json.insert("date", serialized.value("date"));
	json.insert("from", serialized.value("from"));
	for (const auto key : { "media", "reply_to", "topic_id" }) {
		if (serialized.contains(key)) {
			json.insert(key, serialized.value(key));
		}
	}
	json.insert("snippet", MatchSnippet(text, state->queries, 300));
	if (serialized.contains("forwarded_from")) {
		json.insert("forwarded_from", serialized.value("forwarded_from"));
	}
	json.insert("length", QJsonValue(int(text.size())));
	if (state->queries.size() > 1) {
		json.insert("matched", state->queries[queryIndex]);
	}
	state->hits.push_back(SearchHit{
		.id = id,
		.date = item->date(),
		.json = std::move(json),
	});
}

[[nodiscard]] QJsonObject SerializeNeighbor(not_null<HistoryItem*> item) {
	auto serialized = ToolFormat::SerializeMessage(item, 300);
	auto result = QJsonObject();
	result.insert("id", serialized.value("id"));
	result.insert("date", serialized.value("date"));
	for (const auto key : { "from", "text" }) {
		if (serialized.contains(key)) {
			result.insert(key, serialized.value(key));
		}
	}
	return result;
}

void ProcessChatSlice(
		not_null<PendingCall*> call,
		not_null<SearchState*> state,
		not_null<PeerData*> peer,
		int queryIndex,
		const MTPmessages_Messages &result) {
	const auto parsed = ParseMessagesSlice(call->session, result);
	state->anyFull = state->anyFull
		|| (int(parsed.items.size()) == state->limit);
	if (state->countsMode) {
		const auto count = (parsed.total >= 0)
			? parsed.total
			: int(parsed.items.size());
		state->countsMap[peer->id] += count;
		return;
	}
	if (state->queries.size() == 1
		&& state->targets.size() == 1
		&& !parsed.items.empty()) {
		state->lastOffsetId = parsed.items.back()->id.bare;
	}
	for (const auto &item : parsed.items) {
		ProcessSearchHit(call, state, item, queryIndex);
	}
}

void ProcessGlobalSlice(
		not_null<PendingCall*> call,
		not_null<SearchState*> state,
		int queryIndex,
		const MTPmessages_Messages &result) {
	const auto parsed = ParseMessagesSlice(call->session, result);
	state->anyFull = state->anyFull
		|| (int(parsed.items.size()) == state->limit);
	if (state->countsMode) {
		auto page = base::flat_map<PeerId, int>();
		for (const auto &item : parsed.items) {
			page[item->history()->peer->id] += 1;
		}
		for (const auto &[peerId, count] : page) {
			state->countsMap[peerId] += count;
		}
		return;
	}
	if (state->queries.size() == 1 && !parsed.items.empty()) {
		state->lastOffsetRate = parsed.nextRate;
		const auto last = parsed.items.back();
		state->lastGlobalId = FullMsgId(
			last->history()->peer->id,
			last->id);
	}
	for (const auto &item : parsed.items) {
		const auto peer = item->history()->peer;
		if (!call->gate->visible(peer->id.value)
			|| !call->gate->inFrame(peer->id.value)) {
			continue;
		}
		if (!state->chatTypes.empty()
			&& !ranges::contains(state->chatTypes, SearchKindName(peer))) {
			continue;
		}
		ProcessSearchHit(call, state, item, queryIndex);
	}
}

void DeliverSearchResult(
		not_null<PendingCall*> call,
		not_null<SearchState*> state) {
	auto json = QJsonObject();
	if (state->countsMode) {
		auto counts = QJsonArray();
		auto list = std::vector<std::pair<PeerId, int>>();
		list.reserve(state->countsMap.size());
		for (const auto &[peerId, count] : state->countsMap) {
			list.emplace_back(peerId, count);
		}
		ranges::sort(list, [](const auto &a, const auto &b) {
			return a.second > b.second;
		});
		for (const auto &[peerId, count] : list) {
			const auto peer = call->session->data().peerLoaded(peerId);
			if (!peer) {
				continue;
			}
			auto chat = QJsonObject();
			chat.insert("id", QJsonValue(qint64(peerId.value)));
			chat.insert("title", peer->name());
			auto entry = QJsonObject();
			entry.insert("chat", chat);
			entry.insert("count", QJsonValue(count));
			counts.push_back(std::move(entry));
		}
		json.insert("counts", counts);
		json.insert("complete", QJsonValue(!state->global));
		call->done(std::move(json));
		return;
	}
	auto hits = QJsonArray();
	for (auto &hit : state->hits) {
		hits.push_back(hit.json);
		call->markSeen(hit.id);
	}
	json.insert("hits", hits);
	if (!state->seen.empty()) {
		auto seen = QJsonArray();
		for (auto &entry : state->seen) {
			seen.push_back(std::move(entry));
		}
		json.insert("seen", seen);
	}
	if (!state->failed.empty()) {
		auto failed = QJsonArray();
		for (auto &entry : state->failed) {
			failed.push_back(std::move(entry));
		}
		json.insert("failed", failed);
	}
	if (state->queries.size() == 1 && state->targets.size() == 1) {
		if (state->anyFull && state->lastOffsetId > 0) {
			json.insert("next_offset_id", QJsonValue(state->lastOffsetId));
		}
	} else if (state->queries.size() == 1 && state->global) {
		if (state->anyFull && state->lastGlobalId) {
			json.insert("next_offset", u"%1_%2_%3"_q
				.arg(state->lastOffsetRate)
				.arg(state->lastGlobalId.peer.value)
				.arg(state->lastGlobalId.msg.bare));
		}
	} else if (state->anyFull) {
		json.insert("has_more", QJsonValue(true));
	}
	call->done(std::move(json));
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

void DeliverBasicGroupAdmins(not_null<PendingCall*> call) {
	const auto chat = call->peer->asChat();
	auto members = QJsonArray();
	const auto push = [&](not_null<UserData*> user, const QString &role) {
		auto member = MemberJson(user, role);
		const auto rights = ToolRights::AllAdminRightNames();
		auto list = QJsonArray();
		for (const auto &right : rights) {
			list.push_back(right);
		}
		member.insert("rights", list);
		members.push_back(std::move(member));
	};
	if (const auto creator = call->session->data().userLoaded(
			chat->creator.bare)) {
		push(creator, u"owner"_q);
	}
	for (const auto &user : chat->admins) {
		push(user, u"admin"_q);
	}
	auto json = QJsonObject();
	json.insert("members", members);
	json.insert("has_more", QJsonValue(false));
	call->done(std::move(json));
}

[[nodiscard]] QString MemberRole(const Api::ChatParticipant &participant) {
	switch (participant.type()) {
	case Api::ChatParticipant::Type::Creator: return u"owner"_q;
	case Api::ChatParticipant::Type::Admin: return u"admin"_q;
	case Api::ChatParticipant::Type::Banned: return u"banned"_q;
	case Api::ChatParticipant::Type::Restricted: return u"restricted"_q;
	default: return u"member"_q;
	}
}

[[nodiscard]] QJsonObject MemberJsonWithRights(
		not_null<UserData*> user,
		const Api::ChatParticipant &participant) {
	auto member = MemberJson(user, MemberRole(participant));
	if (participant.isCreatorOrAdmin()) {
		const auto rank = participant.rank();
		if (!rank.isEmpty()) {
			member.insert("rank", rank);
		}
		auto list = QJsonArray();
		for (const auto &right
				: ToolRights::AdminRightNames(participant.rights().flags)) {
			list.push_back(right);
		}
		if (!list.isEmpty()) {
			member.insert("rights", list);
		}
	} else if (participant.type() == Api::ChatParticipant::Type::Banned
		|| participant.type() == Api::ChatParticipant::Type::Restricted) {
		auto list = QJsonArray();
		for (const auto &name : ToolRights::RestrictionNames(
				participant.restrictions().flags)) {
			list.push_back(name);
		}
		member.insert("denied", list);
		const auto until = participant.restrictions().until;
		if (!ChannelData::IsRestrictedForever(until)) {
			member.insert("until", base::unixtime::parse(until)
				.toString(u"yyyy-MM-dd HH:mm"_q));
		}
	}
	return member;
}

[[nodiscard]] QJsonObject SearchQueriesProperty() {
	auto item = QJsonObject();
	item.insert("type", QJsonValue(u"string"_q));
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"array"_q));
	result.insert("items", item);
	result.insert("minItems", QJsonValue(1));
	result.insert("maxItems", QJsonValue(5));
	result.insert("description", QJsonValue(u"Up to 5 alternative words"
		u" or phrases to search for."_q));
	return result;
}

[[nodiscard]] QJsonObject SearchMediaProperty() {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"string"_q));
	auto list = QJsonArray();
	list.push_back(u"photo"_q);
	list.push_back(u"video"_q);
	list.push_back(u"photo_video"_q);
	list.push_back(u"document"_q);
	list.push_back(u"voice"_q);
	list.push_back(u"round_video"_q);
	list.push_back(u"music"_q);
	list.push_back(u"url"_q);
	list.push_back(u"gif"_q);
	result.insert("enum", list);
	result.insert("description", QJsonValue(u"Only messages with this"
		u" media type."_q));
	return result;
}

[[nodiscard]] QJsonObject SearchChatTypesProperty() {
	auto item = QJsonObject();
	item.insert("type", QJsonValue(u"string"_q));
	auto list = QJsonArray();
	list.push_back(u"private"_q);
	list.push_back(u"group"_q);
	list.push_back(u"channel"_q);
	list.push_back(u"bot"_q);
	item.insert("enum", list);
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"array"_q));
	result.insert("items", item);
	result.insert("minItems", QJsonValue(1));
	result.insert("maxItems", QJsonValue(4));
	result.insert("description", QJsonValue(u"Account scope only, when"
		u" searching all chats: only these chat kinds."_q));
	return result;
}

[[nodiscard]] QJsonObject SearchModeProperty() {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"string"_q));
	auto list = QJsonArray();
	list.push_back(u"messages"_q);
	list.push_back(u"counts"_q);
	result.insert("enum", list);
	result.insert("description", QJsonValue(u"messages (default) returns"
		u" hits, counts returns how many messages match in each chat."_q));
	return result;
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
	const auto ids = ParseHistoryIds(input, error);
	const auto around = JsonInt(input, "around_id", error);
	const auto before = JsonInt(input, "before_id", error);
	const auto after = JsonInt(input, "after_id", error);
	const auto fromDateString = input.value("from_date").toString();
	const auto limitValue = JsonInt(input, "limit", error);
	const auto topic = JsonInt(input, "topic_id", error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	if (!ids.empty()) {
		if (around.has_value()
			|| before.has_value()
			|| after.has_value()
			|| !fromDateString.isEmpty()
			|| limitValue.has_value()
			|| topic.has_value()) {
			call->fail(
				u"ids cannot be combined with other parameters."_q);
			return;
		}
		DeliverMessagesByIds(call, peer, ids);
		return;
	}
	const auto given = (around.has_value() ? 1 : 0)
		+ (before.has_value() ? 1 : 0)
		+ (after.has_value() ? 1 : 0)
		+ (fromDateString.isEmpty() ? 0 : 1);
	if (given > 1) {
		call->fail(u"Give at most one of around_id, before_id, after_id,"
			" from_date."_q);
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
	auto fromDate = 0;
	if (!fromDateString.isEmpty()) {
		fromDate = ParseHistoryDate(fromDateString, error).value_or(0);
		if (!error.isEmpty()) {
			call->fail(error);
			return;
		}
	}
	const auto page = fromDate
		? Page::After
		: around
		? Page::Around
		: before
		? Page::Before
		: after
		? Page::After
		: Page::Newest;
	auto offsetId = MsgId();
	auto addOffset = 0;
	auto minId = MsgId();
	if (fromDate) {
		addOffset = -limit;
	} else if (around) {
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
		DeliverHistoryPage(
			call,
			ParseMessagesSlice(call->session, result),
			limit,
			page);
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
					MTP_int(fromDate),
					MTP_int(addOffset),
					MTP_int(limit),
					MTP_int(0),
					MTP_int(minIdInt),
					MTP_long(0)));
			}
			return send(MTPmessages_GetHistory(
				peer->input(),
				MTP_int(offsetIdInt),
				MTP_int(fromDate),
				MTP_int(addOffset),
				MTP_int(limit),
				MTP_int(0),
				MTP_int(minIdInt),
				MTP_long(0)));
		});
	call->setRequestId(requestId);
}

[[nodiscard]] std::vector<QString> SearchQueries(
		const QJsonObject &input,
		QString &error) {
	auto result = std::vector<QString>();
	const auto single = input.value("query").toString().trimmed();
	if (!single.isEmpty()) {
		result.push_back(single);
	}
	const auto value = input.value("queries");
	if (!value.isUndefined() && !value.isNull()) {
		if (!value.isArray()) {
			error = u"queries must be an array of strings."_q;
			return result;
		}
		const auto list = value.toArray();
		if (list.isEmpty() || list.size() > 5) {
			error = u"queries must contain 1 to 5 strings."_q;
			return result;
		}
		for (const auto &item : list) {
			const auto text = item.toString().trimmed();
			if (text.isEmpty() || text.size() > 100) {
				error = u"queries must contain strings of 1 to 100"
					u" characters."_q;
				return result;
			}
			if (!ranges::contains(result, text)) {
				result.push_back(text);
			}
		}
	}
	return result;
}

[[nodiscard]] MTPMessagesFilter SearchMediaFilter(
		const QJsonObject &input,
		QString &error) {
	const auto media = input.value("media").toString().trimmed().toLower();
	if (media.isEmpty()) {
		return MTP_inputMessagesFilterEmpty();
	} else if (media == u"photo") {
		return MTP_inputMessagesFilterPhotos();
	} else if (media == u"video") {
		return MTP_inputMessagesFilterVideo();
	} else if (media == u"photo_video") {
		return MTP_inputMessagesFilterPhotoVideo();
	} else if (media == u"document") {
		return MTP_inputMessagesFilterDocument();
	} else if (media == u"voice") {
		return MTP_inputMessagesFilterVoice();
	} else if (media == u"round_video") {
		return MTP_inputMessagesFilterRoundVideo();
	} else if (media == u"music") {
		return MTP_inputMessagesFilterMusic();
	} else if (media == u"url") {
		return MTP_inputMessagesFilterUrl();
	} else if (media == u"gif") {
		return MTP_inputMessagesFilterGif();
	}
	error = u"Unknown media: %1. The types are: photo, video,"
		u" photo_video, document, voice, round_video, music, url, gif."_q
		.arg(media);
	return MTP_inputMessagesFilterEmpty();
}

[[nodiscard]] std::vector<QString> SearchChatTypes(
		const QJsonObject &input,
		QString &error) {
	auto result = std::vector<QString>();
	const auto value = input.value("chat_types");
	if (value.isUndefined() || value.isNull()) {
		return result;
	}
	if (!value.isArray()) {
		error = u"chat_types must be an array of strings."_q;
		return result;
	}
	const auto list = value.toArray();
	if (list.isEmpty()) {
		error = u"chat_types must not be empty."_q;
		return result;
	}
	for (const auto &item : list) {
		const auto type = item.toString().trimmed().toLower();
		if (type != u"private" && type != u"group"
			&& type != u"channel" && type != u"bot") {
			error = u"Unknown chat type: %1. The types are: private,"
				u" group, channel, bot."_q.arg(item.toString());
			return result;
		}
		if (!ranges::contains(result, type)) {
			result.push_back(type);
		}
	}
	return result;
}

[[nodiscard]] QString SearchMode(
		const QJsonObject &input,
		QString &error) {
	const auto mode = input.value("mode").toString().trimmed().toLower();
	if (mode.isEmpty() || mode == u"messages") {
		return u"messages"_q;
	} else if (mode == u"counts") {
		return u"counts"_q;
	}
	error = u"Unknown mode: %1. The modes are: messages, counts."_q
		.arg(mode);
	return QString();
}

void RunSearchMessages(not_null<PendingCall*> call) {
	const auto &input = call->input;
	auto error = QString();
	const auto queries = SearchQueries(input, error);
	const auto chatTypes = error.isEmpty()
		? SearchChatTypes(input, error)
		: std::vector<QString>();
	const auto mode = error.isEmpty() ? SearchMode(input, error) : QString();
	const auto limitValue = error.isEmpty()
		? JsonInt(input, "limit", error)
		: std::optional<int>();
	const auto contextValue = error.isEmpty()
		? JsonInt(input, "context", error)
		: std::optional<int>();
	const auto topic = error.isEmpty()
		? JsonInt(input, "topic_id", error)
		: std::optional<int>();
	const auto offsetIdValue = error.isEmpty()
		? JsonInt(input, "offset_id", error)
		: std::optional<int>();
	const auto filter = error.isEmpty()
		? SearchMediaFilter(input, error)
		: MTPMessagesFilter();
	const auto from = input.value("from").toString().trimmed();
	const auto minDateString = input.value("min_date").toString();
	const auto maxDateString = input.value("max_date").toString();
	const auto cursor = input.value("offset").toString().trimmed();
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto limit = limitValue.value_or(kDefaultSearchLimit);
	if (limit < 1 || limit > 50) {
		call->fail(u"limit must be between 1 and 50."_q);
		return;
	}
	const auto context = contextValue.value_or(0);
	if (context < 0 || context > 2) {
		call->fail(u"context must be between 0 and 2."_q);
		return;
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
	const auto global = call->peers.empty();
	const auto targets = call->peers;
	if (!global && targets.size() > 20) {
		call->fail(u"Search is limited to 20 chats, split it into"
			u" several calls."_q);
		return;
	}
	const auto hasWords = !queries.empty();
	const auto hasMedia = !input.value("media").toString().isEmpty();
	const auto hasDates = !minDateString.isEmpty()
		|| !maxDateString.isEmpty();
	if (!hasWords && !hasMedia && !hasDates) {
		call->fail(u"Give a word, a media type or dates."_q);
		return;
	}
	const auto singleChat = !global && targets.size() == 1;
	if (!singleChat) {
		if (!from.isEmpty()) {
			call->fail(u"from is only supported when searching a single"
				u" chat."_q);
			return;
		}
		if (topic.has_value()) {
			call->fail(u"topic_id is only supported in forums."_q);
			return;
		}
		if (offsetIdValue.has_value()) {
			call->fail(
				u"offset_id is only supported when searching a single"
				u" chat."_q);
			return;
		}
	} else if (topic.has_value()) {
		const auto channel = targets.front()->asChannel();
		if (!channel || !channel->isForum()) {
			call->fail(u"topic_id is only supported in forums."_q);
			return;
		}
	}
	if (queries.size() != 1) {
		if (offsetIdValue.has_value()) {
			call->fail(u"offset_id is only supported with a single query."_q);
			return;
		}
		if (!cursor.isEmpty()) {
			call->fail(u"offset is only supported with a single query."_q);
			return;
		}
	}
	if (!global && !chatTypes.empty()) {
		call->fail(u"chat_types is only supported when searching all"
			u" chats."_q);
		return;
	}
	auto fromPeer = MTPInputPeer();
	if (!from.isEmpty() && from != u"me") {
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
	} else if (!from.isEmpty()) {
		fromPeer = MTP_inputPeerSelf();
	}
	auto cursorRate = 0;
	auto cursorPeer = (PeerData*)nullptr;
	auto cursorId = 0;
	if (!cursor.isEmpty()) {
		const auto parts = cursor.split('_');
		if (parts.size() != 3) {
			call->fail(u"Malformed search offset."_q);
			return;
		}
		auto okRate = false;
		auto okPeer = false;
		auto okId = false;
		cursorRate = parts[0].toInt(&okRate);
		const auto peerId = parts[1].toULongLong(&okPeer);
		cursorId = parts[2].toInt(&okId);
		if (!okRate || !okPeer || !okId) {
			call->fail(u"Malformed search offset."_q);
			return;
		}
		cursorPeer = call->session->data().peerLoaded(PeerId(peerId));
		if (!cursorPeer) {
			call->fail(u"Unknown chat in the search offset."_q);
			return;
		}
	}
	const auto state = call->lifetime.make_state<SearchState>();
	state->queries = queries;
	state->chatTypes = chatTypes;
	state->targets = targets;
	state->global = global;
	state->countsMode = (mode == u"counts"_q);
	state->limit = state->countsMode ? (global ? 100 : 1) : limit;
	state->context = context;
	if (!global) {
		auto filtered = std::vector<not_null<PeerData*>>();
		for (const auto peer : state->targets) {
			if (call->gate->visible(peer->id.value)
				&& call->gate->inFrame(peer->id.value)) {
				filtered.push_back(peer);
			}
		}
		state->targets = std::move(filtered);
	}
	if (state->queries.empty()) {
		state->queries.push_back(QString());
	}
	for (auto q = 0; q < int(state->queries.size()); ++q) {
		if (global) {
			state->tasks.push_back([=](Fn<void()> done) {
				const auto only = [&](const char *type) {
					return state->chatTypes.size() == 1
						&& state->chatTypes.front() == type;
				};
				const auto flags = (only("private")
					? MTPmessages_SearchGlobal::Flag::f_users_only
					: MTPmessages_SearchGlobal::Flag())
					| (only("group")
						? MTPmessages_SearchGlobal::Flag::f_groups_only
						: MTPmessages_SearchGlobal::Flag())
					| (only("channel")
						? MTPmessages_SearchGlobal::Flag::f_broadcasts_only
						: MTPmessages_SearchGlobal::Flag());
				const auto finish = [=](const MTPmessages_Messages &result) {
					ProcessGlobalSlice(call, state, q, result);
					done();
				};
				const auto fail = [call](const MTP::Error &error) {
					call->fail(error.type());
				};
				call->api.request(MTPmessages_SearchGlobal(
					MTP_flags(flags),
					MTP_int(0),
					MTPInputChannel(),
					MTP_string(state->queries[q]),
					filter,
					MTP_int(minDate),
					MTP_int(maxDate),
					MTP_int(cursorRate),
					(cursorPeer ? cursorPeer->input() : MTP_inputPeerEmpty()),
					MTP_int(cursorId),
					MTP_int(state->limit)
				)).done([=](const MTPmessages_Messages &result) {
					finish(result);
				}).fail([=](const MTP::Error &error) {
					fail(error);
				}).send();
			});
		} else {
			for (const auto peer : state->targets) {
				state->tasks.push_back([=](Fn<void()> done) {
					const auto flags = (fromPeer.type() != mtpc_inputPeerEmpty
						? MTPmessages_Search::Flag::f_from_id
						: MTPmessages_Search::Flag())
						| (topic.has_value()
							? MTPmessages_Search::Flag::f_top_msg_id
							: MTPmessages_Search::Flag());
					const auto finish = [=](
							const MTPmessages_Messages &result) {
						ProcessChatSlice(call, state, peer, q, result);
						done();
					};
					const auto fail = [=](const MTP::Error &error) {
						if (state->targets.size() == 1) {
							call->fail(error.type());
							return;
						}
						auto failed = QJsonObject();
						failed.insert(
							"chat",
							QJsonValue(qint64(peer->id.value)));
						failed.insert("error", error.type());
						state->failed.push_back(std::move(failed));
						done();
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
								MTP_string(state->queries[q]),
								fromPeer,
								MTPInputPeer(),
								MTPVector<MTPReaction>(),
								MTP_int(topic.value_or(0)),
								filter,
								MTP_int(minDate),
								MTP_int(maxDate),
								MTP_int(offsetIdValue.value_or(0)),
								MTP_int(0),
								MTP_int(state->limit),
								MTP_int(0),
								MTP_int(0),
								MTP_long(0)
							)).done([=](
									const MTPmessages_Messages &result) {
								finishRequest();
								finish(result);
							}).fail([=](const MTP::Error &error) {
								finishRequest();
								fail(error);
							}).send();
						});
					call->setRequestId(requestId);
				});
			}
		}
	}
	state->next = [state, call] {
		if (!state->prepared && state->taskIndex >= int(state->tasks.size())) {
			state->prepared = true;
			ranges::sort(
				state->hits,
				[](const SearchHit &a, const SearchHit &b) {
					if (a.date != b.date) {
						return a.date > b.date;
					}
					return a.id.msg.bare > b.id.msg.bare;
				});
			if (int(state->hits.size()) > state->limit) {
				state->hits.resize(state->limit);
			}
			if (state->context > 0 && !state->countsMode) {
				const auto count = std::min(int(state->hits.size()), 10);
				for (auto i = 0; i < count; ++i) {
					const auto hitIndex = i;
					const auto &hit = state->hits[hitIndex];
					const auto peer = call->session->data().peerLoaded(
						hit.id.peer);
					if (!peer) {
						continue;
					}
					const auto bareId = hit.id.msg.bare;
					state->tasks.push_back([=](Fn<void()> done) {
						auto before = QJsonArray();
						auto after = QJsonArray();
						auto loaded = true;
						const auto range = state->context;
						for (auto j = -range; j <= range; ++j) {
							if (j == 0) {
								continue;
							}
							const auto neighborId = bareId + j;
							if (neighborId <= 0) {
								continue;
							}
							const auto item = call->session->data().message(
								peer,
								MsgId(neighborId));
							if (!item) {
								loaded = false;
								break;
							}
							(j < 0 ? before : after).push_back(
								SerializeNeighbor(item));
						}
						if (loaded) {
							auto &target = state->hits[hitIndex];
							target.json.insert("before", before);
							target.json.insert("after", after);
							done();
							return;
						}
						auto &histories = call->session->data().histories();
						const auto history = call->session->data().history(
							peer);
						const auto requestId = histories.sendRequest(
							history,
							Data::Histories::RequestType::History,
							[=](Fn<void()> finishRequest) {
								return call->api.request(MTPmessages_GetHistory(
									peer->input(),
									MTP_int(bareId),
									MTP_int(0),
									MTP_int(-state->context),
									MTP_int(2 * state->context + 1),
									MTP_int(0),
									MTP_int(0),
									MTP_long(0)
								)).done([=](
										const MTPmessages_Messages &result) {
									finishRequest();
									const auto parsed = ParseMessagesSlice(
										call->session,
										result);
									auto before = QJsonArray();
									auto after = QJsonArray();
									for (auto k = parsed.items.rbegin();
										k != parsed.items.rend(); ++k) {
										const auto item = *k;
										if (item->id.bare == bareId) {
											continue;
										}
										(item->id.bare < bareId
											? before
											: after
										).push_back(SerializeNeighbor(item));
									}
									auto &target = state->hits[hitIndex];
									target.json.insert("before", before);
									target.json.insert("after", after);
									done();
								}).fail([=](const MTP::Error &error) {
									finishRequest();
									done();
								}).send();
						});
					call->setRequestId(requestId);
					});
				}
			}
		}
		if (state->taskIndex >= int(state->tasks.size())) {
			DeliverSearchResult(call, state);
			return;
		}
		const auto task = state->tasks[state->taskIndex++];
		task([state] { state->next(); });
	};
	state->next();
}

void RunGetPeerInfo(not_null<PendingCall*> call) {
	OnFullPeer(call, [call] {
		call->done(BuildPeerInfoJson(call->peer, call->gate));
	});
}

[[nodiscard]] QString MembersFilterName(
		const QJsonObject &input,
		QString &error) {
	auto name = input.value("filter").toString().trimmed().toLower();
	if (name.isEmpty()) {
		return u"recent"_q;
	}
	const auto kFilters = {
		u"recent"_q,
		u"admins"_q,
		u"banned"_q,
		u"restricted"_q,
		u"bots"_q,
	};
	if (!ranges::contains(kFilters, name)) {
		error = u"Unknown filter: %1. The filters are: recent, admins,"
			u" banned, restricted, bots."_q.arg(name);
	}
	return name;
}

void RunListMembers(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	auto error = QString();
	const auto filterName = MembersFilterName(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto recent = (filterName == u"recent"_q);
	if (const auto user = peer->asUser()) {
		if (!recent) {
			call->fail(
				u"Only supergroups and channels have this list."_q);
			return;
		}
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
		if (!recent && filterName != u"admins"_q) {
			call->fail(
				u"Only supergroups and channels have this list."_q);
			return;
		}
		const auto deliver = (filterName == u"admins"_q)
			? DeliverBasicGroupAdmins
			: DeliverBasicGroupMembers;
		if (chat->participants.empty() && !peer->isFullLoaded()) {
			OnFullPeer(call, [call, deliver] {
				deliver(call);
			});
		} else {
			deliver(call);
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
	auto filter = MTPChannelParticipantsFilter();
	if (filterName == u"admins"_q) {
		filter = MTP_channelParticipantsAdmins();
	} else if (filterName == u"banned"_q) {
		filter = MTP_channelParticipantsKicked(MTP_string(query));
	} else if (filterName == u"restricted"_q) {
		filter = MTP_channelParticipantsBanned(MTP_string(query));
	} else if (filterName == u"bots"_q) {
		filter = MTP_channelParticipantsBots();
	} else if (query.isEmpty()) {
		filter = MTP_channelParticipantsRecent();
	} else {
		filter = MTP_channelParticipantsSearch(MTP_string(query));
	}
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
			members.push_back(MemberJsonWithRights(user, participant));
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
	historyProperties.insert("from_date", StringProperty(
		u"Read messages sent at or after this local time, YYYY-MM-DD"
		u" or YYYY-MM-DD HH:MM, instead of around_id, before_id or"
		u" after_id. Page newer messages with after_id."_q));
	auto idsProperty = QJsonObject();
	idsProperty.insert("type", QJsonValue(u"array"_q));
	idsProperty.insert("description", QJsonValue(QString(
		u"Read these message ids in full instead of paging (1 to %1"
			u" ids); missing ids come back in \"missing\". Cannot be"
			u" combined with other parameters."_q).arg(kMaxReadIds)));
	idsProperty.insert("items", MessageIdProperty(u"Message id."_q));
	idsProperty.insert("minItems", QJsonValue(1));
	idsProperty.insert("maxItems", QJsonValue(kMaxReadIds));
	historyProperties.insert("ids", idsProperty);
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
	searchProperties.insert("queries", SearchQueriesProperty());
	searchProperties.insert("media", SearchMediaProperty());
	searchProperties.insert("chat_types", SearchChatTypesProperty());
	searchProperties.insert("from", StringProperty(
		u"\"me\", a numeric user id or @username, resolved among"
		" locally known users only. Needs exactly one chat."_q));
	searchProperties.insert("min_date", StringProperty(
		u"Only messages sent on or after this local date YYYY-MM-DD."_q));
	searchProperties.insert("max_date", StringProperty(
		u"Only messages sent on or before this local date YYYY-MM-DD."_q));
	searchProperties.insert("limit", IntProperty(
		1,
		50,
		u"How many hits to return, 1..50, default 20."_q));
	searchProperties.insert("offset_id", MessageIdProperty(
		u"Pagination in one chat: search older than this message id."
		u" Needs exactly one chat and one query."_q));
	searchProperties.insert("offset", StringProperty(
		u"Pagination for the all-chats search: pass the next_offset"
		" string from the previous page here. Only valid when no"
		" chat is given and there is a single query."_q));
	searchProperties.insert("topic_id", MessageIdProperty(
		u"Forum topic root message id. Needs exactly one chat."_q));
	searchProperties.insert("context", IntProperty(
		0,
		2,
		u"How many neighbor messages to show around each of the"
		u" first 10 hits, 0..2, default 0."_q));
	searchProperties.insert("mode", SearchModeProperty());
	auto membersProperties = QJsonObject();
	membersProperties.insert("filter", StringProperty(
		u"Which members to list: recent (default), admins, banned"
		u" (removed and banned), restricted or bots. Banned and"
		u" restricted need a supergroup or channel."_q));
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
		u"Get the user's account and a summary of this chat. The [Context]"
		" line of every message already gives the local date and time."_q,
		u"Get the user's account: id, name and username. The [Context] line"
		" of every message already gives the local date and time."_q,
		ToolSchema(std::move(empty), QJsonArray()),
		ChatParam::None,
		RunGetContext);
	push(
		u"read_history"_q,
		u"Read messages of this chat, oldest to newest: the latest ones, a"
		" window with around_id, before_id or after_id, a period with"
		" from_date (YYYY-MM-DD or YYYY-MM-DD HH:MM, then page on with"
		" after_id), or ids to read those messages in full. Long texts are"
		" cut in pages of history; read them by ids. Use it to summarize a"
		" period or see the context of a message; to find messages by words"
		" use search_messages. Reading never marks anything read."_q,
		u"Read messages of a chat, oldest to newest: the latest ones, a"
		" window with around_id, before_id or after_id, a period with"
		" from_date (YYYY-MM-DD or YYYY-MM-DD HH:MM, then page on with"
		" after_id), or ids to read those messages in full. Long texts are"
		" cut in pages of history; read them by ids. Use it to summarize a"
		" period or see the context of a message; to find messages by words"
		" use search_messages. Reading never marks anything read."_q,
		ToolSchema(std::move(historyProperties), QJsonArray()),
		ChatParam::One,
		RunReadHistory);
	push(
		u"search_messages"_q,
		u"Find messages in this chat and get short hits (id, date, sender,"
		" media, a snippet around the match); read full texts with"
		" read_history ids. Telegram matches whole words and their forms,"
		" not parts of words: \"ivanova\" doesn't find"
		" \"ivanovaanna@mail.ru\", so search an e-mail by its domain"
		" (\"mail.ru\") and add the Latin spelling of names and logins to"
		" queries. Give up to 5 queries in one call instead of one call per"
		" spelling. With media (photo, video, document, voice, round_video,"
		" url ...) or dates the word can be left out, for example to find"
		" pictures without text. After 2-3 empty searches change the"
		" approach: other words, a media filter, or read the history around"
		" a found mention. Example: {\"queries\": [\"почта\", \"mail.ru\"],"
		" \"min_date\": \"2025-09-01\"}."_q,
		u"Find messages in the given chats (chats) or in all chats and get"
		" short hits (chat, id, date, sender, media, a snippet around the"
		" match); read full texts with read_history ids. Search the chats"
		" the user named before all chats; chat_types narrows all chats to"
		" private, group, channel or bot chats; mode counts tells how many"
		" matches each chat has. Telegram matches whole words and their"
		" forms, not parts of words: \"ivanova\" doesn't find"
		" \"ivanovaanna@mail.ru\", so search an e-mail by its domain"
		" (\"mail.ru\") and add the Latin spelling of names and logins to"
		" queries. Give up to 5 queries in one call instead of one call per"
		" spelling. With media (photo, video, document, voice, round_video,"
		" url ...) or dates the word can be left out, for example to find"
		" pictures without text. After 2-3 empty searches change the"
		" approach: other words, other chats, a media filter, or read the"
		" history around a found mention. Example: {\"chats\":"
		" [\"@some_group\"], \"queries\": [\"почта\", \"mail.ru\"]}."_q,
		ToolSchema(
			std::move(searchProperties),
			QJsonArray()),
		ChatParam::OptionalMany,
		RunSearchMessages);
	push(
		u"get_peer_info"_q,
		u"Get details of this chat: title, kind, description or bio, members"
		" count, the user's role and rights."_q,
		u"Get details of a chat or a person: title or name, kind, username,"
		" description or bio, members count, the user's role and rights. To"
		" find a chat by its title use list_chats with query."_q,
		ToolSchema(QJsonObject(), QJsonArray()),
		ChatParam::One,
		RunGetPeerInfo);
	push(
		u"list_members"_q,
		u"List members of this chat with names, usernames and roles."
		u" Use the filter admins, banned, restricted or bots for those"
		u" lists."_q,
		u"List members of the chat with names, usernames and roles."
		u" Use the filter admins, banned, restricted or bots for those"
		u" lists."_q,
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
