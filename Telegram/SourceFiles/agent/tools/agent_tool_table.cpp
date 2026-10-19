/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "data/data_histories.h"
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
	return nullptr;
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
