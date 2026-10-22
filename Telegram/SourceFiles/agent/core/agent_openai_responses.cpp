/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_openai_responses.h"

namespace Agent::OpenAi {
namespace {

class StubParser final : public ResponseParser {
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
			.kind = ErrorKind::BadResponse,
			.text = u"Not implemented."_q,
		};
		return result;
	}

};

} // namespace

ResponsesAdapter::ResponsesAdapter(ProviderSettings settings)
: _settings(std::move(settings)) {
}

HttpRequest ResponsesAdapter::prepare(const Request &request) const {
	return HttpRequest{ .url = _settings.baseUrl + u"/responses"_q };
}

std::unique_ptr<ResponseParser> ResponsesAdapter::parser() const {
	return ResponsesParser(_settings.model, _settings.id);
}

QJsonObject ResponsesRequestObject(
		const Request &request,
		const ResponsesOptions &options) {
	return QJsonObject();
}

std::unique_ptr<ResponseParser> ResponsesParser(
		const QString &model,
		const QString &origin) {
	return std::make_unique<StubParser>();
}

QByteArray ReasoningSignature(
		const QString &model,
		const QString &origin,
		const QJsonObject &item) {
	return QByteArray();
}

std::optional<QJsonObject> ReasoningItem(
		const QByteArray &signature,
		const QString &model,
		const QString &origin) {
	return std::nullopt;
}

bool IsReasoningSignature(const QByteArray &signature) {
	return false;
}

std::vector<QString> ResponsesEffortLevels() {
	return {};
}

std::optional<KnownModel> FindKnownModel(const QString &id) {
	return std::nullopt;
}

bool SupportsReasoning(const QString &model) {
	return true;
}

int ContextWindowLimit() {
	return 272'000;
}

HttpRequest ResponsesModelsRequest(const ProviderSettings &settings) {
	return HttpRequest{ .url = settings.baseUrl + u"/models"_q, .post = false };
}

std::vector<ModelInfo> ParseResponsesModels(const QByteArray &json) {
	return {};
}

Error ParseResponsesError(const HttpHead &head, const QByteArray &body) {
	return Error{ .kind = ErrorKind::BadResponse, .httpStatus = head.status };
}

ErrorKind ErrorKindForCode(const QString &code, ErrorKind fallback) {
	return fallback;
}

} // namespace Agent::OpenAi
