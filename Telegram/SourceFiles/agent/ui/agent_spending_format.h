/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_spending.h"

namespace Agent::Ui {

[[nodiscard]] QString FormatCost(double cost);
[[nodiscard]] QString FormatSpending(
	const SpendingTotals &totals,
	bool withCache = true);
[[nodiscard]] QString FormatSpendingDetails(const SpendingTotals &totals);
[[nodiscard]] SpendingTotals UsageTotals(const Usage &usage);

} // namespace Agent::Ui
