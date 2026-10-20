/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class PeerData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

bool ShowPalette(
	not_null<Window::SessionController*> controller,
	HistoryItemsList passed = {});

[[nodiscard]] bool CanPassToAgent(const HistoryItemsList &items);
[[nodiscard]] bool CanPassChatToAgent(not_null<PeerData*> peer);
bool PassToAgent(
	not_null<Window::SessionController*> controller,
	const MessageIdsList &ids);
void AddPassToAgentAction(
	not_null<::Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	const MessageIdsList &ids,
	bool selected,
	Fn<void()> passed = nullptr);

} // namespace Agent::Ui
