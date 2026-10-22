/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_providers.h"

#include "agent/core/agent_adapter.h"
#include "agent/core/agent_anthropic.h"
#include "agent/core/agent_openai.h"
#include "agent/core/agent_openai_responses.h"
#include "base/assertion.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

namespace Agent {
namespace {

constexpr auto kFormatVersion = qint32(10);
constexpr auto kMinFormatVersion = qint32(1);
constexpr auto kMaxProviders = 1000;
constexpr auto kMaxIgnoredRoutes = 100;

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
			<< qint32(provider.vision ? 1 : 0)
			<< provider.priceInput
			<< provider.priceOutput
			<< provider.priceCached
			<< qint32(provider.audio ? 1 : 0)
			<< qint32(provider.video ? 1 : 0)
			<< qint32(provider.files ? 1 : 0)
			<< qint32(provider.service)
			<< qint32(provider.ignoredRoutes.size());
		for (const auto &route : provider.ignoredRoutes) {
			stream << route;
		}
		stream << provider.effort << provider.priceCacheWrite;
	}
	stream << state.defaultId << state.instructions;
	stream << qint32(state.showEntryPoints ? 1 : 0);
	stream << qint32(state.showSpending ? 1 : 0);
	stream << qint32(state.rowPreview);
	stream << state.mediaId;
	stream
		<< state.backgroundId
		<< state.backgroundDailyCost
		<< qint64(state.backgroundDailyTokens);
	stream << state.claudePath;
	stream << state.agentName << state.agentPhoto;
	stream << qint32(state.showBotIcon ? 1 : 0);
	return result;
}

std::optional<ProvidersState> DeserializeProviders(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < kMinFormatVersion
		|| version > kFormatVersion) {
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
			|| kind > int(ProviderKind::ClaudeCode)) {
			return std::nullopt;
		}
		provider.kind = ProviderKind(kind);
		provider.contextWindow = contextWindow;
		provider.vision = (vision != 0);
		if (version >= 3) {
			stream
				>> provider.priceInput
				>> provider.priceOutput
				>> provider.priceCached;
			if (stream.status() != QDataStream::Ok) {
				return std::nullopt;
			}
		}
		if (version >= 5) {
			auto audio = qint32(0);
			auto video = qint32(0);
			auto files = qint32(0);
			stream >> audio >> video >> files;
			if (stream.status() != QDataStream::Ok
				|| audio < 0 || audio > 1
				|| video < 0 || video > 1
				|| files < 0 || files > 1) {
				return std::nullopt;
			}
			provider.audio = (audio != 0);
			provider.video = (video != 0);
			provider.files = (files != 0);
		}
		if (version >= 7) {
			auto service = qint32(0);
			auto routes = qint32(0);
			stream >> service >> routes;
			if (stream.status() != QDataStream::Ok
				|| service < 0
				|| service > int(ProviderService::ClaudeSubscription)
				|| routes < 0
				|| routes > kMaxIgnoredRoutes) {
				return std::nullopt;
			}
			provider.service = ProviderService(service);
			auto ignored = std::vector<QString>();
			ignored.reserve(routes);
			for (auto i = 0; i != routes; ++i) {
				auto route = QString();
				stream >> route;
				if (stream.status() != QDataStream::Ok) {
					return std::nullopt;
				}
				ignored.push_back(std::move(route));
			}
			provider.ignoredRoutes = std::move(ignored);
		}
		if (version >= 8) {
			stream >> provider.effort >> provider.priceCacheWrite;
			if (stream.status() != QDataStream::Ok) {
				return std::nullopt;
			}
		}
		state.list.push_back(std::move(provider));
	}
	stream >> state.defaultId >> state.instructions;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	if (version >= 2) {
		auto showEntryPoints = qint32(0);
		stream >> showEntryPoints;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		state.showEntryPoints = (showEntryPoints != 0);
	} else {
		state.showEntryPoints = true;
	}
	if (version >= 3) {
		auto showSpending = qint32(0);
		stream >> showSpending;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		state.showSpending = (showSpending != 0);
	}
	if (version >= 4) {
		auto rowPreview = qint32(0);
		stream >> rowPreview;
		if (stream.status() != QDataStream::Ok
			|| rowPreview < 0
			|| rowPreview > qint32(RowPreview::Hint)) {
			return std::nullopt;
		}
		state.rowPreview = RowPreview(rowPreview);
	}
	if (version >= 5) {
		stream >> state.mediaId;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
	}
	if (version >= 6) {
		auto tokens = qint64(0);
		stream
			>> state.backgroundId
			>> state.backgroundDailyCost
			>> tokens;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		state.backgroundDailyTokens = tokens;
	}
	if (version >= 8) {
		stream >> state.claudePath;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
	}
	if (version >= 9) {
		stream >> state.agentName >> state.agentPhoto;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
	}
	if (version >= 10) {
		auto showBotIcon = qint32(0);
		stream >> showBotIcon;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		state.showBotIcon = (showBotIcon != 0);
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return state;
}

