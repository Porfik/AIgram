/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_menus.h"

#include "agent/agent_chat.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "agent/ui/agent_chat_items.h"
#include "agent/ui/agent_section.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "boxes/share_box.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_list_widget.h"
#include "settings/sections/settings_agent.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"

namespace Agent::Ui {

void FillPeerMenu(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const ::Ui::Menu::MenuCallback &addAction) {
	const auto &agent = controller->session().agent();
	if (!agent.available()) {
		return;
	}
	const auto listed = agent.exclusions().peerListed(peer->id.value);
	addAction(
		listed
			? tr::lng_agent_show_chat(tr::now)
			: tr::lng_agent_hide_chat(tr::now),
		[=] {
			const auto &agent = controller->session().agent();
			const auto wasListed
				= agent.exclusions().peerListed(peer->id.value);
			agent.exclusions().setPeerExcluded(peer->id.value, !wasListed);
			if (wasListed) {
				controller->showToast(tr::lng_agent_chat_shown(tr::now));
			} else {
				controller->showToast(tr::lng_agent_chat_hidden(tr::now));
			}
		},
		listed ? &st::menuIconRestore : &st::menuIconRemove);
}

bool FillAgentChatMenu(
		not_null<Window::SessionController*> controller,
		const Dialogs::EntryState &request,
		const ::Ui::Menu::MenuCallback &addAction) {
	if (!IsAgentHistory(request.key.history())) {
		return false;
	}
	const auto chat = &controller->session().agent().chat();
	addAction(tr::lng_agent_new_session(tr::now), [=] {
		ShowGeneralAgentNewSession(controller);
	}, &st::menuIconEdit);
	const auto pinned = chat->pinned();
	addAction(
		(pinned
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now)),
		[=] { chat->setPinned(!pinned); },
		pinned ? &st::menuIconUnpin : &st::menuIconPin);
	addAction(tr::lng_context_view_profile(tr::now), [=] {
		controller->showPeerInfo(chat->user());
	}, &st::menuIconProfile);
	addAction(tr::lng_agent_profile_settings(tr::now), [=] {
		controller->showSettings(Settings::AgentId());
	}, &st::menuIconSettings);
	return true;
}

void FillFolderMenu(
		not_null<Window::SessionController*> controller,
		int folder,
		const ::Ui::Menu::MenuCallback &addAction) {
	if (!folder) {
		return;
	}
	const auto &agent = controller->session().agent();
	if (!agent.available()) {
		return;
	}
	const auto listed = agent.exclusions().folderExcluded(folder);
	addAction(
		listed
			? tr::lng_agent_show_folder(tr::now)
			: tr::lng_agent_hide_folder(tr::now),
		[=] {
			const auto &agent = controller->session().agent();
			const auto wasListed = agent.exclusions().folderExcluded(folder);
			agent.exclusions().setFolderExcluded(folder, !wasListed);
			if (wasListed) {
				controller->showToast(tr::lng_agent_folder_shown(tr::now));
			} else {
				controller->showToast(tr::lng_agent_folder_hidden(tr::now));
			}
		},
		listed ? &st::menuIconRestore : &st::menuIconRemove);
}

bool AddAgentReplyAction(
		not_null<::Ui::PopupMenu*> menu,
		const HistoryView::ContextMenuRequest &request,
		not_null<HistoryView::ListWidget*> list) {
	const auto item = request.quote.item
		? request.quote.item
		: request.item;
	if (!item || !IsAgentHistory(item->history())) {
		return false;
	}
	const auto info = ChatItems::Info(item);
	if (!info || info->readOnly || !request.selectedItems.empty()) {
		return true;
	}
	const auto &quote = request.quote.highlight;
	menu->addAction(
		(quote.quote.empty()
			? tr::lng_context_reply_msg
			: tr::lng_context_quote_and_reply)(tr::now),
		[=, id = item->fullId()] {
			list->replyToMessageRequestNotify({
				.messageId = id,
				.quote = quote.quote,
				.quoteOffset = quote.quoteOffset,
			}, false);
		},
		&st::menuIconReply);
	return true;
}

bool AddAgentForwardAction(
		not_null<::Ui::PopupMenu*> menu,
		const HistoryView::ContextMenuRequest &request,
		not_null<HistoryView::ListWidget*> list) {
	const auto item = request.item;
	if (!item || !IsAgentHistory(item->history())) {
		return false;
	}
	const auto info = ChatItems::Info(item);
	if (!info
		|| info->copyText.text.isEmpty()
		|| !request.selectedItems.empty()) {
		return true;
	}
	const auto controller = list->controller();
	const auto text = info->copyText;
	menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
		ShowForwardCopyBox(controller, text);
	}, &st::menuIconForward);
	return true;
}

void ShowForwardCopyBox(
		not_null<Window::SessionController*> controller,
		TextWithEntities text) {
	const auto session = &controller->session();
	const auto show = controller->uiShow();
	const auto copy = TextWithTags{
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
	auto submit = [=](
			std::vector<not_null<Data::Thread*>> &&result,
			Fn<bool()> checkPaid,
			TextWithTags &&comment,
			Api::SendOptions options,
			Data::ForwardOptions) {
		if (!checkPaid()) {
			return;
		}
		auto &api = session->api();
		for (const auto &thread : result) {
			const auto action = Api::SendAction(thread, options);
			if (!comment.text.isEmpty()) {
				auto message = Api::MessageToSend(action);
				message.textWithTags = comment;
				message.action.clearDraft = false;
				api.sendMessage(std::move(message));
			}
			auto message = Api::MessageToSend(action);
			message.textWithTags = copy;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		show->showToast(tr::lng_share_done(tr::now));
		show->hideLayer();
	};
	auto filter = [](not_null<Data::Thread*> thread) {
		return !IsAgentPeer(thread->peer())
			&& Data::CanSendTexts(thread);
	};
	controller->show(Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.submitCallback = std::move(submit),
		.filterCallback = std::move(filter),
		.moneyRestrictionError = ShareMessageMoneyRestrictionError(),
	}));
}

} // namespace Agent::Ui
