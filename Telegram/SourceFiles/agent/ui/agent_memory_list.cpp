/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_memory_list.h"

#include "agent/agent_manager.h"
#include "window/window_session_controller.h"

namespace Agent::Ui {

MemoryList::MemoryList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	PeerKey owner)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _owner(owner) {
}

} // namespace Agent::Ui
