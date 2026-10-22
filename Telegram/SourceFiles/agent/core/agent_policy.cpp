/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_policy.h"

#include <algorithm>

namespace Agent {
namespace {

[[nodiscard]] bool IsWordChar(QChar ch) {
	return ch.isLetterOrNumber() || (ch == '_');
}

[[nodiscard]] bool WordBefore(const QString &text, int position) {
	return !position || !IsWordChar(text[position - 1]);
}

[[nodiscard]] bool WordAfter(const QString &text, int position) {
	return position == text.size() || !IsWordChar(text[position]);
}

[[nodiscard]] bool MatchesTitle(const QString &folded, const QString &title) {
	for (auto i = folded.indexOf(title)
		; i >= 0
		; i = folded.indexOf(title, i + 1)) {
		if (WordBefore(folded, i) && WordAfter(folded, i + title.size())) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool MatchesUsername(
		const QString &folded,
		const QString &name) {
	const auto length = name.size();
	for (auto i = folded.indexOf(u'@')
		; i >= 0
		; i = folded.indexOf(u'@', i + 1)) {
		if (WordBefore(folded, i)
			&& (i + 1 + length <= folded.size())
			&& (folded.mid(i + 1, length) == name)
			&& WordAfter(folded, i + 1 + length)) {
			return true;
		}
	}
	const auto schemes = { u"http://"_q, u"https://"_q };
	const auto domains = { u"t.me/"_q, u"telegram.me/"_q };
	for (const auto &domain : domains) {
		for (auto i = folded.indexOf(domain)
			; i >= 0
			; i = folded.indexOf(domain, i + 1)) {
			const auto after = i + domain.size() + length;
			if (after > folded.size()
				|| (folded.mid(i + domain.size(), length) != name)
				|| !WordAfter(folded, after)) {
				continue;
			}
			auto start = i;
			for (const auto &scheme : schemes) {
				if (i >= int(scheme.size())
					&& (folded.mid(i - scheme.size(), scheme.size())
						== scheme)) {
					start = i - scheme.size();
				}
			}
			if (WordBefore(folded, start)) {
				return true;
			}
		}
	}
	return false;
}

void AddPeer(std::vector<PeerKey> &to, PeerKey peer) {
	if (!peer || std::find(to.begin(), to.end(), peer) != to.end()) {
		return;
	}
	to.push_back(peer);
}

} // namespace

std::vector<PeerKey> FindNamedPeers(
		const QString &text,
		const std::vector<PeerName> &candidates) {
	const auto folded = text.toCaseFolded();
	auto result = std::vector<PeerKey>();
	for (const auto &candidate : candidates) {
		const auto username = candidate.username.trimmed().toCaseFolded();
		if (!username.isEmpty() && MatchesUsername(folded, username)) {
			AddPeer(result, candidate.peer);
			continue;
		}
		const auto title = candidate.title.trimmed().toCaseFolded();
		if (title.size() >= 3 && MatchesTitle(folded, title)) {
			AddPeer(result, candidate.peer);
		}
	}
	return result;
}

PolicyGate::PolicyGate(
	Scope scope,
	Fn<bool(PeerKey)> excluded,
	bool tainted,
	std::optional<GateFrame> frame)
: _scope(scope)
, _excluded(std::move(excluded))
, _frame(std::move(frame))
, _tainted(tainted) {
}

const Scope &PolicyGate::scope() const {
	return _scope;
}

bool PolicyGate::background() const {
	return _frame.has_value();
}

void PolicyGate::beginTurn(std::vector<PeerKey> named) {
	_named = std::move(named);
	_allowances.clear();
}

void PolicyGate::markTainted() {
	_tainted = true;
}

bool PolicyGate::tainted() const {
	return _tainted;
}

bool PolicyGate::available(const ToolPolicy &policy) const {
	const auto mode = _frame ? policy.background : policy.interactive;
	const auto scope = (_scope.kind == ScopeKind::Account)
		? policy.general
		: (policy.chat || (_frame && policy.chatTask));
	return mode && scope;
}

bool PolicyGate::visible(PeerKey peer) const {
	if (!peer || (_excluded && _excluded(peer))) {
		return false;
	}
	return (_scope.kind == ScopeKind::Account) || (peer == _scope.peer);
}

bool PolicyGate::inFrame(PeerKey peer) const {
	return !_frame || peersInFrame({ peer });
}

const PolicyGate::Allowance *PolicyGate::findAllowance(
		const QString &tool) const {
	const auto i = std::find_if(
		_allowances.begin(),
		_allowances.end(),
		[&](const Allowance &allowance) {
			return allowance.tool == tool;
		});
	return (i == _allowances.end()) ? nullptr : &*i;
}

bool PolicyGate::coversPeers(
		const QString &tool,
		const std::vector<PeerKey> &peers) const {
	const auto allowance = findAllowance(tool);
	if (!allowance) {
		return false;
	}
	const auto &covered = allowance->peers;
	return std::all_of(peers.begin(), peers.end(), [&](PeerKey peer) {
		return std::find(covered.begin(), covered.end(), peer)
			!= covered.end();
	});
}

bool PolicyGate::hasUnnamedPeer(const std::vector<PeerKey> &peers) const {
	return std::any_of(peers.begin(), peers.end(), [&](PeerKey peer) {
		return std::find(_named.begin(), _named.end(), peer) == _named.end();
	});
}

bool PolicyGate::peersInFrame(const std::vector<PeerKey> &peers) const {
	for (const auto peer : peers) {
		if (!_frame->contains || !_frame->contains(peer)) {
			return false;
		}
	}
	return true;
}

bool PolicyGate::toolInFrame(const QString &tool) const {
	return std::find(
		_frame->tools.begin(),
		_frame->tools.end(),
		tool) != _frame->tools.end();
}

Decision PolicyGate::check(const ActionRequest &request) const {
	if (!available(request.policy)) {
		return { Verdict::Deny, DenyReason::NotInMode, false };
	} else if (request.policy.risk == Risk::Forbidden) {
		return { Verdict::Deny, DenyReason::Forbidden, false };
	}
	for (const auto peer : request.peers) {
		if (_excluded && _excluded(peer)) {
			return { Verdict::Deny, DenyReason::Excluded, false };
		}
	}
	for (const auto peer : request.peers) {
		if (!peer
			|| (_scope.kind == ScopeKind::Chat && peer != _scope.peer)) {
			return { Verdict::Deny, DenyReason::OutOfScope, false };
		}
	}
	if (_frame) {
		const auto inFrame = peersInFrame(request.peers);
		if (request.policy.risk == Risk::Read) {
			return inFrame
				? Decision{ Verdict::Allow, DenyReason::None, false }
				: Decision{ Verdict::Deny, DenyReason::OutOfFrame, false };
		}
		return (inFrame && toolInFrame(request.tool))
			? Decision{ Verdict::Allow, DenyReason::None, false }
			: Decision{ Verdict::Confirm, DenyReason::None, false };
	}
	if (request.policy.risk == Risk::Reversible
		&& request.policy.taintedAsks
		&& _tainted) {
		return { Verdict::Confirm, DenyReason::None, false };
	} else if (request.policy.risk != Risk::Visible) {
		return { Verdict::Allow, DenyReason::None, false };
	}
	const auto blocked = _tainted
		&& (_scope.kind == ScopeKind::Account)
		&& request.policy.outbound
		&& (request.peers.empty() || hasUnnamedPeer(request.peers));
	if (!blocked && coversPeers(request.tool, request.peers)) {
		return { Verdict::Allow, DenyReason::None, false };
	}
	return { Verdict::Confirm, DenyReason::None, !blocked };
}

void PolicyGate::allowForTurn(const ActionRequest &request) {
	auto i = std::find_if(
		_allowances.begin(),
		_allowances.end(),
		[&](const Allowance &allowance) {
			return allowance.tool == request.tool;
		});
	if (i == _allowances.end()) {
		i = _allowances.insert(_allowances.end(), { request.tool, {} });
	}
	for (const auto peer : request.peers) {
		if (peer
			&& std::find(i->peers.begin(), i->peers.end(), peer)
				== i->peers.end()) {
			i->peers.push_back(peer);
		}
	}
}

} // namespace Agent
