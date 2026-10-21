/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_sessions_list.h"

#include "agent/agent_manager.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/panel_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {

void ShowRenameSessionBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &id) {
	const auto info = manager->info(id);
	if (!info) {
		return;
	}
	const auto current = info->title;
	controller->show(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_agent_session_rename_title());
		const auto input = box->addRow(
			object_ptr<InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_session_title_placeholder(),
				current));
		box->setFocusCallback([=] {
			input->setFocusFast();
			input->selectAll();
		});
		const auto save = [=] {
			const auto text = input->getLastText().trimmed();
			if (text.isEmpty() || text == current) {
				return;
			}
			manager->rename(id, text);
			box->closeBox();
		};
		input->submits() | rpl::on_next(save, box->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowDeleteSessionBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &id) {
	controller->show(MakeConfirmBox({
		.text = tr::lng_agent_session_delete_sure(tr::marked),
		.confirmed = [=](Fn<void()> close) {
			manager->remove(id);
			close();
		},
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void ShowSessionMenu(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		not_null<QWidget*> button,
		const QString &id,
		base::unique_qptr<PopupMenu> &menu) {
	menu = base::make_unique_q<PopupMenu>(button, st::popupMenuWithIcons);
	const auto addAction = Menu::CreateAddActionCallback(menu);
	addAction(tr::lng_agent_session_rename(tr::now), [=] {
		ShowRenameSessionBox(controller, manager, id);
	}, &st::menuIconEdit);
	addAction({
		.text = tr::lng_agent_session_delete_current(tr::now),
		.handler = [=] { ShowDeleteSessionBox(controller, manager, id); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	menu->setForcedOrigin(PanelAnimation::Origin::TopRight);
	menu->popup(button->mapToGlobal(
		QPoint(button->width(), button->height())));
}

} // namespace Agent::Ui