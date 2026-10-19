/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>

class PeerData;

namespace Info {
class ContentMemento;
class ContentWidget;
class TopBar;
enum class Wrap;
} // namespace Info

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

[[nodiscard]] std::shared_ptr<Info::ContentMemento> MakePanelMemento(
	not_null<PeerData*> peer);
[[nodiscard]] bool PanelAvailable(not_null<PeerData*> peer);
void TogglePanel(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void SetupPanelTopBar(
	not_null<Info::ContentWidget*> content,
	not_null<Info::TopBar*> topBar,
	Info::Wrap wrap);

} // namespace Agent::Ui
