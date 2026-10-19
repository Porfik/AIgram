/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_types.h"

#include <QtCore/QByteArray>

#include <optional>
#include <vector>

namespace Agent {

struct ProviderSettings;

inline constexpr auto kSpendingKeepDays = 62;

struct SpendingTotals {
	qint64 inputTokens = 0;
	qint64 outputTokens = 0;
	qint64 cachedInputTokens = 0;
	double cost = 0.;
	bool hasCost = false;
	int requests = 0;

	void add(const Usage &usage);
	void add(const SpendingTotals &other);
	[[nodiscard]] qint64 tokens() const;
	[[nodiscard]] bool empty() const;
};

struct SpendingDay {
	int day = 0;
	SpendingTotals totals;
};

struct SpendingStats {
	std::vector<SpendingDay> days;
	SpendingTotals older;
};

[[nodiscard]] Usage PriceUsage(
	Usage usage,
	const ProviderSettings &provider);

void AddSpending(SpendingStats &stats, int day, const Usage &usage);
[[nodiscard]] SpendingTotals SumSpending(
	const SpendingStats &stats,
	int fromDay,
	int toDay);
[[nodiscard]] SpendingTotals SumAllSpending(const SpendingStats &stats);

[[nodiscard]] QByteArray SerializeSpending(const SpendingStats &stats);
[[nodiscard]] std::optional<SpendingStats> DeserializeSpending(
	const QByteArray &bytes);

} // namespace Agent
