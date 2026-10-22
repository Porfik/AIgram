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

namespace Agent::OpenAi {

struct ModelInfo {
	QString id;
	QString name;
	int contextWindow = 0;
	std::optional<bool> tools;
	std::optional<bool> vision;
	std::optional<bool> audio;
	std::optional<bool> video;
	std::optional<bool> files;
	double priceInput = 0.;
	double priceOutput = 0.;
	double priceCached = 0.;
	std::vector<QString> effortLevels;
};

struct ModelRoute {
	QString provider;
	bool tools = false;
};

class Adapter final : public Agent::Adapter {
public:
	explicit Adapter(ProviderSettings settings);

	[[nodiscard]] HttpRequest prepare(const Request &request) const override;
	[[nodiscard]] std::unique_ptr<ResponseParser> parser() const override;

private:
	const ProviderSettings _settings;

};

[[nodiscard]] QByteArray SerializeRequest(const Request &request);
[[nodiscard]] QByteArray SerializeRequestWithCacheKey(
	const Request &request);
[[nodiscard]] QJsonObject RequestObject(const Request &request);
[[nodiscard]] HttpRequest ModelsRequest(const ProviderSettings &settings);
[[nodiscard]] std::vector<ModelInfo> ParseModels(const QByteArray &json);
[[nodiscard]] std::vector<int> SearchModels(
	const std::vector<ModelInfo> &models,
	const QString &query);
[[nodiscard]] HttpRequest EndpointsRequest(const ProviderSettings &settings);
[[nodiscard]] std::vector<ModelRoute> ParseEndpoints(const QByteArray &json);
[[nodiscard]] HttpRequest KeyRequest(const ProviderSettings &settings);
[[nodiscard]] Error ParseError(
	const HttpHead &head,
	const QByteArray &body);

} // namespace Agent::OpenAi
