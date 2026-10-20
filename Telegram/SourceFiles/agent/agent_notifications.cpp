/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_notifications.h"

#include "agent/ui/agent_section.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Agent {
namespace {

int NotificationCounter = 0;

void OpenRun(not_null<Main::Session*> session, const QString &sessionId) {
	const auto window = session->tryResolveWindow();
	if (!window) {
		return;
	}
	window->widget()->showFromTray();
	if (sessionId.isEmpty()) {
		Ui::ShowGeneralAgent(window);
	} else {
		Ui::ShowGeneralAgentSession(window, sessionId);
	}
}

} // namespace

void ShowTaskNotification(
		not_null<Main::Session*> session,
		TaskNotification notification) {
	const auto &settings = Core::App().settings();
	if (!settings.desktopNotify()) {
		return;
	}
	const auto loaded = notification.peer
		? session->data().peerLoaded(PeerId(notification.peer))
		: nullptr;
	const auto peer = loaded ? not_null(loaded) : session->user();
	const auto weak = base::make_weak(session);
	const auto sessionId = notification.sessionId;
	const auto open = [=] {
		if (const auto strong = weak.get()) {
			OpenRun(strong, sessionId);
		}
	};
	auto &manager = Core::App().notifications().manager();
	using Window::Notifications::ManagerType;
	if (manager.type() != ManagerType::Native) {
		if (const auto window = session->tryResolveWindow()) {
			window->showToast(notification.title
				+ u": "_q
				+ notification.text);
		}
		return;
	}
	using Native = Window::Notifications::NativeManager;
	const auto hidden = Core::App().passcodeLocked()
		|| Core::App().screenIsLocked();
	static_cast<Native&>(manager).showCustomNotification({
		.peer = peer,
		.itemId = MsgId(-(++NotificationCounter)),
		.title = notification.title,
		.message = notification.text,
		.options = {
			.hideNameAndPhoto = hidden,
			.hideMessageText = hidden,
			.hideMarkAsRead = true,
			.hideReplyButton = true,
		},
	}, notification.sound, open);
}

} // namespace Agent
