/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_settings.h"

#include "base/basic_types.h"
#include "base/random.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

#include <rpl/event_stream.h>

namespace Agent {
namespace {

[[nodiscard]] QString FormatHexId(uint64 value) {
	return u"%1"_q.arg(value, 16, 16, QChar('0'));
}

[[nodiscard]] rpl::event_stream<> &ProvidersChangedStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

void WriteProviders(const ProvidersState &state) {
	if (!Core::App().domain().local().writeAgentSettings(
			SerializeProviders(state))) {
		LOG(("Agent Error: could not save the provider settings."));
	}
}

} // namespace

ProvidersState LoadProviders() {
	const auto bytes = Core::App().domain().local().readAgentSettings();
	if (!bytes) {
		return {};
	}
	auto state = DeserializeProviders(*bytes);
	return state ? std::move(*state) : ProvidersState();
}

void SaveProviders(const ProvidersState &state) {
	auto copy = state;
	copy.chatGpt = LoadProviders().chatGpt;
	WriteProviders(copy);
	ProvidersChangedStream().fire({});
}

void SaveChatGptAccount(const ChatGptAccount &account) {
	auto state = LoadProviders();
	state.chatGpt = account;
	WriteProviders(state);
}

void UpdateProviders(Fn<void(ProvidersState &state)> change) {
	auto state = LoadProviders();
	change(state);
	SaveProviders(state);
}

rpl::producer<> ProvidersChanged() {
	return ProvidersChangedStream().events();
}

QString GenerateProviderId() {
	return FormatHexId(base::RandomValue<uint64>());
}

} // namespace Agent
