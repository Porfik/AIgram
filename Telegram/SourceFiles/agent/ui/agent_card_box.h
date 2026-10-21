/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_types.h"
#include "base/basic_types.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

using namespace ::Ui;

struct CardBoxArgs {
	Card card;
	bool run = false;
	Fn<void(CardDecision decision)> decide;
	Fn<void(QString text)> handoff;
	Fn<std::optional<MediaBlock>(const QString &attachment)> attachment;
};

void ShowCardBox(
	not_null<Window::SessionController*> controller,
	CardBoxArgs args);

} // namespace Agent::Ui
