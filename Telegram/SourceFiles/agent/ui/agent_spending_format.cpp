/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_spending_format.h"

#include "lang/lang_keys.h"
#include "lang/lang_tag.h"

#include <cmath>

namespace Agent::Ui {
namespace {

constexpr auto kMinShownCost = 0.0001;

[[nodiscard]] QString ShortCount(qint64 count) {
	return Lang::FormatCountToShort(count).string;
}

} // namespace

QString FormatCost(double cost) {
	if (cost <= 0.) {
		return u"$0"_q;
	} else if (cost < kMinShownCost) {
		return u"<$0.0001"_q;
	}
	const auto magnitude = int(std::floor(std::log10(cost)));
	const auto decimals = std::clamp(1 - magnitude, 2, 4);
	return u"$"_q + QString::number(cost, 'f', decimals);
}

QString FormatSpending(const SpendingTotals &totals, bool withCache) {
	if (totals.empty()) {
		return QString();
	}
	const auto tokens = float64(totals.tokens());
	const auto result = totals.hasCost
		? tr::lng_agent_spending_tokens_cost(
			tr::now,
			lt_count_short,
			tokens,
			lt_cost,
			FormatCost(totals.cost))
		: tr::lng_agent_spending_tokens(tr::now, lt_count_short, tokens);
	const auto cached = withCache ? CachedPercent(totals) : 0;
	return (cached > 0)
		? (result + u" · "_q + tr::lng_agent_spending_cached(
			tr::now,
			lt_percent,
			QString::number(cached)))
		: result;
}

QString FormatSpendingDetails(const SpendingTotals &totals) {
	return tr::lng_agent_spending_details(
		tr::now,
		lt_input,
		ShortCount(totals.inputTokens),
		lt_cached,
		ShortCount(totals.cachedInputTokens),
		lt_output,
		ShortCount(totals.outputTokens));
}

SpendingTotals UsageTotals(const Usage &usage) {
	auto result = SpendingTotals();
	result.inputTokens = usage.inputTokens;
	result.outputTokens = usage.outputTokens;
	result.cachedInputTokens = usage.cachedInputTokens;
	result.cost = usage.cost;
	result.hasCost = usage.hasCost;
	result.requests = (usage.inputTokens || usage.outputTokens) ? 1 : 0;
	return result;
}

} // namespace Agent::Ui
