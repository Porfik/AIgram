/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/timer.h"
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>

#include <limits>

namespace Agent {
namespace {

constexpr auto kTargetGap = crl::time(300);
constexpr auto kMaxMessageIds = 100;

void RunNextTarget(
		base::weak_ptr<PendingCall> weak,
		std::shared_ptr<Card> card,
		int index,
		Fn<void(const CardTarget &target, Fn<void(QString error)> done)> each,
		Fn<void(Card card)> finished) {
	const auto call = weak.get();
	if (!call) {
		return;
	}
	auto &targets = card->targets;
	while (index < int(targets.size())
		&& (!targets[index].checked || !targets[index].error.isEmpty())) {
		++index;
	}
	if (index == int(targets.size())) {
		card->status = TargetsStatus(*card);
		finished(std::move(*card));
		return;
	}
	each(targets[index], [=](QString error) {
		if (!weak) {
			return;
		}
		auto &targets = card->targets;
		targets[index].error = error;
		const auto stop = error.contains(u"FLOOD_WAIT"_q)
			|| error.contains(u"PEER_FLOOD"_q);
		if (stop) {
			for (auto i = index + 1; i < int(targets.size()); ++i) {
				if (targets[i].checked && targets[i].error.isEmpty()) {
					targets[i].error = u"Not done: Telegram limited"
						" this account for now. "_q + error;
				}
			}
		}
		base::call_delayed(kTargetGap, weak.get(), [=] {
			RunNextTarget(weak, card, index + 1, each, finished);
		});
	});
}

} // namespace

PendingCall::PendingCall(not_null<Main::Session*> session)
: session(session)
, api(&session->mtp()) {
}

void PendingCall::setRequestId(mtpRequestId id) {
	historiesRequestId = id;
}

void PendingCall::cancel() {
	if (historiesRequestId) {
		session->data().histories().cancelRequest(
			base::take(historiesRequestId));
	}
}

PeerData *ResolveChatRef(
		not_null<Main::Session*> session,
		const QJsonValue &value) {
	if (value.isDouble()) {
		return session->data().peerLoaded(
			PeerId(uint64(int64(value.toDouble()))));
	} else if (!value.isString()) {
		return nullptr;
	}
	auto text = value.toString().trimmed();
	if (text.isEmpty()) {
		return nullptr;
	}
	const auto lower = text.toLower();
	if (lower == u"me"_q || lower == u"@me"_q || lower == u"self"_q) {
		return session->user().get();
	}
	auto ok = false;
	const auto number = text.toLongLong(&ok);
	if (ok) {
		return session->data().peerLoaded(PeerId(uint64(number)));
	}
	const auto username = ChatRefUsername(value);
	return username.isEmpty()
		? nullptr
		: session->data().peerByUsername(username);
}

QString ChatRefUsername(const QJsonValue &value) {
	if (!value.isString()) {
		return QString();
	}
	auto text = value.toString().trimmed();
	auto lower = text.toLower();
	if (lower.startsWith(u"https://"_q)) {
		text = text.mid(8);
	} else if (lower.startsWith(u"http://"_q)) {
		text = text.mid(7);
	}
	lower = text.toLower();
	if (lower.startsWith(u"telegram.me/"_q)) {
		text = text.mid(12);
	} else if (lower.startsWith(u"t.me/"_q)) {
		text = text.mid(5);
	}
	if (text.startsWith('@')) {
		text = text.mid(1);
	}
	const auto slash = text.indexOf('/');
	if (slash > 0) {
		text = text.left(slash);
	}
	static const auto valid = QRegularExpression(
		u"^[A-Za-z][A-Za-z0-9_]{3,31}$"_q);
	return valid.match(text).hasMatch() ? text : QString();
}

std::vector<not_null<UserData*>> ResolveUserRefs(
		not_null<PendingCall*> call,
		const QJsonValue &value,
		int max,
		QString &error) {
	const auto session = call->session;
	if (!value.isArray()) {
		error = u"users must be an array of user ids or @usernames."_q;
		return {};
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > max) {
		error = u"users must contain 1 to %1 entries."_q.arg(max);
		return {};
	}
	auto result = std::vector<not_null<UserData*>>();
	for (const auto &item : list) {
		const auto peer = ResolveChatRef(session, item);
		const auto user = peer ? peer->asUser() : nullptr;
		if (!user) {
			error = u"Unknown user: %1. Use a user id from an earlier"
				" tool result (members, messages) or an @username."_q.arg(
					item.isDouble()
						? QString::number(qint64(item.toDouble()))
						: item.toString().left(64));
			return {};
		} else if (!user->isSelf()
			&& session->agent().exclusions().excluded(user->id.value)) {
			error = u"The user hid the chat with %1 from the agent. Tell the"
				" user you can't act on this person and don't try to reach"
				" them another way."_q.arg(user->name());
			return {};
		} else if (!ranges::contains(result, not_null(user))) {
			result.push_back(user);
		}
	}
	return result;
}

QString PeerTitle(not_null<PeerData*> peer) {
	return peer->isSelf() ? tr::lng_saved_messages(tr::now) : peer->name();
}

CardTarget PeerTarget(not_null<PeerData*> peer) {
	const auto username = peer->username();
	return CardTarget{
		.peer = peer->id.value,
		.title = PeerTitle(peer),
		.preview = username.isEmpty() ? QString() : (u"@"_q + username),
	};
}

std::optional<TimeId> ParseLocalTime(
		const QJsonValue &value,
		QString &error) {
	const auto text = value.toString().trimmed();
	const auto parsed = QDateTime::fromString(text, u"yyyy-MM-dd HH:mm"_q);
	if (!parsed.isValid()) {
		error = u"Expected a local date and time as YYYY-MM-DD HH:MM,"
			" got: %1."_q.arg(text.left(64));
		return std::nullopt;
	}
	return TimeId(parsed.toSecsSinceEpoch());
}

QString MtpErrorText(const MTP::Error &error) {
	const auto type = error.type();
	if (type.startsWith(u"FLOOD_WAIT_"_q)) {
		return u"Telegram asks to wait %1 seconds before trying again"
			" (%2)."_q.arg(type.mid(11), type);
	} else if (type == u"PEER_FLOOD"_q) {
		return u"Telegram limits this account for such actions right now"
			" (PEER_FLOOD)."_q;
	} else if (type == u"CHAT_ADMIN_REQUIRED"_q) {
		return u"You don't have the admin right needed for this"
			" (CHAT_ADMIN_REQUIRED)."_q;
	} else if (type == u"RIGHT_FORBIDDEN"_q) {
		return u"You can't give or change these rights"
			" (RIGHT_FORBIDDEN)."_q;
	} else if (type == u"USER_PRIVACY_RESTRICTED"_q) {
		return u"The user's privacy settings don't allow this"
			" (USER_PRIVACY_RESTRICTED)."_q;
	} else if (type == u"USER_NOT_MUTUAL_CONTACT"_q) {
		return u"The user can only be added by a mutual contact"
			" (USER_NOT_MUTUAL_CONTACT)."_q;
	} else if (type == u"USER_NOT_PARTICIPANT"_q) {
		return u"The user is not a member of this chat"
			" (USER_NOT_PARTICIPANT)."_q;
	} else if (type == u"PREMIUM_ACCOUNT_REQUIRED"_q) {
		return u"This needs Telegram Premium"
			" (PREMIUM_ACCOUNT_REQUIRED)."_q;
	}
	return type;
}

void WhenFullLoaded(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		Fn<void()> ready) {
	constexpr auto kTimeout = crl::time(5000);
	if (peer->isFullLoaded()) {
		ready();
		return;
	}
	const auto once = call->lifetime.make_state<bool>(false);
	const auto trigger = [=] {
		if (!*once) {
			*once = true;
			ready();
		}
	};
	peer->session().changes().peerUpdates(
		peer,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::on_next([=](const Data::PeerUpdate &) {
		trigger();
	}, call->lifetime);
	call->lifetime.make_state<base::Timer>(trigger)->callOnce(kTimeout);
	peer->session().api().requestFullPeer(peer);
}

void ForEachTarget(
		not_null<PendingCall*> call,
		Card card,
		Fn<void(const CardTarget &target, Fn<void(QString error)> done)> each,
		Fn<void(Card card)> finished) {
	RunNextTarget(
		base::make_weak(call.get()),
		std::make_shared<Card>(std::move(card)),
		0,
		std::move(each),
		std::move(finished));
}

QJsonObject ToolSchema(QJsonObject properties, QJsonArray required) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"object"_q));
	result.insert("properties", properties);
	result.insert("required", required);
	result.insert("additionalProperties", QJsonValue(false));
	return result;
}

