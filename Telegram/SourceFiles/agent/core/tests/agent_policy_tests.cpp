/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_policy.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] PeerName Candidate(
		PeerKey peer,
		QString title,
		QString username = {}) {
	return { peer, std::move(title), std::move(username) };
}

[[nodiscard]] ToolPolicy Policy(
		Risk risk,
		bool general = true,
		bool chat = true,
		bool outbound = false) {
	return { risk, general, chat, outbound };
}

[[nodiscard]] PolicyGate FrameGate(
		Scope scope,
		std::vector<QString> tools,
		std::vector<PeerKey> chats,
		Fn<bool(PeerKey)> excluded = nullptr) {
	return PolicyGate(scope, std::move(excluded), true, GateFrame{
		.tools = std::move(tools),
		.contains = [=](PeerKey peer) {
			return std::find(chats.begin(), chats.end(), peer)
				!= chats.end();
		},
	});
}

[[nodiscard]] QString PeerKeys(const std::vector<PeerKey> &peers) {
	auto result = QString();
	for (const auto peer : peers) {
		if (!result.isEmpty()) {
			result += u", "_q;
		}
		result += QString::number(peer);
	}
	return result;
}

[[nodiscard]] QString ExpectPeers(
		const std::vector<PeerKey> &expected,
		const std::vector<PeerKey> &found) {
	if (expected == found) {
		return QString();
	}
	return u"found ["_q + PeerKeys(found) + u"], expected ["_q
		+ PeerKeys(expected) + u"]"_q;
}

[[nodiscard]] QString ExpectDecision(
		const Decision &decision,
		Verdict verdict,
		DenyReason reason = DenyReason::None,
		bool canAllow = false) {
	if (decision.verdict != verdict
		|| decision.reason != reason
		|| decision.canAllowForTurn != canAllow) {
		return u"verdict "_q + QString::number(int(decision.verdict))
			+ u", reason "_q + QString::number(int(decision.reason))
			+ u", canAllow "_q
			+ (decision.canAllowForTurn ? u"true"_q : u"false"_q);
	}
	return QString();
}

[[nodiscard]] ActionRequest Request(
		QString tool,
		ToolPolicy policy,
		std::vector<PeerKey> peers = {}) {
	return { std::move(tool), std::move(policy), std::move(peers) };
}

} // namespace

