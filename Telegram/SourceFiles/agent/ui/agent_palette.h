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

namespace Data {
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

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
// Removes the agent chat from forward targets and passes the messages to it.
bool TakeAgentTargets(
	std::vector<not_null<Data::Thread*>> &threads,
	not_null<Main::Session*> session,
	const MessageIdsList &ids);
void AddPassToAgentAction(
	not_null<::Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	const MessageIdsList &ids,
	bool selected,
	Fn<void()> passed = nullptr);

} // namespace Agent::Ui
