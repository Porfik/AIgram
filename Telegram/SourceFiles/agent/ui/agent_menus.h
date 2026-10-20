/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Dialogs {
struct EntryState;
} // namespace Dialogs

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

void FillPeerMenu(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const ::Ui::Menu::MenuCallback &addAction);
[[nodiscard]] bool FillAgentChatMenu(
	not_null<Window::SessionController*> controller,
	const Dialogs::EntryState &request,
	const ::Ui::Menu::MenuCallback &addAction);
void FillFolderMenu(
	not_null<Window::SessionController*> controller,
	int folder,
	const ::Ui::Menu::MenuCallback &addAction);

} // namespace Agent::Ui
