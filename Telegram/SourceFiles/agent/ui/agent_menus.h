/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

class PeerData;

namespace Dialogs {
struct EntryState;
} // namespace Dialogs

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace HistoryView {
struct ContextMenuRequest;
class ListWidget;
} // namespace HistoryView

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

// Both return true for agent chat messages, which skip the usual action.
bool AddAgentReplyAction(
	not_null<::Ui::PopupMenu*> menu,
	const HistoryView::ContextMenuRequest &request,
	not_null<HistoryView::ListWidget*> list);
bool AddAgentForwardAction(
	not_null<::Ui::PopupMenu*> menu,
	const HistoryView::ContextMenuRequest &request,
	not_null<HistoryView::ListWidget*> list);
void ShowForwardCopyBox(
	not_null<Window::SessionController*> controller,
	TextWithEntities text);

} // namespace Agent::Ui
