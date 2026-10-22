/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"

#include <rpl/producer.h>

#include <QtCore/QString>

namespace Agent {

[[nodiscard]] ProvidersState LoadProviders();
void SaveProviders(const ProvidersState &state);
void UpdateProviders(Fn<void(ProvidersState &state)> change);
void SaveChatGptAccount(const ChatGptAccount &account);
[[nodiscard]] rpl::producer<> ProvidersChanged();

[[nodiscard]] QString GenerateProviderId();

} // namespace Agent
