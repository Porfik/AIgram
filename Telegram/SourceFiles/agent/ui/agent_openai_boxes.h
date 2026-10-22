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

namespace Agent::Ui {

void ShowOpenAiChoice(
	not_null<Window::SessionController*> controller,
	Fn<void(QString id)> added);
void ShowEditChatGpt(
	not_null<Window::SessionController*> controller,
	const ProviderSettings &provider);
void ShowEditOpenAiKey(
	not_null<Window::SessionController*> controller,
	const ProviderSettings &provider);

} // namespace Agent::Ui
