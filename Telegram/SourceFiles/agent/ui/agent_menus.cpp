/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_menus.h"

#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"

namespace Agent::Ui {

void FillPeerMenu(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const ::Ui::Menu::MenuCallback &addAction) {
}

void FillFolderMenu(
		not_null<Window::SessionController*> controller,
		int folder,
		const ::Ui::Menu::MenuCallback &addAction) {
}

} // namespace Agent::Ui
