/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Agent {

enum class Risk : uchar {
	Read,
	Reversible,
	Visible,
	Forbidden,
};

struct ToolPolicy {
	Risk risk = Risk::Read;
	bool general = true;
	bool chat = true;
	bool outbound = false;
	bool background = true;
	bool interactive = true;
	bool chatTask = false;
};

struct ActionRequest {
	QString tool;
	ToolPolicy policy;
	std::vector<PeerKey> peers;
};

enum class Verdict : uchar {
	Allow,
	Confirm,
	Deny,
};

enum class DenyReason : uchar {
	None,
	NotInMode,
	OutOfScope,
	Excluded,
	Forbidden,
	OutOfFrame,
};

struct Decision {
	Verdict verdict = Verdict::Allow;
	DenyReason reason = DenyReason::None;
	bool canAllowForTurn = false;
};

struct PeerName {
	PeerKey peer = 0;
	QString title;
	QString username;
};

[[nodiscard]] std::vector<PeerKey> FindNamedPeers(
	const QString &text,
	const std::vector<PeerName> &candidates);

struct GateFrame {
	std::vector<QString> tools;
	Fn<bool(PeerKey)> contains;
};

class PolicyGate final {
public:
	PolicyGate(
		Scope scope,
		Fn<bool(PeerKey)> excluded = nullptr,
		bool tainted = false,
		std::optional<GateFrame> frame = std::nullopt);

	[[nodiscard]] const Scope &scope() const;
	[[nodiscard]] bool background() const;

	void beginTurn(std::vector<PeerKey> named);
	void markTainted();
	[[nodiscard]] bool tainted() const;

	[[nodiscard]] bool available(const ToolPolicy &policy) const;
	[[nodiscard]] bool visible(PeerKey peer) const;
	[[nodiscard]] Decision check(const ActionRequest &request) const;
	void allowForTurn(const ActionRequest &request);

private:
	struct Allowance {
		QString tool;
		std::vector<PeerKey> peers;
	};

	[[nodiscard]] const Allowance *findAllowance(const QString &tool) const;
	[[nodiscard]] bool coversPeers(
		const QString &tool,
		const std::vector<PeerKey> &peers) const;
	[[nodiscard]] bool hasUnnamedPeer(
		const std::vector<PeerKey> &peers) const;
	[[nodiscard]] bool peersInFrame(
		const std::vector<PeerKey> &peers) const;
	[[nodiscard]] bool toolInFrame(const QString &tool) const;

	const Scope _scope;
	const Fn<bool(PeerKey)> _excluded;
	const std::optional<GateFrame> _frame;
	bool _tainted = false;
	std::vector<PeerKey> _named;
	std::vector<Allowance> _allowances;

};

} // namespace Agent
