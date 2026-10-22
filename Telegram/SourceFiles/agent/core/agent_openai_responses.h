/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_adapter.h"
#include "agent/core/agent_openai.h"
#include "agent/core/agent_providers.h"

#include <QtCore/QJsonObject>

#include <optional>
#include <vector>

namespace Agent::OpenAi {

struct KnownModel {
	QString name;
	double priceInput = 0.;
	double priceOutput = 0.;
	double priceCached = 0.;
	int contextWindow = 0;
	bool reasoning = true;
	bool vision = true;
	bool files = true;
	std::vector<QString> effortLevels;
};

class ResponsesAdapter final : public Agent::Adapter {
public:
	explicit ResponsesAdapter(ProviderSettings settings);

	[[nodiscard]] HttpRequest prepare(const Request &request) const override;
	[[nodiscard]] std::unique_ptr<ResponseParser> parser() const override;

private:
	const ProviderSettings _settings;

};

struct ResponsesOptions {
	QString origin;
	bool plan = false;
};

[[nodiscard]] QJsonObject ResponsesRequestObject(
	const Request &request,
	const ResponsesOptions &options);
[[nodiscard]] std::unique_ptr<ResponseParser> ResponsesParser(
	const QString &model,
	const QString &origin);

[[nodiscard]] QByteArray ReasoningSignature(
	const QString &model,
	const QString &origin,
	const QJsonObject &item);
[[nodiscard]] std::optional<QJsonObject> ReasoningItem(
	const QByteArray &signature,
	const QString &model,
	const QString &origin);
[[nodiscard]] bool IsReasoningSignature(const QByteArray &signature);

[[nodiscard]] std::vector<QString> ResponsesEffortLevels();
[[nodiscard]] std::optional<KnownModel> FindKnownModel(const QString &id);
[[nodiscard]] bool SupportsReasoning(const QString &model);
[[nodiscard]] int ContextWindowLimit();

[[nodiscard]] HttpRequest ResponsesModelsRequest(
	const ProviderSettings &settings);
[[nodiscard]] std::vector<ModelInfo> ParseResponsesModels(
	const QByteArray &json);
[[nodiscard]] Error ParseResponsesError(
	const HttpHead &head,
	const QByteArray &body);
[[nodiscard]] ErrorKind ErrorKindForCode(
	const QString &code,
	ErrorKind fallback);

} // namespace Agent::OpenAi
