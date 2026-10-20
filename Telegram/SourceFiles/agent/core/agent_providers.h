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

enum class ProviderService : uchar {
	Custom,
	OpenRouter,
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
	bool audio = false;
	bool video = false;
	bool files = false;
	ProviderService service = ProviderService::Custom;
	std::vector<QString> ignoredRoutes;
};

enum class RowPreview : uchar {
	LastMessage,
	SessionTitle,
	Hint,
};

struct ProvidersState {
	std::vector<ProviderSettings> list;
	QString defaultId;
	QString instructions;
	bool showEntryPoints = true;
	bool showSpending = false;
	RowPreview rowPreview = RowPreview::LastMessage;
	QString mediaId;
	QString backgroundId;
	double backgroundDailyCost = 1.;
	qint64 backgroundDailyTokens = 1'000'000;
};

[[nodiscard]] QByteArray SerializeProviders(const ProvidersState &state);
[[nodiscard]] std::optional<ProvidersState> DeserializeProviders(
	const QByteArray &bytes);

[[nodiscard]] QString OpenRouterBaseUrl();

[[nodiscard]] const ProviderSettings *FindProvider(
	const ProvidersState &state,
	const QString &id);
[[nodiscard]] const ProviderSettings *DefaultProvider(
	const ProvidersState &state);
[[nodiscard]] const ProviderSettings *SessionProvider(
	const ProvidersState &state,
	const QString &profileId);
[[nodiscard]] const ProviderSettings *MediaProvider(
	const ProvidersState &state);
[[nodiscard]] const ProviderSettings *BackgroundProvider(
	const ProvidersState &state,
	const QString &taskProviderId = QString());
[[nodiscard]] QString ServiceKey(
	const ProvidersState &state,
	ProviderService service);
void SetServiceKey(
	ProvidersState &state,
	ProviderService service,
	const QString &key);
[[nodiscard]] bool IsOpenRouter(const ProviderSettings &settings);
[[nodiscard]] std::unique_ptr<Adapter> CreateAdapter(
	const ProviderSettings &settings);

} // namespace Agent
