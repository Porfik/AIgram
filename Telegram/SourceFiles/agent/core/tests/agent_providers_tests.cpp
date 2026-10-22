/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_adapter.h"
#include "agent/core/agent_providers.h"

#include <QtCore/QDataStream>

namespace Agent::Tests {
namespace {

[[nodiscard]] QByteArray Written(Fn<void(QDataStream &)> write) {
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	write(stream);
	return bytes;
}

[[nodiscard]] ProvidersState SampleState() {
	auto state = ProvidersState();
	state.list = {
		{
			u"openai"_q,
			ProviderKind::OpenAiCompatible,
			u"OpenAI"_q,
			u"https://api.openai.com/v1"_q,
			u"sk-test-key"_q,
			u"gpt-5"_q,
			400000,
			true,
		},
		{
			u"локальный"_q,
			ProviderKind::OpenAiCompatible,
			u"Локальный сервер"_q,
			QString(),
			QString(),
			u"qwen3"_q,
			32768,
			false,
		},
		{
			u"anthropic"_q,
			ProviderKind::Anthropic,
			QString(),
			QString(),
			QString(),
			QString(),
			0,
			false,
		},
	};
	state.defaultId = u"локальный"_q;
	state.instructions = u"Отвечай по-русски."_q;
	state.showEntryPoints = false;
	state.showSpending = true;
	state.rowPreview = RowPreview::SessionTitle;
	state.list[0].priceInput = 0.5;
	state.list[0].priceOutput = 1.5;
	state.list[0].priceCached = 0.05;
	state.list[1].priceInput = 0.1;
	return state;
}

[[nodiscard]] QString ProvidersEqual(
		const ProvidersState &a,
		const ProvidersState &b) {
	if (a.defaultId != b.defaultId) {
		return u"defaultId differs"_q;
	} else if (a.instructions != b.instructions) {
		return u"instructions differ"_q;
	} else if (a.list.size() != b.list.size()) {
		return u"list size differs"_q;
	}
	for (auto i = 0, count = int(a.list.size()); i != count; ++i) {
		const auto &x = a.list[i];
		const auto &y = b.list[i];
		if (x.id != y.id
			|| x.kind != y.kind
			|| x.title != y.title
			|| x.baseUrl != y.baseUrl
			|| x.apiKey != y.apiKey
			|| x.model != y.model
			|| x.contextWindow != y.contextWindow
			|| x.vision != y.vision
			|| x.priceInput != y.priceInput
			|| x.priceOutput != y.priceOutput
			|| x.priceCached != y.priceCached
			|| x.effort != y.effort
			|| x.priceCacheWrite != y.priceCacheWrite) {
			return u"provider "_q + QString::number(i) + u" differs"_q;
		}
	}
	if (a.showEntryPoints != b.showEntryPoints) {
		return u"showEntryPoints differs"_q;
	} else if (a.showSpending != b.showSpending) {
		return u"showSpending differs"_q;
	} else if (a.rowPreview != b.rowPreview) {
		return u"rowPreview differs"_q;
	} else if (a.claudePath != b.claudePath) {
		return u"claudePath differs"_q;
	} else if (a.agentName != b.agentName) {
		return u"agentName differs"_q;
	} else if (a.agentPhoto != b.agentPhoto) {
		return u"agentPhoto differs"_q;
	} else if (a.showBotIcon != b.showBotIcon) {
		return u"showBotIcon differs"_q;
	}
	return QString();
}

} // namespace

std::vector<Check> ProvidersChecks() {
	return {
		{ u"providers_round_trip"_q, [] {
			const auto state = SampleState();
			const auto bytes = SerializeProviders(state);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			const auto diff = ProvidersEqual(state, *restored);
			if (!diff.isEmpty()) {
				return diff;
			}
			const auto again = SerializeProviders(*restored);
			if (again != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"providers_reject_bad_versions_and_empty"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(),
				Written([](QDataStream &stream) {
					stream << qint32(0) << qint32(0) << QString() << QString();
				}),
				Written([](QDataStream &stream) {
					stream << qint32(9) << qint32(0) << QString() << QString();
				}),
				Written([](QDataStream &stream) {
					stream << QString() << qint32(0) << QString() << QString();
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeProviders(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
		{ u"providers_reject_bad_counts"_q, [] {
			const auto bad = std::vector<QByteArray>{
				Written([](QDataStream &stream) {
					stream << qint32(2) << qint32(-1) << QString() << QString();
				}),
				Written([](QDataStream &stream) {
					stream
						<< qint32(2)
						<< qint32(1001)
						<< QString()
						<< QString();
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeProviders(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
		{ u"providers_reject_bad_kinds"_q, [] {
			const auto bad = std::vector<QByteArray>{
				Written([](QDataStream &stream) {
					stream
						<< qint32(1)
						<< qint32(1)
						<< u"x"_q
						<< qint32(7)
						<< QString()
						<< QString()
						<< QString()
						<< QString()
						<< qint32(0)
						<< qint32(0)
						<< QString()
						<< QString();
				}),
				Written([](QDataStream &stream) {
					stream
						<< qint32(1)
						<< qint32(1)
						<< u"x"_q
						<< qint32(-1)
						<< QString()
						<< QString()
						<< QString()
						<< QString()
						<< qint32(0)
						<< qint32(0)
						<< QString()
						<< QString();
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeProviders(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
		{ u"providers_reject_truncated_and_garbage"_q, [] {
			const auto good = SerializeProviders(SampleState());
			const auto bad = std::vector<QByteArray>{
				good.mid(0, good.size() - 1),
				good.mid(0, good.size() / 2),
				good + QByteArray("x"),
				good + QByteArray("\x00\x00\x00\x00", 4),
			};
			for (const auto &bytes : bad) {
				if (DeserializeProviders(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
		{ u"providers_show_entry_points_old_version"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(1)
					<< qint32(1)
					<< u"old"_q
					<< qint32(0)
					<< u"Название"_q
					<< u"https://example.com/v1"_q
					<< u"key"_q
					<< u"model"_q
					<< qint32(8192)
					<< qint32(0)
					<< u"old"_q
					<< u"инструкции"_q;
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"old version failed to deserialize"_q;
			}
			if (!restored->showEntryPoints) {
				return u"expected showEntryPoints == true"_q;
			}
			if (restored->list.size() != 1
				|| restored->list[0].id != u"old"_q
				|| restored->defaultId != u"old"_q) {
				return u"unexpected restored state"_q;
			}
			return QString();
		} },
		{ u"providers_second_version_loads"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(2)
					<< qint32(1)
					<< u"old"_q
					<< qint32(0)
					<< u"Название"_q
					<< u"https://example.com/v1"_q
					<< u"key"_q
					<< u"model"_q
					<< qint32(8192)
					<< qint32(1)
					<< u"old"_q
					<< QString()
					<< qint32(1);
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"version 2 failed to deserialize"_q;
			}
			const auto &provider = restored->list[0];
			if (provider.priceInput != 0.
				|| provider.priceOutput != 0.
				|| provider.priceCached != 0.) {
				return u"version 2 must load zero prices"_q;
			} else if (!restored->showEntryPoints) {
				return u"version 2 lost showEntryPoints"_q;
			} else if (restored->showSpending) {
				return u"version 2 must load showSpending == false"_q;
			}
			return QString();
		} },
		{ u"providers_active_provider"_q, [] {
			auto state = ProvidersState();
			if (DefaultProvider(state) != nullptr) {
				return u"expected nullptr for an empty list"_q;
			}
			auto first = ProviderSettings();
			first.id = u"first"_q;
			auto second = ProviderSettings();
			second.id = u"second"_q;
			state.list = { first, second };
			if (DefaultProvider(state) != &state.list[0]) {
				return u"expected the first provider as fallback"_q;
			}
			state.defaultId = u"second"_q;
			if (DefaultProvider(state) != &state.list[1]) {
				return u"expected the provider with defaultId"_q;
			}
			state.defaultId = u"missing"_q;
			if (DefaultProvider(state) != &state.list[0]) {
				return u"expected the first provider as fallback"_q;
			}
			return QString();
		} },
		{ u"providers_v5_round_trip"_q, [] {
			auto state = SampleState();
			state.list[0].audio = true;
			state.list[0].video = true;
			state.list[0].files = false;
			state.list[1].audio = false;
			state.list[1].video = false;
			state.list[1].files = true;
			state.mediaId = u"openai"_q;
			const auto bytes = SerializeProviders(state);
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v5 failed to deserialize"_q;
			}
			const auto &first = restored->list[0];
			const auto &second = restored->list[1];
			if (!first.audio || !first.video || first.files) {
				return u"provider 1 flags differ"_q;
			} else if (second.audio || second.video || !second.files) {
				return u"provider 2 flags differ"_q;
			} else if (restored->mediaId != state.mediaId) {
				return u"mediaId differs"_q;
			}
			return QString();
		} },
		{ u"providers_v4_loads"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(4)
					<< qint32(1)
					<< u"old"_q
					<< qint32(0)
					<< u"Название"_q
					<< u"https://example.com/v1"_q
					<< u"key"_q
					<< u"model"_q
					<< qint32(8192)
					<< qint32(1)
					<< 0.1
					<< 0.2
					<< 0.3
					<< u"old"_q
					<< u"инструкции"_q
					<< qint32(1)
					<< qint32(0)
					<< qint32(0);
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"version 4 failed to deserialize"_q;
			}
			const auto &provider = restored->list[0];
			if (provider.audio || provider.video || provider.files) {
				return u"version 4 must load default media flags"_q;
			} else if (!restored->mediaId.isEmpty()) {
				return u"version 4 must load empty mediaId"_q;
			}
			return QString();
		} },
		{ u"providers_v6_round_trip"_q, [] {
			auto state = SampleState();
			state.backgroundId = u"openai"_q;
			state.backgroundDailyCost = 2.5;
			state.backgroundDailyTokens = 3'000'000;
			const auto restored = DeserializeProviders(
				SerializeProviders(state));
			if (!restored) {
				return u"v6 failed to deserialize"_q;
			} else if (restored->backgroundId != state.backgroundId
				|| restored->backgroundDailyCost != 2.5
				|| restored->backgroundDailyTokens != 3'000'000) {
				return u"background fields differ"_q;
			}
			return QString();
		} },
		{ u"providers_v5_loads_background_defaults"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(5)
					<< qint32(1)
					<< u"old"_q
					<< qint32(0)
					<< u"Название"_q
					<< u"https://example.com/v1"_q
					<< u"key"_q
					<< u"model"_q
					<< qint32(8192)
					<< qint32(1)
					<< 0.1
					<< 0.2
					<< 0.3
					<< qint32(0)
					<< qint32(0)
					<< qint32(0)
					<< u"old"_q
					<< u"инструкции"_q
					<< qint32(1)
					<< qint32(0)
					<< qint32(0)
					<< u"openai"_q;
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"version 5 failed to deserialize"_q;
			} else if (!restored->backgroundId.isEmpty()
				|| restored->backgroundDailyCost != 1.
				|| restored->backgroundDailyTokens != 1'000'000) {
				return u"version 5 must load background defaults"_q;
			} else if (restored->mediaId != u"openai"_q) {
				return u"version 5 mediaId lost"_q;
			}
			return QString();
		} },
		{ u"providers_background_provider"_q, [] {
			auto state = SampleState();
			const auto active = DefaultProvider(state);
			if (BackgroundProvider(state) != active) {
				return u"default must be the active provider"_q;
			}
			state.backgroundId = u"openai"_q;
			const auto background = BackgroundProvider(state);
			if (!background || background->id != u"openai"_q) {
				return u"backgroundId must win over the active one"_q;
			}
			const auto own = BackgroundProvider(state, active->id);
			if (own != active) {
				return u"the task provider must win"_q;
			}
			if (BackgroundProvider(state, u"missing"_q) != background) {
				return u"an unknown task provider must be skipped"_q;
			}
			state.backgroundId = u"missing"_q;
			if (BackgroundProvider(state) != active) {
				return u"an unknown backgroundId must be skipped"_q;
			}
			return QString();
		} },
		{ u"providers_media_provider"_q, [] {
			auto state = SampleState();
			state.mediaId = u"openai"_q;
			const auto media = MediaProvider(state);
			if (!media || media->id != u"openai"_q) {
				return u"media provider must be found"_q;
			}
			state.mediaId = u"missing"_q;
			if (MediaProvider(state)) {
				return u"unknown mediaId must yield nullptr"_q;
			}
			state.mediaId = QString();
			if (MediaProvider(state)) {
				return u"empty mediaId must yield nullptr"_q;
			}
			return QString();
		} },
		{ u"providers_is_open_router"_q, [] {
			const auto openRouter = [](const QString &baseUrl) {
				auto settings = ProviderSettings();
				settings.baseUrl = baseUrl;
				return IsOpenRouter(settings);
			};
			if (!openRouter(u"https://openrouter.ai/api/v1"_q)) {
				return u"openrouter.ai must be true"_q;
			} else if (!openRouter(u"https://sub.openrouter.ai/api/v1"_q)) {
				return u"subdomains of openrouter.ai must be true"_q;
			} else if (openRouter(u"https://api.openai.com/v1"_q)) {
				return u"api.openai.com must be false"_q;
			} else if (openRouter(
					u"https://openrouter.ai.evil.com/api/v1"_q)) {
				return u"openrouter.ai.evil.com must be false"_q;
			}
			return QString();
		} },
		{ u"providers_create_adapter"_q, [] {
			auto settings = ProviderSettings();
			settings.kind = ProviderKind::OpenAiCompatible;
			if (!CreateAdapter(settings)) {
				return u"expected an adapter for OpenAiCompatible"_q;
			}
			settings.kind = ProviderKind::Anthropic;
			if (!CreateAdapter(settings)) {
				return u"expected an adapter for Anthropic"_q;
			}
			settings.kind = ProviderKind::ClaudeCode;
			if (CreateAdapter(settings)) {
				return u"expected nullptr for ClaudeCode"_q;
			} else if (!UsesClaudeCode(settings)) {
				return u"ClaudeCode must use the claude engine"_q;
			}
			return QString();
		} },
		{ u"providers_v8_round_trip"_q, [] {
			auto state = SampleState();
			state.list[2].service = ProviderService::Anthropic;
			state.list[2].baseUrl = AnthropicBaseUrl();
			state.list[2].effort = u"high"_q;
			state.list[2].priceCacheWrite = 5.;
			auto subscription = ProviderSettings();
			subscription.id = u"claude"_q;
			subscription.kind = ProviderKind::ClaudeCode;
			subscription.service = ProviderService::ClaudeSubscription;
			subscription.model = u"opus"_q;
			subscription.effort = u"xhigh"_q;
			state.list.push_back(subscription);
			state.claudePath = u"/opt/claude/bin/claude"_q;
			const auto bytes = SerializeProviders(state);
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v8 failed to deserialize"_q;
			}
			const auto diff = ProvidersEqual(state, *restored);
			if (!diff.isEmpty()) {
				return diff;
			} else if (restored->list[3].service
					!= ProviderService::ClaudeSubscription
				|| restored->list[3].kind != ProviderKind::ClaudeCode) {
				return u"the subscription profile must round-trip"_q;
			}
			return (SerializeProviders(*restored) == bytes)
				? QString()
				: u"re-serialization is not byte-identical"_q;
		} },
		{ u"providers_v9_round_trip"_q, [] {
			auto state = SampleState();
			state.agentName = u"Ассистент"_q;
			state.agentPhoto = QByteArray("\xff\xd8jpeg\0bytes", 12);
			const auto bytes = SerializeProviders(state);
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v9 failed to deserialize"_q;
			}
			const auto diff = ProvidersEqual(state, *restored);
			if (!diff.isEmpty()) {
				return diff;
			}
			auto empty = SampleState();
			const auto plain = DeserializeProviders(SerializeProviders(empty));
			if (!plain
				|| !plain->agentName.isEmpty()
				|| !plain->agentPhoto.isEmpty()) {
				return u"an unset agent name and photo must stay empty"_q;
			}
			return QString();
		} },
		{ u"providers_v10_bot_icon"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(10)
					<< qint32(0)
					<< u"old"_q
					<< u"инструкции"_q
					<< qint32(1)
					<< qint32(0)
					<< qint32(0)
					<< QString()
					<< QString()
					<< 1.0
					<< qint64(1'000'000)
					<< QString()
					<< QString()
					<< QByteArray()
					<< qint32(0);
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v10 data failed to deserialize"_q;
			} else if (restored->showBotIcon) {
				return u"a hidden bot icon must round-trip"_q;
			}
			const auto bytesV9 = Written([](QDataStream &stream) {
				stream
					<< qint32(9)
					<< qint32(0)
					<< u"old"_q
					<< u"инструкции"_q
					<< qint32(1)
					<< qint32(0)
					<< qint32(0)
					<< QString()
					<< QString()
					<< 1.0
					<< qint64(1'000'000)
					<< QString()
					<< QString()
					<< QByteArray();
			});
			const auto old = DeserializeProviders(bytesV9);
			if (!old) {
				return u"v9 data failed to deserialize"_q;
			} else if (!old->showBotIcon) {
				return u"v9 data must show the bot icon"_q;
			}
			return QString();
		} },
		{ u"providers_is_claude_on_open_router"_q, [] {
			auto settings = ProviderSettings();
			settings.baseUrl = OpenRouterBaseUrl();
			settings.model = u"anthropic/claude-opus-5.5"_q;
			if (!IsClaudeOnOpenRouter(settings)) {
				return u"anthropic/* on OpenRouter must match"_q;
			}
			settings.model = u"z-ai/glm-5.3-flash"_q;
			if (IsClaudeOnOpenRouter(settings)) {
				return u"other OpenRouter models must not match"_q;
			}
			settings.baseUrl = AnthropicBaseUrl();
			settings.model = u"anthropic/claude-opus-5.5"_q;
			if (IsClaudeOnOpenRouter(settings)) {
				return u"other hosts must not match"_q;
			}
			return QString();
		} },
		{ u"providers_v7_round_trip"_q, [] {
			auto state = SampleState();
			state.list[0].service = ProviderService::OpenRouter;
			state.list[0].baseUrl = OpenRouterBaseUrl();
			state.list[0].ignoredRoutes = {
				u"InferenceNet"_q,
				u"Novita"_q,
			};
			const auto bytes = SerializeProviders(state);
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v7 failed to deserialize"_q;
			}
			const auto diff = ProvidersEqual(state, *restored);
			if (!diff.isEmpty()) {
				return diff;
			} else if (restored->list[1].service
					!= ProviderService::Custom
				|| !restored->list[1].ignoredRoutes.empty()) {
				return u"the Custom profile must have no routes"_q;
			}
			return (SerializeProviders(*restored) == bytes)
				? QString()
				: u"re-serialization is not byte-identical"_q;
		} },
		{ u"providers_v6_loads_custom"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(6)
					<< qint32(1)
					<< u"old"_q
					<< qint32(0)
					<< u"Название"_q
					<< u"https://example.com/v1"_q
					<< u"key"_q
					<< u"model"_q
					<< qint32(8192)
					<< qint32(1)
					<< 0.1
					<< 0.2
					<< 0.3
					<< qint32(0)
					<< qint32(0)
					<< qint32(0)
					<< u"old"_q
					<< u"инструкции"_q
					<< qint32(1)
					<< qint32(0)
					<< qint32(0)
					<< QString()
					<< QString()
					<< 1.0
					<< qint64(1'000'000);
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"version 6 failed to deserialize"_q;
			}
			const auto &provider = restored->list[0];
			if (provider.service != ProviderService::Custom) {
				return u"version 6 must load Custom service"_q;
			} else if (!provider.ignoredRoutes.empty()) {
				return u"version 6 must load no routes"_q;
			} else if (restored->defaultId != u"old"_q) {
				return u"version 6 must keep defaultId"_q;
			}
			return QString();
		} },
		{ u"providers_v7_reject_bad_service"_q, [] {
			for (const auto service : { qint32(6), qint32(-1) }) {
				const auto bytes = Written([&](QDataStream &stream) {
					stream
						<< qint32(7)
						<< qint32(1)
						<< u"x"_q
						<< qint32(0)
						<< QString()
						<< QString()
						<< QString()
						<< QString()
						<< qint32(0)
						<< qint32(0)
						<< 0.
						<< 0.
						<< 0.
						<< qint32(0)
						<< qint32(0)
						<< qint32(0)
						<< service
						<< qint32(0)
						<< QString()
						<< QString()
						<< qint32(1);
				});
				if (DeserializeProviders(bytes)) {
					return u"service %1 must fail"_q.arg(service);
				}
			}
			return QString();
		} },
		{ u"providers_v7_reject_bad_route_count"_q, [] {
			for (const auto routes : { qint32(101), qint32(-1) }) {
				const auto bytes = Written([&](QDataStream &stream) {
					stream
						<< qint32(7)
						<< qint32(1)
						<< u"x"_q
						<< qint32(0)
						<< QString()
						<< QString()
						<< QString()
						<< QString()
						<< qint32(0)
						<< qint32(0)
						<< 0.
						<< 0.
						<< 0.
						<< qint32(0)
						<< qint32(0)
						<< qint32(0)
						<< qint32(0)
						<< routes
						<< QString()
						<< QString()
						<< qint32(1);
				});
				if (DeserializeProviders(bytes)) {
					return u"route count %1 must fail"_q.arg(routes);
				}
			}
			return QString();
		} },
		{ u"providers_v7_reject_truncated_routes"_q, [] {
			auto state = ProvidersState();
			auto provider = ProviderSettings();
			provider.id = u"router"_q;
			provider.service = ProviderService::OpenRouter;
			provider.ignoredRoutes = { u"A"_q, u"B"_q, u"C"_q };
			state.list.push_back(provider);
			const auto bytes = SerializeProviders(state);
			for (auto size = 0; size != bytes.size(); ++size) {
				if (DeserializeProviders(bytes.mid(0, size))) {
					return u"expected nullopt for truncation at "_q
						+ QString::number(size);
				}
			}
			return QString();
		} },
		{ u"providers_find_provider"_q, [] {
			auto state = SampleState();
			const auto found = FindProvider(state, u"openai"_q);
			if (!found || found->id != u"openai"_q) {
				return u"the provider must be found"_q;
			} else if (FindProvider(state, QString())) {
				return u"an empty id must yield nullptr"_q;
			} else if (FindProvider(state, u"missing"_q)) {
				return u"an unknown id must yield nullptr"_q;
			}
			return QString();
		} },
		{ u"providers_session_provider"_q, [] {
			auto state = SampleState();
			state.defaultId = u"openai"_q;
			const auto known = SessionProvider(state, u"anthropic"_q);
			if (!known || known->id != u"anthropic"_q) {
				return u"a known profileId must win"_q;
			} else if (SessionProvider(state, u"missing"_q)
					!= DefaultProvider(state)) {
				return u"an unknown profileId must fall back"_q;
			} else if (SessionProvider(state, QString())
					!= DefaultProvider(state)) {
				return u"an empty profileId must fall back"_q;
			}
			return QString();
		} },
		{ u"providers_service_key"_q, [] {
			auto state = SampleState();
			state.list[0].apiKey = u"sk-router"_q;
			state.list[0].service = ProviderService::OpenRouter;
			if (ServiceKey(state, ProviderService::OpenRouter)
					!= u"sk-router"_q) {
				return u"the OpenRouter key must be returned"_q;
			} else if (!ServiceKey(state, ProviderService::Custom).isEmpty()) {
				return u"Custom must have no key"_q;
			}
			auto empty = ProvidersState();
			if (!ServiceKey(empty, ProviderService::OpenRouter).isEmpty()) {
				return u"a missing service must have no key"_q;
			}
			return QString();
		} },
		{ u"providers_set_service_key"_q, [] {
			auto state = SampleState();
			state.list[1].apiKey = u"sk-custom"_q;
			state.list[0].service = ProviderService::OpenRouter;
			state.list[2].service = ProviderService::OpenRouter;
			SetServiceKey(state, ProviderService::OpenRouter, u"sk-new"_q);
			for (const auto &provider : state.list) {
				const auto expected = (provider.id == u"локальный"_q)
					? u"sk-custom"_q
					: u"sk-new"_q;
				if (provider.apiKey != expected) {
					return u"key of "_q + provider.id + u" is wrong"_q;
				}
			}
			return QString();
		} },
		{ u"providers_v11_round_trip"_q, [] {
			auto state = SampleState();
			state.chatGpt = ChatGptAccount{
				.clientId = u"client-id"_q,
				.hostId = u"urn:uuid:00000000-0000-4000-8000-000000000000"_q,
				.email = u"me@example.com"_q,
				.subject = u"subject"_q,
				.plan = u"plus"_q,
				.accessToken = u"at"_q,
				.refreshToken = u"rt"_q,
				.expiresAt = 1234567890123,
				.scopes = u"openid chatgpt.tokens.use.direct"_q,
			};
			auto subscription = ProviderSettings();
			subscription.id = u"openai-sub"_q;
			subscription.kind = ProviderKind::OpenAiResponses;
			subscription.service = ProviderService::ChatGptSubscription;
			subscription.baseUrl = OpenAiBaseUrl();
			auto key = ProviderSettings();
			key.id = u"openai-key"_q;
			key.kind = ProviderKind::OpenAiResponses;
			key.service = ProviderService::OpenAi;
			key.baseUrl = OpenAiBaseUrl();
			key.apiKey = u"sk-openai"_q;
			state.list.push_back(subscription);
			state.list.push_back(key);
			const auto bytes = SerializeProviders(state);
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v11 failed to deserialize"_q;
			}
			const auto &account = restored->chatGpt;
			if (account.clientId != u"client-id"
				|| account.hostId
					!= u"urn:uuid:00000000-0000-4000-8000-000000000000"_q
				|| account.email != u"me@example.com"
				|| account.subject != u"subject"
				|| account.plan != u"plus"
				|| account.accessToken != u"at"
				|| account.refreshToken != u"rt"
				|| account.expiresAt != 1234567890123
				|| account.scopes
					!= u"openid chatgpt.tokens.use.direct"_q) {
				return u"the account must round-trip"_q;
			}
			if (restored->list[3].kind != ProviderKind::OpenAiResponses
				|| restored->list[3].service
					!= ProviderService::ChatGptSubscription
				|| restored->list[4].service != ProviderService::OpenAi) {
				return u"the new kinds and services must round-trip"_q;
			}
			return (SerializeProviders(*restored) == bytes)
				? QString()
				: u"re-serialization is not byte-identical"_q;
		} },
		{ u"providers_v10_loads_empty_account"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream
					<< qint32(10)
					<< qint32(0)
					<< u"old"_q
					<< u"инструкции"_q
					<< qint32(1)
					<< qint32(0)
					<< qint32(0)
					<< QString()
					<< QString()
					<< 1.0
					<< qint64(1'000'000)
					<< QString()
					<< QString()
					<< QByteArray()
					<< qint32(1);
			});
			const auto restored = DeserializeProviders(bytes);
			if (!restored) {
				return u"v10 data failed to deserialize"_q;
			}
			const auto &account = restored->chatGpt;
			if (!account.clientId.isEmpty()
				|| !account.hostId.isEmpty()
				|| !account.email.isEmpty()
				|| !account.subject.isEmpty()
				|| !account.plan.isEmpty()
				|| !account.accessToken.isEmpty()
				|| !account.refreshToken.isEmpty()
				|| account.expiresAt != 0
				|| !account.scopes.isEmpty()) {
				return u"v10 must load an empty account"_q;
			}
			return QString();
		} },
		{ u"providers_v11_reject_beyond"_q, [] {
			const auto bad = std::vector<QByteArray>{
				Written([](QDataStream &stream) {
					stream
						<< qint32(11)
						<< qint32(1)
						<< u"x"_q
						<< qint32(4)
						<< QString()
						<< QString()
						<< QString()
						<< QString()
						<< qint32(0)
						<< qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream
						<< qint32(11)
						<< qint32(1)
						<< u"x"_q
						<< qint32(0)
						<< QString()
						<< QString()
						<< QString()
						<< QString()
						<< qint32(0)
						<< qint32(0)
						<< 0.
						<< 0.
						<< 0.
						<< qint32(0)
						<< qint32(0)
						<< qint32(0)
						<< qint32(6)
						<< qint32(0);
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeProviders(bytes)) {
					return u"a value beyond the last must fail"_q;
				}
			}
			return QString();
		} },
		{ u"providers_openai_subscription_helpers"_q, [] {
			auto settings = ProviderSettings();
			settings.kind = ProviderKind::OpenAiResponses;
			settings.service = ProviderService::OpenAi;
			settings.baseUrl = OpenAiBaseUrl();
			if (!CreateAdapter(settings)) {
				return u"expected an adapter for OpenAiResponses"_q;
			} else if (UsesChatGptPlan(settings) || IsSubscription(settings)) {
				return u"an api key profile is not a subscription"_q;
			}
			settings.service = ProviderService::ChatGptSubscription;
			settings.apiKey = QString();
			if (!UsesChatGptPlan(settings)) {
				return u"ChatGptSubscription must use the plan"_q;
			} else if (!IsSubscription(settings)) {
				return u"ChatGptSubscription must be a subscription"_q;
			}
			auto claude = ProviderSettings();
			claude.kind = ProviderKind::ClaudeCode;
			claude.service = ProviderService::ClaudeSubscription;
			if (!IsSubscription(claude) || UsesChatGptPlan(claude)) {
				return u"ClaudeSubscription must be its own subscription"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
