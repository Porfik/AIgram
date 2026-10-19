/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_menus.h"

#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"

namespace Agent::Ui {

void FillPeerMenu(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const ::Ui::Menu::MenuCallback &addAction) {
	const auto &agent = controller->session().agent();
	if (!agent.available()) {
		return;
	}
	const auto listed = agent.exclusions().peerListed(peer->id.value);
	addAction(
		listed
			? tr::lng_agent_show_chat(tr::now)
			: tr::lng_agent_hide_chat(tr::now),
		[=] {
			const auto &agent = controller->session().agent();
			const auto wasListed
				= agent.exclusions().peerListed(peer->id.value);
			agent.exclusions().setPeerExcluded(peer->id.value, !wasListed);
			if (wasListed) {
				controller->showToast(tr::lng_agent_chat_shown(tr::now));
			} else {
				controller->showToast(tr::lng_agent_chat_hidden(tr::now));
			}
		},
		listed ? &st::menuIconRestore : &st::menuIconRemove);
}

void FillFolderMenu(
		not_null<Window::SessionController*> controller,
		int folder,
		const ::Ui::Menu::MenuCallback &addAction) {
	if (!folder) {
		return;
	}
	const auto &agent = controller->session().agent();
	if (!agent.available()) {
		return;
	}
	const auto listed = agent.exclusions().folderExcluded(folder);
	addAction(
		listed
			? tr::lng_agent_show_folder(tr::now)
			: tr::lng_agent_hide_folder(tr::now),
		[=] {
			const auto &agent = controller->session().agent();
			const auto wasListed = agent.exclusions().folderExcluded(folder);
			agent.exclusions().setFolderExcluded(folder, !wasListed);
			if (wasListed) {
				controller->showToast(tr::lng_agent_folder_shown(tr::now));
			} else {
				controller->showToast(tr::lng_agent_folder_hidden(tr::now));
			}
		},
		listed ? &st::menuIconRestore : &st::menuIconRemove);
}

} // namespace Agent::Ui
