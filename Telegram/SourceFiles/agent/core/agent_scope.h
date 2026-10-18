/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <compare>

namespace Agent {

using PeerKey = uint64;

enum class ScopeKind : uchar {
	Account,
	Chat,
};

struct Scope {
	ScopeKind kind = ScopeKind::Account;
	PeerKey peer = 0;

	[[nodiscard]] static Scope Account() {
		return {};
	}
	[[nodiscard]] static Scope Chat(PeerKey peer) {
		return { .kind = ScopeKind::Chat, .peer = peer };
	}

	friend inline auto operator<=>(const Scope &, const Scope &) = default;
	friend inline bool operator==(const Scope &, const Scope &) = default;
};

} // namespace Agent
