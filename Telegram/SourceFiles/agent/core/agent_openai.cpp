/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_openai.h"

namespace Agent::OpenAi {

Adapter::Adapter(ProviderSettings settings)
: _settings(std::move(settings)) {
}

HttpRequest Adapter::prepare(const Request &request) const {
	return HttpRequest();
}

std::unique_ptr<ResponseParser> Adapter::parser() const {
	return nullptr;
}

QByteArray SerializeRequest(const Request &request) {
	return QByteArray();
}

HttpRequest ModelsRequest(const ProviderSettings &settings) {
	return HttpRequest();
}

std::vector<ModelInfo> ParseModels(const QByteArray &json) {
	return {};
}

Error ParseError(const HttpHead &head, const QByteArray &body) {
	return Error();
}

} // namespace Agent::OpenAi
