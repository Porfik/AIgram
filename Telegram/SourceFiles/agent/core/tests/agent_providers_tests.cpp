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
	state.activeId = u"локальный"_q;
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
	if (a.activeId != b.activeId) {
		return u"activeId differs"_q;
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
			|| x.priceCached != y.priceCached) {
			return u"provider "_q + QString::number(i) + u" differs"_q;
		}
	}
	if (a.showEntryPoints != b.showEntryPoints) {
		return u"showEntryPoints differs"_q;
	} else if (a.showSpending != b.showSpending) {
		return u"showSpending differs"_q;
	} else if (a.rowPreview != b.rowPreview) {
		return u"rowPreview differs"_q;
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
					stream << qint32(5) << qint32(0) << QString() << QString();
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
				|| restored->activeId != u"old"_q) {
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
			if (ActiveProvider(state) != nullptr) {
				return u"expected nullptr for an empty list"_q;
			}
			auto first = ProviderSettings();
			first.id = u"first"_q;
			auto second = ProviderSettings();
			second.id = u"second"_q;
			state.list = { first, second };
			if (ActiveProvider(state) != &state.list[0]) {
				return u"expected the first provider as fallback"_q;
			}
			state.activeId = u"second"_q;
			if (ActiveProvider(state) != &state.list[1]) {
				return u"expected the provider with activeId"_q;
			}
			state.activeId = u"missing"_q;
			if (ActiveProvider(state) != &state.list[0]) {
				return u"expected the first provider as fallback"_q;
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
			if (CreateAdapter(settings)) {
				return u"expected nullptr for Anthropic"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
