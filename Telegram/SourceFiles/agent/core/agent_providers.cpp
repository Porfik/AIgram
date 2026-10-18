/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_providers.h"

#include "agent/core/agent_adapter.h"

namespace Agent {

QByteArray SerializeProviders(const ProvidersState &state) {
	return QByteArray();
}

std::optional<ProvidersState> DeserializeProviders(const QByteArray &bytes) {
	return std::nullopt;
}

const ProviderSettings *ActiveProvider(const ProvidersState &state) {
	return nullptr;
}

std::unique_ptr<Adapter> CreateAdapter(const ProviderSettings &settings) {
	return nullptr;
}

} // namespace Agent
