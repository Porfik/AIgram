/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_panel.h"

#include "info/info_content_widget.h"

namespace Agent::Ui {

std::shared_ptr<Info::ContentMemento> MakePanelMemento(
		not_null<PeerData*> peer) {
	return nullptr;
}

bool PanelAvailable(not_null<PeerData*> peer) {
	return false;
}

void TogglePanel(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
}

} // namespace Agent::Ui