std::vector<Check> PolicyChecks() {
	return {
		{ u"policy_username_mention"_q, [] {
			const auto found = FindNamedPeers(
				u"ping @durov and @DUROV!"_q,
				{ Candidate(1, u"Durov"_q, u"durov"_q) });
			return ExpectPeers({ 1 }, found);
		} },
		{ u"policy_username_boundaries"_q, [] {
			const auto found = FindNamedPeers(
				u"mail@durov to @durovsky, dear @durov."_q,
				{ Candidate(1, QString(), u"durov"_q) });
			return ExpectPeers({ 1 }, found);
		} },
		{ u"policy_username_links"_q, [] {
			const auto found = FindNamedPeers(
				u"t.me/durov, https://telegram.me/durov"
				u" and http://t.me/durov?"_q,
				{ Candidate(1, QString(), u"durov"_q) });
			if (!ExpectPeers({ 1 }, found).isEmpty()) {
				return u"links: "_q + ExpectPeers({ 1 }, found);
			}
			const auto partial = FindNamedPeers(
				u"not t.me/durovsky or telegram.me/durovxx"_q,
				{ Candidate(1, QString(), u"durov"_q) });
			return ExpectPeers({}, partial);
		} },
		{ u"policy_username_needs_no_title"_q, [] {
			const auto found = FindNamedPeers(
				u"@first"_q,
				{ Candidate(1, u"The First One"_q, u"first"_q) });
			return ExpectPeers({ 1 }, found);
		} },
		{ u"policy_title_case_insensitive"_q, [] {
			const auto found = FindNamedPeers(
				u"ask Max, then ask max again"_q,
				{ Candidate(1, u"Max"_q) });
			return ExpectPeers({ 1 }, found);
		} },
		{ u"policy_title_inside_word"_q, [] {
			const auto found = FindNamedPeers(
				u"Maximum and Maxine"_q,
				{ Candidate(1, u"Max"_q) });
			return ExpectPeers({}, found);
		} },
		{ u"policy_title_cyrillic_punctuation"_q, [] {
			const auto found = FindNamedPeers(
				u"написал МАКС, а не Максимальный"_q,
				{ Candidate(1, u"Макс"_q) });
			return ExpectPeers({ 1 }, found);
		} },
		{ u"policy_title_short_ignored"_q, [] {
			const auto found = FindNamedPeers(
				u"Jo and Bo here"_q,
				{ Candidate(1, u" Bo "_q) });
			return ExpectPeers({}, found);
		} },
		{ u"policy_candidate_order_and_duplicates"_q, [] {
			const auto found = FindNamedPeers(
				u"Alpha, beta and @gamma"_q,
				{
					Candidate(2, u"Alpha"_q),
					Candidate(1, u"Beta"_q),
					Candidate(2, QString(), u"gamma"_q),
				});
			return ExpectPeers({ 2, 1 }, found);
		} },
		{ u"policy_no_match"_q, [] {
			const auto found = FindNamedPeers(
				u"nothing here at all"_q,
				{
					Candidate(1, u"Zeta"_q),
					Candidate(2, QString(), u"zeta"_q),
				});
			return ExpectPeers({}, found);
		} },
		{ u"policy_available_by_mode"_q, [] {
			auto account = PolicyGate(Scope::Account());
			if (!account.available(Policy(Risk::Read, true, true))) {
				return u"account scope lost a general tool"_q;
			} else if (account.available(Policy(Risk::Read, false, true))) {
				return u"account scope got a chat-only tool"_q;
			}
			auto chat = PolicyGate(Scope::Chat(5));
			if (chat.available(Policy(Risk::Read, true, false))) {
				return u"chat scope got a general-only tool"_q;
			} else if (!chat.available(Policy(Risk::Read, false, true))) {
				return u"chat scope lost a chat tool"_q;
			}
			return QString();
		} },
		{ u"policy_visible_by_scope_and_exclusion"_q, [] {
			const auto excluded = [](PeerKey peer) {
				return peer == 7;
			};
			auto account = PolicyGate(Scope::Account(), excluded);
			if (account.visible(0) || account.visible(7) || !account.visible(5)) {
				return u"account scope visibility differs"_q;
			}
			auto chat = PolicyGate(Scope::Chat(5), excluded);
			if (!chat.visible(5) || chat.visible(6) || chat.visible(0)
				|| chat.visible(7)) {
				return u"chat scope visibility differs"_q;
			}
			return QString();
		} },
		{ u"policy_deny_not_in_mode"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			return ExpectDecision(
				gate.check(Request(
					u"send"_q,
					Policy(Risk::Read, false, true),
					{ 5 })),
				Verdict::Deny,
				DenyReason::NotInMode);
		} },
		{ u"policy_deny_forbidden"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			return ExpectDecision(
				gate.check(Request(u"wipe"_q, Policy(Risk::Forbidden), { 5 })),
				Verdict::Deny,
				DenyReason::Forbidden);
		} },
		{ u"policy_forbidden_not_in_mode_reports_not_in_mode"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			return ExpectDecision(
				gate.check(Request(
					u"wipe"_q,
					Policy(Risk::Forbidden, false, true),
					{ 5 })),
				Verdict::Deny,
				DenyReason::NotInMode);
		} },
		{ u"policy_deny_excluded"_q, [] {
			auto gate = PolicyGate(
				Scope::Account(),
				[](PeerKey peer) { return peer == 7; });
			return ExpectDecision(
				gate.check(Request(
					u"read_history"_q,
					Policy(Risk::Read),
					{ 5, 7 })),
				Verdict::Deny,
				DenyReason::Excluded);
		} },
		{ u"policy_deny_out_of_scope"_q, [] {
			auto chat = PolicyGate(Scope::Chat(5));
			const auto other = chat.check(
				Request(u"read_history"_q, Policy(Risk::Read), { 6 }));
			const auto zero = chat.check(
				Request(u"read_history"_q, Policy(Risk::Read), { 0 }));
			auto account = PolicyGate(Scope::Account());
			const auto zeroAccount = account.check(
				Request(u"read_history"_q, Policy(Risk::Read), { 0 }));
			if (const auto diff = ExpectDecision(
					other,
					Verdict::Deny,
					DenyReason::OutOfScope);
				!diff.isEmpty()) {
				return u"other peer: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					zero,
					Verdict::Deny,
					DenyReason::OutOfScope);
				!diff.isEmpty()) {
				return u"zero peer in chat: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					zeroAccount,
					Verdict::Deny,
					DenyReason::OutOfScope);
				!diff.isEmpty()) {
				return u"zero peer in account: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_read_and_reversible_allowed"_q, [] {
			auto chat = PolicyGate(Scope::Chat(5));
			const auto read = chat.check(
				Request(u"read_history"_q, Policy(Risk::Read), { 5 }));
			const auto reversible = chat.check(
				Request(u"mark_read"_q, Policy(Risk::Reversible), { 5 }));
			if (const auto diff = ExpectDecision(read, Verdict::Allow);
				!diff.isEmpty()) {
				return u"read: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					reversible,
					Verdict::Allow);
				!diff.isEmpty()) {
				return u"reversible: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_visible_confirms"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			return ExpectDecision(
				gate.check(Request(u"send"_q, Policy(Risk::Visible), { 5 })),
				Verdict::Confirm,
				DenyReason::None,
				true);
		} },
		{ u"policy_allowance_covers_subset"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			gate.allowForTurn(
				Request(u"send"_q, Policy(Risk::Visible), { 5 }));
			const auto same = gate.check(
				Request(u"send"_q, Policy(Risk::Visible), { 5 }));
			const auto extra = gate.check(
				Request(u"send"_q, Policy(Risk::Visible), { 5, 6 }));
			const auto otherTool = gate.check(
				Request(u"forward"_q, Policy(Risk::Visible), { 5 }));
			if (const auto diff = ExpectDecision(same, Verdict::Allow);
				!diff.isEmpty()) {
				return u"same peers: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					extra,
					Verdict::Confirm,
					DenyReason::None,
					true);
				!diff.isEmpty()) {
				return u"extra peer: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					otherTool,
					Verdict::Confirm,
					DenyReason::None,
					true);
				!diff.isEmpty()) {
				return u"other tool: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_allowance_grows"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			gate.allowForTurn(
				Request(u"send"_q, Policy(Risk::Visible), { 5 }));
			gate.allowForTurn(
				Request(u"send"_q, Policy(Risk::Visible), { 5, 6 }));
			const auto both = gate.check(
				Request(u"send"_q, Policy(Risk::Visible), { 5, 6 }));
			return ExpectDecision(both, Verdict::Allow);
		} },
		{ u"policy_allowance_empty_peers"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			gate.allowForTurn(
				Request(u"profile"_q, Policy(Risk::Visible), {}));
			const auto peers = gate.check(
				Request(u"profile"_q, Policy(Risk::Visible), { 9 }));
			const auto none = gate.check(
				Request(u"profile"_q, Policy(Risk::Visible), {}));
			gate.allowForTurn(
				Request(u"send"_q, Policy(Risk::Visible), { 5 }));
			const auto empty = gate.check(
				Request(u"send"_q, Policy(Risk::Visible), {}));
			const auto otherTool = gate.check(
				Request(u"forward"_q, Policy(Risk::Visible), {}));
			if (const auto diff = ExpectDecision(
					peers,
					Verdict::Confirm,
					DenyReason::None,
					true);
				!diff.isEmpty()) {
				return u"empty allowance, peers asked: "_q + diff;
			} else if (const auto diff = ExpectDecision(none, Verdict::Allow);
				!diff.isEmpty()) {
				return u"empty allowance, no peers: "_q + diff;
			} else if (const auto diff = ExpectDecision(empty, Verdict::Allow);
				!diff.isEmpty()) {
				return u"peer allowance, no peers: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					otherTool,
					Verdict::Confirm,
					DenyReason::None,
					true);
				!diff.isEmpty()) {
				return u"other tool: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_begin_turn_drops_allowances"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			gate.allowForTurn(
				Request(u"send"_q, Policy(Risk::Visible), { 5 }));
			gate.beginTurn({});
			return ExpectDecision(
				gate.check(Request(u"send"_q, Policy(Risk::Visible), { 5 })),
				Verdict::Confirm,
				DenyReason::None,
				true);
		} },
		{ u"policy_taint_blocks_unnamed_outbound"_q, [] {
			auto gate = PolicyGate(Scope::Account(), nullptr, true);
			gate.beginTurn({ 4 });
			const auto unnamed = gate.check(Request(
				u"send"_q,
				Policy(Risk::Visible, true, true, true),
				{ 5 }));
			gate.allowForTurn(Request(
				u"send"_q,
				Policy(Risk::Visible, true, true, true),
				{ 5 }));
			const auto allowed = gate.check(Request(
				u"send"_q,
				Policy(Risk::Visible, true, true, true),
				{ 5 }));
			if (const auto diff = ExpectDecision(
					unnamed,
					Verdict::Confirm,
					DenyReason::None,
					false);
				!diff.isEmpty()) {
				return u"unnamed: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					allowed,
					Verdict::Confirm,
					DenyReason::None,
					false);
				!diff.isEmpty()) {
				return u"after allowance: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_taint_named_or_not_outbound"_q, [] {
			auto gate = PolicyGate(Scope::Account(), nullptr, true);
			gate.beginTurn({ 5 });
			const auto named = gate.check(Request(
				u"send"_q,
				Policy(Risk::Visible, true, true, true),
				{ 5 }));
			gate.allowForTurn(Request(
				u"send"_q,
				Policy(Risk::Visible, true, true, true),
				{ 5 }));
			const auto allowed = gate.check(Request(
				u"send"_q,
				Policy(Risk::Visible, true, true, true),
				{ 5 }));
			const auto notOutbound = gate.check(Request(
				u"react"_q,
				Policy(Risk::Visible, true, true, false),
				{ 7 }));
			if (const auto diff = ExpectDecision(
					named,
					Verdict::Confirm,
					DenyReason::None,
					true);
				!diff.isEmpty()) {
				return u"named: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					allowed,
					Verdict::Allow);
				!diff.isEmpty()) {
				return u"allowed: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					notOutbound,
					Verdict::Confirm,
					DenyReason::None,
					true);
				!diff.isEmpty()) {
				return u"not outbound: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_taint_blocks_peerless_outbound"_q, [] {
			const auto outbound = Policy(Risk::Visible, true, true, true);
			auto clean = PolicyGate(Scope::Account());
			clean.allowForTurn(Request(u"method"_q, outbound, {}));
			auto gate = PolicyGate(Scope::Account(), nullptr, true);
			gate.beginTurn({ 5 });
			const auto first = gate.check(Request(u"method"_q, outbound, {}));
			gate.allowForTurn(Request(u"method"_q, outbound, {}));
			const auto allowed = gate.check(
				Request(u"method"_q, outbound, {}));
			if (const auto diff = ExpectDecision(
					clean.check(Request(u"method"_q, outbound, {})),
					Verdict::Allow);
				!diff.isEmpty()) {
				return u"clean session: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					first,
					Verdict::Confirm,
					DenyReason::None,
					false);
				!diff.isEmpty()) {
				return u"tainted: "_q + diff;
			} else if (const auto diff = ExpectDecision(
					allowed,
					Verdict::Confirm,
					DenyReason::None,
					false);
				!diff.isEmpty()) {
				return u"tainted after allowance: "_q + diff;
			}
			return QString();
		} },
		{ u"policy_taint_survives_begin_turn"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			gate.markTainted();
			if (!gate.tainted()) {
				return u"expected the gate to be tainted"_q;
			}
			gate.beginTurn({ 4 });
			if (!gate.tainted()) {
				return u"expected taint to survive beginTurn"_q;
			}
			return ExpectDecision(
				gate.check(Request(
					u"send"_q,
					Policy(Risk::Visible, true, true, true),
					{ 5 })),
				Verdict::Confirm,
				DenyReason::None,
				false);
		} },
		{ u"policy_not_tainted_by_default"_q, [] {
			auto gate = PolicyGate(Scope::Account());
			gate.beginTurn({});
			if (gate.tainted()) {
				return u"expected a clean gate"_q;
			}
			return ExpectDecision(
				gate.check(Request(
					u"send"_q,
					Policy(Risk::Visible, true, true, true),
					{ 5 })),
				Verdict::Confirm,
				DenyReason::None,
				true);
		} },
		{ u"policy_frame_availability"_q, [] {
			const auto gate = FrameGate(Scope::Account(), {}, {});
			if (!gate.background()) {
				return u"expected a background gate"_q;
			}
			auto policy = Policy(Risk::Read);
			policy.background = false;
			if (gate.available(policy)) {
				return u"interactive-only tool available in a run"_q;
			}
			policy.background = true;
			policy.interactive = false;
			if (!gate.available(policy)) {
				return u"background-only tool missing in a run"_q;
			}
			const auto normal = PolicyGate(Scope::Account());
			if (normal.background() || normal.available(policy)) {
				return u"background-only tool available interactively"_q;
			}
			return QString();
		} },
		{ u"policy_frame_chat_task_tools"_q, [] {
			auto policy = Policy(Risk::Visible, true, false, true);
			policy.chatTask = true;
			const auto run = FrameGate(Scope::Chat(5), { u"send"_q }, { 5 });
			if (!run.available(policy)) {
				return u"a chat task run must get chatTask tools"_q;
			}
			const auto chat = PolicyGate(Scope::Chat(5));
			if (chat.available(policy)) {
				return u"chatTask must not open tools to the chat agent"_q;
			}
			policy.chatTask = false;
			if (run.available(policy)) {
				return u"general-only tools must stay out of chat runs"_q;
			}
			policy.chatTask = true;
			if (const auto error = ExpectDecision(
					run.check(Request(u"send"_q, policy, { 6 })),
					Verdict::Deny,
					DenyReason::OutOfScope);
				!error.isEmpty()) {
				return error;
			}
			return ExpectDecision(
				run.check(Request(u"send"_q, policy, { 5 })),
				Verdict::Allow);
		} },
		{ u"policy_frame_read"_q, [] {
			const auto gate = FrameGate(Scope::Account(), {}, { 5 });
			if (const auto error = ExpectDecision(
					gate.check(Request(u"read"_q, Policy(Risk::Read), { 5 })),
					Verdict::Allow);
				!error.isEmpty()) {
				return error;
			} else if (const auto error = ExpectDecision(
					gate.check(Request(u"read"_q, Policy(Risk::Read), {})),
					Verdict::Allow);
				!error.isEmpty()) {
				return error;
			}
			return ExpectDecision(
				gate.check(Request(u"read"_q, Policy(Risk::Read), { 6 })),
				Verdict::Deny,
				DenyReason::OutOfFrame);
		} },
		{ u"policy_frame_actions"_q, [] {
			const auto gate = FrameGate(
				Scope::Account(),
				{ u"send"_q, u"mute"_q },
				{ 5 });
			const auto send = Policy(Risk::Visible, true, true, true);
			if (const auto error = ExpectDecision(
					gate.check(Request(u"send"_q, send, { 5 })),
					Verdict::Allow);
				!error.isEmpty()) {
				return u"tainted send in frame: "_q + error;
			} else if (const auto error = ExpectDecision(
					gate.check(Request(
						u"mute"_q,
						Policy(Risk::Reversible),
						{ 5 })),
					Verdict::Allow);
				!error.isEmpty()) {
				return u"mute in frame: "_q + error;
			} else if (const auto error = ExpectDecision(
					gate.check(Request(u"send"_q, send, { 5, 6 })),
					Verdict::Confirm);
				!error.isEmpty()) {
				return u"send out of frame: "_q + error;
			}
			return ExpectDecision(
				gate.check(Request(
					u"forward"_q,
					Policy(Risk::Visible, true, true, true),
					{ 5 })),
				Verdict::Confirm);
		} },
		{ u"policy_frame_keeps_denials"_q, [] {
			const auto excluded = [](PeerKey peer) { return peer == 7; };
			const auto gate = FrameGate(
				Scope::Chat(5),
				{ u"send"_q },
				{ 5, 7, 8 },
				excluded);
			const auto send = Policy(Risk::Visible);
			if (const auto error = ExpectDecision(
					gate.check(Request(u"send"_q, send, { 7 })),
					Verdict::Deny,
					DenyReason::Excluded);
				!error.isEmpty()) {
				return error;
			} else if (const auto error = ExpectDecision(
					gate.check(Request(
						u"send"_q,
						Policy(Risk::Forbidden),
						{ 5 })),
					Verdict::Deny,
					DenyReason::Forbidden);
				!error.isEmpty()) {
				return error;
			}
			return ExpectDecision(
				gate.check(Request(u"send"_q, send, { 8 })),
				Verdict::Deny,
				DenyReason::OutOfScope);
		} },
		{ u"policy_in_frame"_q, [] {
			const auto gate = FrameGate(Scope::Account(), {}, { 5 });
			const auto normal = PolicyGate(Scope::Account());
			const auto empty = PolicyGate(
				Scope::Account(),
				nullptr,
				false,
				GateFrame());
			if (!gate.inFrame(5) || gate.inFrame(6)) {
				return u"a frame must hold only its chats"_q;
			} else if (!normal.inFrame(6)) {
				return u"without a frame every chat is in it"_q;
			} else if (empty.inFrame(5)) {
				return u"a null contains holds no chat"_q;
			}
			return QString();
		} },
		{ u"policy_frame_null_contains"_q, [] {
			const auto gate = PolicyGate(
				Scope::Account(),
				nullptr,
				false,
				GateFrame{ .tools = { u"send"_q } });
			return ExpectDecision(
				gate.check(Request(u"send"_q, Policy(Risk::Visible), { 5 })),
				Verdict::Confirm);
		} },
	};
}

} // namespace Agent::Tests
