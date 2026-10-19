/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <memory>
#include <optional>
#include <vector>

namespace Agent {

class Adapter;

enum class ProviderKind : uchar {
	OpenAiCompatible,
	Anthropic,
};

struct ProviderSettings {
	QString id;
	ProviderKind kind = ProviderKind::OpenAiCompatible;
	QString title;
	QString baseUrl;
	QString apiKey;
	QString model;
	int contextWindow = 0;
	bool vision = false;
	double priceInput = 0.;
	double priceOutput = 0.;
	double priceCached = 0.;
};

enum class RowPreview : uchar {
	LastMessage,
	SessionTitle,
	Hint,
};

struct ProvidersState {
	std::vector<ProviderSettings> list;
	QString activeId;
	QString instructions;
	bool showEntryPoints = true;
	bool showSpending = false;
	RowPreview rowPreview = RowPreview::LastMessage;
};

[[nodiscard]] QByteArray SerializeProviders(const ProvidersState &state);
[[nodiscard]] std::optional<ProvidersState> DeserializeProviders(
	const QByteArray &bytes);

[[nodiscard]] const ProviderSettings *ActiveProvider(
	const ProvidersState &state);
[[nodiscard]] std::unique_ptr<Adapter> CreateAdapter(
	const ProviderSettings &settings);

} // namespace Agent
