/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"

#include <rpl/producer.h>

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

void ShowAnthropicChoice(
	not_null<Window::SessionController*> controller,
	Fn<void(QString id)> added);
void ShowEditClaude(
	not_null<Window::SessionController*> controller,
	const ProviderSettings &provider);
void ShowEditAnthropic(
	not_null<Window::SessionController*> controller,
	const ProviderSettings &provider);

void AddClaudeLimits(not_null<::Ui::VerticalLayout*> container);
[[nodiscard]] rpl::producer<QString> ClaudeLimitValue(bool week);
[[nodiscard]] QString EffortText(const QString &effort);

} // namespace Agent::Ui
