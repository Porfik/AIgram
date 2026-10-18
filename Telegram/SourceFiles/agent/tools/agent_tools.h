/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_engine.h"
#include "agent/core/agent_policy.h"
#include "agent/core/agent_prompts.h"

#include <rpl/event_stream.h>

#include <vector>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class ToolRegistry final : public ToolExecutor {
public:
	ToolRegistry(
		not_null<Main::Session*> session,
		not_null<PolicyGate*> gate);
	~ToolRegistry();

	[[nodiscard]] std::vector<ToolSpec> tools() const override;
	void execute(
		ToolUseBlock call,
		Fn<void(ToolResultBlock)> done) override;
	void cancel(const QString &toolUseId) override;

	[[nodiscard]] rpl::producer<PeerKey> touched() const;

private:
	const not_null<Main::Session*> _session;
	const not_null<PolicyGate*> _gate;
	rpl::event_stream<PeerKey> _touched;

};

[[nodiscard]] ChatBrief MakeChatBrief(not_null<PeerData*> peer);
[[nodiscard]] std::vector<PeerName> NameCandidates(
	not_null<Main::Session*> session);
[[nodiscard]] QString AccountLine(not_null<Main::Session*> session);

} // namespace Agent
