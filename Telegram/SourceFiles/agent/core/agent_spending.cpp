/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_spending.h"

#include "agent/core/agent_providers.h"

namespace Agent {

void SpendingTotals::add(const Usage &usage) {
}

void SpendingTotals::add(const SpendingTotals &other) {
}

qint64 SpendingTotals::tokens() const {
	return inputTokens + outputTokens;
}

bool SpendingTotals::empty() const {
	return !requests;
}

Usage PriceUsage(Usage usage, const ProviderSettings &provider) {
	return usage;
}

void AddSpending(SpendingStats &stats, int day, const Usage &usage) {
}

SpendingTotals SumSpending(
		const SpendingStats &stats,
		int fromDay,
		int toDay) {
	return {};
}

SpendingTotals SumAllSpending(const SpendingStats &stats) {
	return {};
}

QByteArray SerializeSpending(const SpendingStats &stats) {
	return QByteArray();
}

std::optional<SpendingStats> DeserializeSpending(const QByteArray &bytes) {
	return std::nullopt;
}

} // namespace Agent
