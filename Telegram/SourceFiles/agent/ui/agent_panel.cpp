/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_panel.h"

#include "agent/agent_manager.h"
#include "agent/ui/agent_panel_widget.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"

#include "styles/style_agent_panel.h"

namespace Agent::Ui {

std::shared_ptr<Info::ContentMemento> MakePanelMemento(
		not_null<PeerData*> peer) {
	return std::make_shared<PanelMemento>(peer);
}

bool PanelAvailable(not_null<PeerData*> peer) {
	return peer->session().agent().enabled();
}

rpl::producer<> PanelAvailabilityChanges(
		not_null<Main::Session*> session) {
	return session->agent().enabledValue() | rpl::to_empty;
}

void TogglePanel(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	auto memento = std::make_shared<Info::Memento>(
		peer,
		Info::Section(Info::Section::Type::Agent));
	const auto params = Window::SectionShow();
	if (controller->isLayerShown()
		&& controller->widget()->showSectionInExistingLayer(
			memento.get(),
			params)) {
		controller->hideSpecialLayer();
		return;
	}
	if (controller->content()->thirdSectionIsAgentPanel(peer)) {
		controller->showSection(
			std::make_shared<Info::Memento>(peer),
			params.withThirdColumn());
		return;
	}
	const auto enabled = EnableThirdColumn(controller);
	controller->showSection(
		std::move(memento),
		(enabled
			? Window::SectionShow(anim::type::instant)
			: params).withThirdColumn());
}

bool EnableThirdColumn(not_null<Window::SessionController*> controller) {
	if (controller->adaptive().isThreeColumn()
		|| !controller->canShowThirdSectionWithoutResize()) {
		return false;
	}
	Core::App().settings().setThirdSectionInfoEnabled(true);
	Core::App().saveSettingsDelayed();
	return true;
}

void SetupPanelTopBar(
		not_null<Info::ContentWidget*> content,
		not_null<Info::TopBar*> topBar,
		Info::Wrap wrap) {
	if (const auto panel = dynamic_cast<PanelWidget*>(content.get())) {
		panel->setupTopBar(topBar, wrap);
	}
}

} // namespace Agent::Ui
