/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"

#include <QtCore/QString>

namespace Agent {

[[nodiscard]] ProvidersState LoadProviders();
void SaveProviders(const ProvidersState &state);

[[nodiscard]] QString GenerateProviderId();

} // namespace Agent
