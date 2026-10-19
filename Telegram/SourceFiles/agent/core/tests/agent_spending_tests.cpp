/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_providers.h"
#include "agent/core/agent_spending.h"

#include <QtCore/QDataStream>

namespace Agent::Tests {
namespace {

[[nodiscard]] QByteArray Written(Fn<void(QDataStream &)> write) {
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	write(stream);
	return bytes;
}

[[nodiscard]] ProviderSettings Priced(double input, double output, double cached) {
	auto provider = ProviderSettings();
	provider.priceInput = input;
	provider.priceOutput = output;
	provider.priceCached = cached;
	return provider;
}

[[nodiscard]] QString TotalsEqual(
		const SpendingTotals &a,
		const SpendingTotals &b) {
	if (a.inputTokens != b.inputTokens) {
		return u"inputTokens differ"_q;
	} else if (a.outputTokens != b.outputTokens) {
		return u"outputTokens differ"_q;
	} else if (a.cachedInputTokens != b.cachedInputTokens) {
		return u"cachedInputTokens differ"_q;
	} else if (a.cost != b.cost) {
		return u"cost differs"_q;
	} else if (a.hasCost != b.hasCost) {
		return u"hasCost differs"_q;
	} else if (a.requests != b.requests) {
		return u"requests differ"_q;
	}
	return QString();
}

} // namespace

std::vector<Check> SpendingChecks() {
	return {
		{ u"spending_totals_add"_q, [] {
			auto totals = SpendingTotals();
			totals.add(Usage{ 10, 20, 4 });
			if (totals.requests != 1
				|| totals.inputTokens != 10
				|| totals.outputTokens != 20
				|| totals.cachedInputTokens != 4
				|| totals.hasCost
				|| totals.tokens() != 30) {
				return u"unexpected totals without cost"_q;
			}
			totals.add(Usage{ 1, 2, 0, 0.5, true });
			if (totals.requests != 2
				|| totals.inputTokens != 11
				|| totals.outputTokens != 22
				|| totals.cachedInputTokens != 4
				|| !totals.hasCost
				|| totals.cost != 0.5) {
				return u"unexpected totals with cost"_q;
			}
			auto other = SpendingTotals();
			other.add(Usage{ 5, 5, 5, 1.5, true });
			totals.add(other);
			if (totals.requests != 3
				|| totals.inputTokens != 16
				|| totals.outputTokens != 27
				|| totals.cachedInputTokens != 9
				|| totals.cost != 2.
				|| totals.tokens() != 43) {
				return u"unexpected merged totals"_q;
			}
			auto empty = SpendingTotals();
			if (!empty.empty()) {
				return u"a fresh total must be empty"_q;
			}
			return QString();
		} },
		{ u"spending_price_usage_with_provider_cost"_q, [] {
			const auto usage = Usage{ 100, 200, 50, 0.25, true };
			const auto priced = PriceUsage(
				usage,
				Priced(1., 2., 0.1));
			if (!priced.hasCost || priced.cost != 0.25) {
				return u"a reported cost must be kept"_q;
			}
			const auto free = PriceUsage(usage, ProviderSettings());
			if (!free.hasCost || free.cost != 0.25) {
				return u"a reported cost must survive empty prices"_q;
			}
			return QString();
		} },
		{ u"spending_price_usage_with_prices"_q, [] {
			const auto usage = Usage{ 1'000'000, 500'000, 400'000 };
			const auto priced = PriceUsage(usage, Priced(1., 2., 0.1));
			if (!priced.hasCost) {
				return u"prices must set hasCost"_q;
			}
			const auto expected = (600'000 * 1. + 400'000 * 0.1
				+ 500'000 * 2.) / 1'000'000.;
			if (priced.cost != expected) {
				return u"cost: got %1, expected %2"_q
					.arg(priced.cost).arg(expected);
			}
			return QString();
		} },
		{ u"spending_price_usage_cached_fallback"_q, [] {
			const auto usage = Usage{ 1'000'000, 0, 300'000 };
			const auto withCached = PriceUsage(usage, Priced(1., 2., 0.5));
			const auto expected = (700'000 * 1. + 300'000 * 0.5) / 1'000'000.;
			if (withCached.cost != expected) {
				return u"cached price: got %1, expected %2"_q
					.arg(withCached.cost).arg(expected);
			}
			const auto withoutCached = PriceUsage(
				usage,
				Priced(1., 2., 0.));
			const auto fallback = (700'000 * 1. + 300'000 * 1.) / 1'000'000.;
			if (withoutCached.cost != fallback) {
				return u"cached fallback: got %1, expected %2"_q
					.arg(withoutCached.cost).arg(fallback);
			}
			return QString();
		} },
		{ u"spending_price_usage_only_cached_price"_q, [] {
			const auto usage = Usage{ 1'000'000, 1'000'000, 300'000 };
			const auto priced = PriceUsage(usage, Priced(0., 0., 0.5));
			if (!priced.hasCost || priced.cost != 150'000 / 1'000'000.) {
				return u"only priceCached: got %1"_q.arg(priced.cost);
			}
			return QString();
		} },
		{ u"spending_price_usage_without_prices"_q, [] {
			auto usage = Usage{ 100, 200, 50 };
			const auto priced = PriceUsage(usage, ProviderSettings());
			if (priced.hasCost || priced.cost != 0.) {
				return u"no prices must leave the usage unchanged"_q;
			}
			const auto negative = PriceUsage(usage, Priced(0., -1., 0.));
			if (negative.hasCost || negative.cost != 0.) {
				return u"negative prices must leave the usage unchanged"_q;
			}
			return QString();
		} },
		{ u"spending_add_days_in_order"_q, [] {
			auto stats = SpendingStats();
			AddSpending(stats, 100, Usage{ 10, 0 });
			AddSpending(stats, 90, Usage{ 20, 0 });
			AddSpending(stats, 100, Usage{ 5, 0 });
			AddSpending(stats, 95, Usage{ 7, 0 });
			if (stats.days.size() != 3) {
				return u"expected 3 days, got %1"_q.arg(stats.days.size());
			}
			const auto days = std::vector{ 90, 95, 100 };
			for (auto i = 0; i != 3; ++i) {
				if (stats.days[i].day != days[i]) {
					return u"day %1 out of order"_q.arg(i);
				}
			}
			if (stats.days[2].totals.inputTokens != 15
				|| stats.days[2].totals.requests != 2) {
				return u"the same day must merge into one bucket"_q;
			}
			return QString();
		} },
		{ u"spending_moves_old_days_into_older"_q, [] {
			auto stats = SpendingStats();
			AddSpending(stats, 100, Usage{ 10, 0 });
			AddSpending(stats, 162, Usage{ 20, 0 });
			if (stats.days.size() != 2 || !stats.older.empty()) {
				return u"a day 62 before the newest must stay"_q;
			}
			AddSpending(stats, 170, Usage{ 40, 0 });
			if (stats.days.size() != 2
				|| stats.days[0].day != 162
				|| stats.days[1].day != 170) {
				return u"the newest window is wrong"_q;
			} else if (stats.older.inputTokens != 10
				|| stats.older.requests != 1) {
				return u"the old day must move into older"_q;
			}
			return QString();
		} },
		{ u"spending_sum_ranges"_q, [] {
			auto stats = SpendingStats();
			AddSpending(stats, 10, Usage{ 10, 10, 5, 1., true });
			AddSpending(stats, 20, Usage{ 20, 20, 0, 2., true });
			AddSpending(stats, 30, Usage{ 30, 30, 0, 3., true });
			const auto all = SumAllSpending(stats);
			if (all.requests != 3
				|| all.inputTokens != 60
				|| all.outputTokens != 60
				|| all.cachedInputTokens != 5
				|| !all.hasCost
				|| all.cost != 6.) {
				return u"unexpected SumAllSpending"_q;
			}
			const auto middle = SumSpending(stats, 15, 25);
			if (middle.requests != 1 || middle.inputTokens != 20) {
				return u"unexpected middle range"_q;
			}
			const auto empty = SumSpending(stats, 40, 50);
			if (!empty.empty()) {
				return u"an out-of-range sum must be empty"_q;
			}
			const auto bounds = SumSpending(stats, 20, 20);
			if (bounds.requests != 1) {
				return u"a single-day range must include its bounds"_q;
			}
			stats.older.add(Usage{ 1, 1 });
			const auto withOlder = SumAllSpending(stats);
			if (withOlder.requests != 4 || withOlder.inputTokens != 61) {
				return u"SumAllSpending must include older"_q;
			}
			return QString();
		} },
		{ u"spending_round_trip"_q, [] {
			auto stats = SpendingStats();
			AddSpending(stats, 10, Usage{ 10, 20, 5, 0.25, true });
			AddSpending(stats, 20, Usage{ 30, 40 });
			stats.older.add(Usage{ 50, 60, 0, 1.5, true });
			const auto bytes = SerializeSpending(stats);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeSpending(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			if (restored->days.size() != 2
				|| restored->days[0].day != 10
				|| restored->days[1].day != 20) {
				return u"unexpected restored days"_q;
			}
			const auto dayDiff = TotalsEqual(
				restored->days[0].totals,
				stats.days[0].totals);
			if (!dayDiff.isEmpty()) {
				return dayDiff;
			}
			const auto olderDiff = TotalsEqual(
				restored->older,
				stats.older);
			if (!olderDiff.isEmpty()) {
				return olderDiff;
			}
			if (SerializeSpending(*restored) != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"spending_rejects_bad_data"_q, [] {
			const auto good = SerializeSpending(SpendingStats());
			const auto bad = std::vector<QByteArray>{
				QByteArray(),
				good.mid(0, good.size() - 1),
				good + QByteArray("x"),
				Written([](QDataStream &stream) {
					stream << quint32(2) << qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(0) << qint32(-1);
				}),
				Written([](QDataStream &stream) {
					stream
						<< quint32(1)
						<< qint64(1)
						<< qint64(1)
						<< qint64(1)
						<< 1.
						<< qint32(0)
						<< qint32(-1)
						<< qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream
						<< quint32(1)
						<< qint64(0)
						<< qint64(0)
						<< qint64(0)
						<< 0.
						<< qint32(0)
						<< qint32(0)
						<< qint32(1)
						<< qint32(10)
						<< qint64(1)
						<< qint64(1)
						<< qint64(1)
						<< 1.
						<< qint32(0)
						<< qint32(-1);
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeSpending(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
