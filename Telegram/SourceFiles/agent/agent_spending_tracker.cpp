/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_spending_tracker.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

#include <QtCore/QDate>

namespace Agent {
namespace {

constexpr auto kSaveDelay = 2 * crl::time(1000);

} // namespace

SpendingTracker::SpendingTracker(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(); }) {
	const auto bytes = _session->local().readAgentValue(u"spending"_q);
	if (!bytes) {
		return;
	}
	auto stats = DeserializeSpending(*bytes);
	if (!stats) {
		LOG(("Agent Error: could not parse the agent spending."));
		return;
	}
	_stats = std::move(*stats);
}

SpendingTracker::~SpendingTracker() {
	if (_savePending) {
		save();
	}
}

void SpendingTracker::add(const Usage &usage) {
	AddSpending(_stats, QDate::currentDate().toJulianDay(), usage);
	_savePending = true;
	_saveTimer.callOnce(kSaveDelay);
	_changes.fire({});
}

SpendingTotals SpendingTracker::today() const {
	const auto day = QDate::currentDate().toJulianDay();
	return SumSpending(_stats, day, day);
}

SpendingTotals SpendingTracker::lastDays(int days) const {
	Expects(days > 0);

	const auto day = QDate::currentDate().toJulianDay();
	return SumSpending(_stats, day - (days - 1), day);
}

SpendingTotals SpendingTracker::allTime() const {
	return SumAllSpending(_stats);
}

void SpendingTracker::reset() {
	_saveTimer.cancel();
	_savePending = false;
	_stats = {};
	_session->local().removeAgentValue(u"spending"_q);
	_changes.fire({});
}

rpl::producer<> SpendingTracker::changes() const {
	return _changes.events();
}

void SpendingTracker::save() {
	_savePending = false;
	_session->local().writeAgentValue(
		u"spending"_q,
		SerializeSpending(_stats));
}

} // namespace Agent
