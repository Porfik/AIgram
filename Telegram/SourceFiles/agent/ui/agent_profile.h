/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

#include <QtCore/QPointer>

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
class TopBar;
enum class Wrap;
} // namespace Info

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

enum class ProfileMode {
	Main,
	Tasks,
	Memory,
};

class ProfileMemento final : public Info::ContentMemento {
public:
	ProfileMemento(not_null<PeerData*> peer, ProfileMode mode);

	object_ptr<Info::ContentWidget> createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) override;

	[[nodiscard]] Info::Section section() const override;
	[[nodiscard]] ProfileMode mode() const;

private:
	ProfileMode _mode = ProfileMode::Main;

};

class ProfileWidget final : public Info::ContentWidget {
public:
	ProfileWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer,
		ProfileMode mode);

	bool showInternal(
		not_null<Info::ContentMemento*> memento) override;
	void setInternalState(
		const QRect &geometry,
		not_null<Info::ContentMemento*> memento);

	rpl::producer<QString> title() override;
	void setupTopBar(not_null<Info::TopBar*> topBar, Info::Wrap wrap);

private:
	void setupMain(not_null<::Ui::VerticalLayout*> container);
	void showMode(ProfileMode mode);

	std::shared_ptr<Info::ContentMemento> doCreateMemento() override;

	const not_null<PeerData*> _peer;
	const ProfileMode _mode;

};

void ShowEditAgentProfileBox(
	not_null<Window::SessionController*> controller);

} // namespace Agent::Ui
