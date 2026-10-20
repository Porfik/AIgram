/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_chat_state.h"

namespace Agent::Tests {

std::vector<Check> ChatStateChecks() {
	return {
		{ u"chat_state_round_trip"_q, [] {
			const auto state = ChatState{
				.pinned = false,
				.pinnedIndex = 3,
				.since = 1760000000,
			};
			const auto restored = DeserializeChatState(
				SerializeChatState(state));
			if (!restored) {
				return u"failed to deserialize"_q;
			} else if (!(*restored == state)) {
				return u"the state must round-trip"_q;
			}
			return QString();
		} },
		{ u"chat_state_defaults_pinned_first"_q, [] {
			const auto state = ChatState();
			if (!state.pinned || state.pinnedIndex != 0) {
				return u"a new agent chat is pinned on top"_q;
			}
			return QString();
		} },
		{ u"chat_state_rejects_garbage"_q, [] {
			if (DeserializeChatState(QByteArray())) {
				return u"empty bytes must not parse"_q;
			}
			auto bytes = SerializeChatState(ChatState());
			bytes.append('x');
			if (DeserializeChatState(bytes)) {
				return u"trailing bytes must not parse"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
