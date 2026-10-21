/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

#include <optional>

class Painter;
class PeerData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

void ShowRenameSessionBox(
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	const QString &id);
void ShowDeleteSessionBox(
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	const QString &id);
void ShowSessionMenu(
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	not_null<QWidget*> button,
	const QString &id,
	base::unique_qptr<PopupMenu> &menu);

} // namespace Agent::Ui