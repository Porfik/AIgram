/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_anthropic.h"

#include "agent/core/agent_anthropic_stream.h"

namespace Agent::Anthropic {
namespace {

class Parser final : public Agent::ResponseParser {
public:
	void head(const HttpHead &head) override {
	}
	void feed(const QByteArray &chunk) override {
	}
	[[nodiscard]] std::vector<StreamDelta> takeDeltas() override {
		return {};
	}
	[[nodiscard]] Response finish(const QString &networkError) override {
		auto result = Response();
		result.error = Error{
			ErrorKind::BadResponse,
			0,
			u"Not implemented."_q,
		};
		return result;
	}

};

} // namespace

Adapter::Adapter(ProviderSettings settings)
: _settings(std::move(settings)) {
}

HttpRequest Adapter::prepare(const Request &request) const {
	return {};
}

std::unique_ptr<ResponseParser> Adapter::parser() const {
	return std::make_unique<Parser>();
}

std::vector<QString> EffortLevels() {
	return {};
}

std::optional<ModelPrices> KnownPrices(const QString &model) {
	return std::nullopt;
}

bool SupportsAdaptiveThinking(const QString &model) {
	return false;
}

bool SupportsFallbacks(const QString &model) {
	return false;
}

QJsonObject RequestObject(const Request &request) {
	return {};
}

HttpRequest ModelsRequest(const ProviderSettings &settings) {
	return {};
}

std::vector<ModelInfo> ParseModels(const QByteArray &json) {
	return {};
}

Error ParseError(const HttpHead &head, const QByteArray &body) {
	return {};
}

} // namespace Agent::Anthropic
