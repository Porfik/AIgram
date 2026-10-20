/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_adapter.h"
#include "agent/core/agent_providers.h"

#include <QtCore/QJsonObject>

#include <optional>
#include <vector>

namespace Agent::Anthropic {

struct ModelPrices {
	double input = 0.;
	double output = 0.;
	double cached = 0.;
	double cacheWrite = 0.;
};

struct ModelInfo {
	QString id;
	QString name;
	int contextWindow = 0;
	int maxOutput = 0;
	bool vision = false;
	bool pdf = false;
	std::vector<QString> effortLevels;
	std::optional<ModelPrices> prices;
};

class Adapter final : public Agent::Adapter {
public:
	explicit Adapter(ProviderSettings settings);

	[[nodiscard]] HttpRequest prepare(const Request &request) const override;
	[[nodiscard]] std::unique_ptr<ResponseParser> parser() const override;

private:
	const ProviderSettings _settings;

};

[[nodiscard]] std::vector<QString> EffortLevels();
[[nodiscard]] std::optional<ModelPrices> KnownPrices(const QString &model);
[[nodiscard]] bool SupportsAdaptiveThinking(const QString &model);
[[nodiscard]] bool SupportsFallbacks(const QString &model);

[[nodiscard]] QJsonObject RequestObject(const Request &request);
[[nodiscard]] HttpRequest ModelsRequest(const ProviderSettings &settings);
[[nodiscard]] std::vector<ModelInfo> ParseModels(const QByteArray &json);
[[nodiscard]] Error ParseError(
	const HttpHead &head,
	const QByteArray &body);

} // namespace Agent::Anthropic
