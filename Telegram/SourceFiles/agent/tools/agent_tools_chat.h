/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_prompts.h"
#include "agent/core/agent_policy.h"
#include "agent/core/agent_types.h"

#include <rpl/lifetime.h>

#include "mtproto/core_types.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonObject>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace MTP {
class Sender;
} // namespace MTP

namespace Agent::ChatTools {

struct PendingCall final {
	explicit PendingCall(not_null<Main::Session*> session);

	not_null<Main::Session*> session;
	MTP::Sender api;
	rpl::lifetime lifetime;
	PeerData *peer = nullptr;
	QJsonObject input;
	Fn<void(ToolResultBlock)> result;
	Fn<void(QJsonObject)> done;
	Fn<void(QString)> fail;
	mtpRequestId historiesRequestId = 0;

	void setRequestId(mtpRequestId id);
	void cancel();

};

void RunGetContext(not_null<PendingCall*> call);
void RunReadHistory(not_null<PendingCall*> call);
void RunSearchMessages(not_null<PendingCall*> call);
void RunGetPeerInfo(not_null<PendingCall*> call);
void RunListMembers(not_null<PendingCall*> call);

} // namespace Agent::ChatTools
