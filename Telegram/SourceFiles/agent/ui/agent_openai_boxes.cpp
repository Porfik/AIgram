/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_openai_boxes.h"

namespace Agent::Ui {

void ShowOpenAiChoice(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
}

void ShowEditChatGpt(
		not_null<Window::SessionController*> controller,
		const ProviderSettings &provider) {
}

void ShowEditOpenAiKey(
		not_null<Window::SessionController*> controller,
		const ProviderSettings &provider) {
}

} // namespace Agent::Ui
