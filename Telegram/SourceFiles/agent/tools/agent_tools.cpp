/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tools.h"

namespace Agent {

ToolRegistry::ToolRegistry(
	not_null<Main::Session*> session,
	not_null<PolicyGate*> gate)
: _session(session)
, _gate(gate) {
}

ToolRegistry::~ToolRegistry() = default;

std::vector<ToolSpec> ToolRegistry::tools() const {
	return {};
}

void ToolRegistry::execute(
		ToolUseBlock call,
		Fn<void(ToolResultBlock)> done) {
	done(ToolResultBlock{
		.toolUseId = call.id,
		.content = u"Unknown tool."_q,
		.error = true,
	});
}

void ToolRegistry::cancel(const QString &toolUseId) {
}

rpl::producer<PeerKey> ToolRegistry::touched() const {
	return _touched.events();
}

ChatBrief MakeChatBrief(not_null<PeerData*> peer) {
	return {};
}

std::vector<PeerName> NameCandidates(not_null<Main::Session*> session) {
	return {};
}

QString AccountLine(not_null<Main::Session*> session) {
	return QString();
}

} // namespace Agent
