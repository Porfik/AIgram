/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_spending_tracker.h"

#include "main/main_session.h"

namespace Agent {

SpendingTracker::SpendingTracker(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(); }) {
}

SpendingTracker::~SpendingTracker() = default;

void SpendingTracker::add(const Usage &usage) {
}

SpendingTotals SpendingTracker::today() const {
	return {};
}

SpendingTotals SpendingTracker::lastDays(int days) const {
	return {};
}

SpendingTotals SpendingTracker::allTime() const {
	return {};
}

void SpendingTracker::reset() {
}

rpl::producer<> SpendingTracker::changes() const {
	return _changes.events();
}

void SpendingTracker::save() {
}

} // namespace Agent