QJsonObject StringProperty(QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"string"_q));
	result.insert("description", description);
	return result;
}

QJsonObject BoolProperty(QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"boolean"_q));
	result.insert("description", description);
	return result;
}

QJsonObject IntProperty(int minimum, int maximum, QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"integer"_q));
	result.insert("description", description);
	result.insert("minimum", QJsonValue(minimum));
	result.insert("maximum", QJsonValue(maximum));
	return result;
}

QJsonObject MessageIdProperty(QString description) {
	return IntProperty(
		1,
		std::numeric_limits<int>::max(),
		std::move(description));
}

QJsonObject MessageIdsProperty(QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"array"_q));
	result.insert("description", description);
	result.insert("items", MessageIdProperty(u"Message id."_q));
	result.insert("minItems", QJsonValue(1));
	result.insert("maxItems", QJsonValue(kMaxMessageIds));
	return result;
}

std::vector<MsgId> ParseMessageIds(
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

QJsonObject UsersProperty(int max, QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"array"_q));
	result.insert("description", description);
	result.insert("items", StringProperty(
		u"A user id from an earlier tool result or an @username."_q));
	result.insert("minItems", QJsonValue(1));
	result.insert("maxItems", QJsonValue(max));
	return result;
}

} // namespace Agent
