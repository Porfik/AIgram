/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "data/data_histories.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <limits>

namespace Agent {

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
	auto ok = false;
	const auto number = text.toLongLong(&ok);
	if (ok) {
		return session->data().peerLoaded(PeerId(uint64(number)));
	}
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
	return session->data().peerByUsername(text);
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
	result.insert("maxItems", QJsonValue(100));
	return result;
}

} // namespace Agent