QString OpenRouterBaseUrl() {
	return u"https://openrouter.ai/api/v1"_q;
}

QString AnthropicBaseUrl() {
	return u"https://api.anthropic.com/v1"_q;
}

QString OpenAiBaseUrl() {
	return u"https://api.openai.com/v1"_q;
}

const ProviderSettings *FindProvider(
		const ProvidersState &state,
		const QString &id) {
	if (id.isEmpty()) {
		return nullptr;
	}
	for (const auto &provider : state.list) {
		if (provider.id == id) {
			return &provider;
		}
	}
	return nullptr;
}

const ProviderSettings *DefaultProvider(const ProvidersState &state) {
	if (const auto result = FindProvider(state, state.defaultId)) {
		return result;
	}
	return state.list.empty() ? nullptr : &state.list.front();
}

const ProviderSettings *SessionProvider(
		const ProvidersState &state,
		const QString &profileId) {
	if (const auto result = FindProvider(state, profileId)) {
		return result;
	}
	return DefaultProvider(state);
}

const ProviderSettings *MediaProvider(const ProvidersState &state) {
	return FindProvider(state, state.mediaId);
}

const ProviderSettings *BackgroundProvider(
		const ProvidersState &state,
		const QString &taskProviderId) {
	if (const auto result = FindProvider(state, taskProviderId)) {
		return result;
	} else if (const auto result = FindProvider(state, state.backgroundId)) {
		return result;
	}
	return DefaultProvider(state);
}

QString ServiceKey(const ProvidersState &state, ProviderService service) {
	if (service == ProviderService::Custom) {
		return QString();
	}
	for (const auto &provider : state.list) {
		if (provider.service == service) {
			return provider.apiKey;
		}
	}
	return QString();
}

void SetServiceKey(
		ProvidersState &state,
		ProviderService service,
		const QString &key) {
	Expects(service != ProviderService::Custom);

	for (auto &provider : state.list) {
		if (provider.service == service) {
			provider.apiKey = key;
		}
	}
}

bool IsOpenRouter(const ProviderSettings &settings) {
	const auto host = QUrl(settings.baseUrl).host();
	return host == u"openrouter.ai"_q
		|| host.endsWith(u".openrouter.ai"_q);
}

bool IsClaudeOnOpenRouter(const ProviderSettings &settings) {
	return IsOpenRouter(settings)
		&& settings.model.startsWith(u"anthropic/"_q, Qt::CaseInsensitive);
}

bool UsesClaudeCode(const ProviderSettings &settings) {
	return (settings.kind == ProviderKind::ClaudeCode);
}

bool UsesChatGptPlan(const ProviderSettings &settings) {
	return (settings.service == ProviderService::ChatGptSubscription);
}

bool IsSubscription(const ProviderSettings &settings) {
	return (settings.service == ProviderService::ClaudeSubscription)
		|| (settings.service == ProviderService::ChatGptSubscription);
}

std::unique_ptr<Adapter> CreateAdapter(const ProviderSettings &settings) {
	switch (settings.kind) {
	case ProviderKind::OpenAiCompatible:
		return std::make_unique<OpenAi::Adapter>(settings);
	case ProviderKind::Anthropic:
		return std::make_unique<Anthropic::Adapter>(settings);
	case ProviderKind::ClaudeCode:
		return nullptr;
	case ProviderKind::OpenAiResponses:
		return std::make_unique<OpenAi::ResponsesAdapter>(settings);
	}
	Unexpected("Kind in Agent::CreateAdapter.");
}

} // namespace Agent
