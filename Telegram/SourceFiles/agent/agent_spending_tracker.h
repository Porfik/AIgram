/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_spending.h"
#include "base/timer.h"

#include <rpl/event_stream.h>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class SpendingTracker final {
public:
	explicit SpendingTracker(not_null<Main::Session*> session);
	~SpendingTracker();

	void add(const Usage &usage);

	[[nodiscard]] SpendingTotals today() const;
	[[nodiscard]] SpendingTotals lastDays(int days) const;
	[[nodiscard]] SpendingTotals allTime() const;
	void reset();

	[[nodiscard]] rpl::producer<> changes() const;

private:
	void save();

	const not_null<Main::Session*> _session;
	SpendingStats _stats;
	base::Timer _saveTimer;
	rpl::event_stream<> _changes;

};

} // namespace Agent
