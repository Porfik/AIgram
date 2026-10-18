/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_providers.h"

#include "agent/core/agent_adapter.h"
#include "agent/core/agent_openai.h"
#include "base/assertion.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

namespace Agent {
namespace {

constexpr auto kFormatVersion = qint32(1);
constexpr auto kMaxProviders = 1000;

} // namespace

QByteArray SerializeProviders(const ProvidersState &state) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kFormatVersion << qint32(state.list.size());
	for (const auto &provider : state.list) {
		stream
			<< provider.id
			<< qint32(provider.kind)
			<< provider.title
			<< provider.baseUrl
			<< provider.apiKey
			<< provider.model
			<< qint32(provider.contextWindow)
			<< qint32(provider.vision ? 1 : 0);
	}
	stream << state.activeId << state.instructions;
	return result;
}

std::optional<ProvidersState> DeserializeProviders(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok || version != kFormatVersion) {
		return std::nullopt;
	}
	auto count = qint32(0);
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count < 0
		|| count > kMaxProviders) {
		return std::nullopt;
	}
	auto state = ProvidersState();
	state.list.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto kind = qint32(0);
		auto contextWindow = qint32(0);
		auto vision = qint32(0);
		auto provider = ProviderSettings();
		stream
			>> provider.id
			>> kind
			>> provider.title
			>> provider.baseUrl
			>> provider.apiKey
			>> provider.model
			>> contextWindow
			>> vision;
		if (stream.status() != QDataStream::Ok
			|| kind < 0
			|| kind > int(ProviderKind::Anthropic)) {
			return std::nullopt;
		}
		provider.kind = ProviderKind(kind);
		provider.contextWindow = contextWindow;
		provider.vision = (vision != 0);
		state.list.push_back(std::move(provider));
	}
	stream >> state.activeId >> state.instructions;
	if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
		return std::nullopt;
	}
	return state;
}

const ProviderSettings *ActiveProvider(const ProvidersState &state) {
	for (const auto &provider : state.list) {
		if (provider.id == state.activeId) {
			return &provider;
		}
	}
	return state.list.empty() ? nullptr : &state.list.front();
}

std::unique_ptr<Adapter> CreateAdapter(const ProviderSettings &settings) {
	switch (settings.kind) {
	case ProviderKind::OpenAiCompatible:
		return std::make_unique<OpenAi::Adapter>(settings);
	case ProviderKind::Anthropic:
		return nullptr;
	}
	Unexpected("Kind in Agent::CreateAdapter.");
}

} // namespace Agent
