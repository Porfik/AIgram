/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_spending.h"

#include "agent/core/agent_providers.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

#include <algorithm>

namespace Agent {
namespace {

constexpr auto kSpendingVersion = quint32(1);
constexpr auto kMaxDays = 1'000'000;

constexpr auto kTokensPerMillion = 1'000'000.;

void AddTotals(SpendingTotals &to, const SpendingTotals &from) {
	to.inputTokens += from.inputTokens;
	to.outputTokens += from.outputTokens;
	to.cachedInputTokens += from.cachedInputTokens;
	to.cost += from.cost;
	to.hasCost = to.hasCost || from.hasCost;
	to.requests += from.requests;
}

void MoveOldDays(SpendingStats &stats) {
	if (stats.days.empty()) {
		return;
	}
	const auto newest = stats.days.back().day;
	const auto cutoff = newest - kSpendingKeepDays;
	auto first = 0;
	while (first < int(stats.days.size())
		&& stats.days[first].day < cutoff) {
		++first;
	}
	if (first > 0) {
		for (auto i = 0; i != first; ++i) {
			AddTotals(stats.older, stats.days[i].totals);
		}
		stats.days.erase(stats.days.begin(), stats.days.begin() + first);
	}
}

void WriteTotals(QDataStream &stream, const SpendingTotals &totals) {
	stream
		<< qint64(totals.inputTokens)
		<< qint64(totals.outputTokens)
		<< qint64(totals.cachedInputTokens)
		<< totals.cost
		<< qint32(totals.hasCost ? 1 : 0)
		<< qint32(totals.requests);
}

[[nodiscard]] bool ReadTotals(QDataStream &stream, SpendingTotals &totals) {
	auto input = qint64(0);
	auto output = qint64(0);
	auto cached = qint64(0);
	auto hasCost = qint32(0);
	auto requests = qint32(0);
	stream >> input >> output >> cached >> totals.cost >> hasCost
		>> requests;
	if (stream.status() != QDataStream::Ok
		|| input < 0
		|| output < 0
		|| cached < 0
		|| (hasCost != 0 && hasCost != 1)
		|| requests < 0) {
		return false;
	}
	totals.inputTokens = input;
	totals.outputTokens = output;
	totals.cachedInputTokens = cached;
	totals.hasCost = (hasCost != 0);
	totals.requests = requests;
	return true;
}

} // namespace

void SpendingTotals::add(const Usage &usage) {
	inputTokens += usage.inputTokens;
	outputTokens += usage.outputTokens;
	cachedInputTokens += usage.cachedInputTokens;
	if (usage.hasCost) {
		cost += usage.cost;
		hasCost = true;
	}
	++requests;
}

void SpendingTotals::add(const SpendingTotals &other) {
	AddTotals(*this, other);
}

qint64 SpendingTotals::tokens() const {
	return inputTokens + outputTokens;
}

bool SpendingTotals::empty() const {
	return !requests;
}

Usage PriceUsage(Usage usage, const ProviderSettings &provider) {
	if (usage.hasCost) {
		return usage;
	} else if (provider.priceInput <= 0.
		&& provider.priceOutput <= 0.
		&& provider.priceCached <= 0.) {
		return usage;
	}
	const auto uncached = std::max(
		usage.inputTokens - usage.cachedInputTokens,
		0);
	const auto cachedPrice = provider.priceCached > 0.
		? provider.priceCached
		: provider.priceInput;
	auto cost = (double(uncached) * provider.priceInput
		+ double(usage.cachedInputTokens) * cachedPrice
		+ double(usage.outputTokens) * provider.priceOutput)
		/ kTokensPerMillion;
	usage.cost = cost;
	usage.hasCost = true;
	return usage;
}

void AddSpending(SpendingStats &stats, int day, const Usage &usage) {
	auto it = std::lower_bound(
		stats.days.begin(),
		stats.days.end(),
		day,
		[](const SpendingDay &bucket, int value) {
			return bucket.day < value;
		});
	if (it == stats.days.end() || it->day != day) {
		it = stats.days.insert(it, SpendingDay{ day, {} });
	}
	it->totals.add(usage);
	MoveOldDays(stats);
}

SpendingTotals SumSpending(
		const SpendingStats &stats,
		int fromDay,
		int toDay) {
	auto result = SpendingTotals();
	for (const auto &bucket : stats.days) {
		if (bucket.day > toDay) {
			break;
		} else if (bucket.day >= fromDay) {
			AddTotals(result, bucket.totals);
		}
	}
	return result;
}

SpendingTotals SumAllSpending(const SpendingStats &stats) {
	auto result = stats.older;
	for (const auto &bucket : stats.days) {
		AddTotals(result, bucket.totals);
	}
	return result;
}

QByteArray SerializeSpending(const SpendingStats &stats) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kSpendingVersion;
	WriteTotals(stream, stats.older);
	stream << qint32(stats.days.size());
	for (const auto &bucket : stats.days) {
		stream << qint32(bucket.day);
		WriteTotals(stream, bucket.totals);
	}
	return result;
}

std::optional<SpendingStats> DeserializeSpending(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version != kSpendingVersion) {
		return std::nullopt;
	}
	auto stats = SpendingStats();
	if (!ReadTotals(stream, stats.older)) {
		return std::nullopt;
	}
	auto dayCount = qint32(0);
	stream >> dayCount;
	if (stream.status() != QDataStream::Ok
		|| dayCount < 0
		|| dayCount > kMaxDays) {
		return std::nullopt;
	}
	stats.days.reserve(dayCount);
	auto previousDay = 0;
	for (auto i = 0; i != dayCount; ++i) {
		auto day = qint32(0);
		auto bucket = SpendingDay();
		stream >> day;
		if (stream.status() != QDataStream::Ok
			|| !ReadTotals(stream, bucket.totals)
			|| (i > 0 && day <= previousDay)) {
			return std::nullopt;
		}
		bucket.day = day;
		previousDay = day;
		stats.days.push_back(std::move(bucket));
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return stats;
}

} // namespace Agent
