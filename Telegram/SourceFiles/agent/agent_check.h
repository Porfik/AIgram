/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {

void ShowCheckBox(
	not_null<Window::SessionController*> controller,
	ProviderSettings provider);

} // namespace Agent
