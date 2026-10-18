/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_policy.h"

namespace Agent {

std::vector<PeerKey> FindNamedPeers(
		const QString &text,
		const std::vector<PeerName> &candidates) {
	return {};
}

PolicyGate::PolicyGate(
	Scope scope,
	Fn<bool(PeerKey)> excluded,
	bool tainted)
: _scope(scope)
, _excluded(std::move(excluded))
, _tainted(tainted) {
}

const Scope &PolicyGate::scope() const {
	return _scope;
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
	return true;
}

bool PolicyGate::visible(PeerKey peer) const {
	return true;
}

Decision PolicyGate::check(const ActionRequest &request) const {
	return {};
}

void PolicyGate::allowForTurn(const ActionRequest &request) {
}

} // namespace Agent
