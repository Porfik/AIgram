/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/tools/agent_tool_table.h"

namespace Agent::ChatTools {

void RunGetContext(not_null<PendingCall*> call);
void RunReadHistory(not_null<PendingCall*> call);
void RunSearchMessages(not_null<PendingCall*> call);
void RunGetPeerInfo(not_null<PendingCall*> call);
void RunListMembers(not_null<PendingCall*> call);

} // namespace Agent::ChatTools
