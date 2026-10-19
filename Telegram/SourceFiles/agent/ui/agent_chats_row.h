/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

void SetupChatsListRow(
	not_null<Window::SessionController*> controller,
	not_null<::Ui::VerticalLayout*> list,
	rpl::producer<bool> shown);

// Whether the general agent screen is open in this window right now.
void SetGeneralAgentShown(
	not_null<Window::SessionController*> controller,
	bool shown);
[[nodiscard]] rpl::producer<bool> GeneralAgentShownValue(
	not_null<Window::SessionController*> controller);

} // namespace Agent::Ui
