/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_chat_participant_status.h"
#include "data/data_drafts.h"
#include "data/data_histories.h"
#include "data/data_message_reaction_id.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>

namespace Agent::ActionTools {
namespace {

constexpr auto kPreviewLimit = 100;
constexpr auto kMaxMessages = 100;
constexpr auto kMaxEmojiLength = 16;

[[nodiscard]] QString ChatTitle(not_null<PeerData*> peer) {
	return peer->isSelf() ? tr::lng_saved_messages(tr::now) : peer->name();
}

[[nodiscard]] QString Preview(not_null<HistoryItem*> item) {
	auto text = item->notificationText().text;
	text.replace('\n', ' ');
	return text.left(kPreviewLimit);
}

[[nodiscard]] std::optional<MsgId> ParseMessageId(
		const QJsonObject &input,
		const QString &key,
		QString &error) {
	const auto value = input.value(key);
	if (value.isUndefined() || value.isNull()) {
		return std::nullopt;
	} else if (!value.isDouble() || value.toDouble() < 1) {
		error = u"%1 must be a message id."_q.arg(key);
		return std::nullopt;
	}
	return MsgId(qint64(value.toDouble()));
}

[[nodiscard]] std::vector<MsgId> ParseMessageIds(
		const QJsonObject &input,
		const QString &key,
		QString &error) {
	const auto value = input.value(key);
	if (!value.isArray()) {
		error = u"%1 must be an array of message ids."_q.arg(key);
		return {};
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > kMaxMessages) {
		error = u"%1 must contain 1 to %2 message ids."_q
			.arg(key)
			.arg(kMaxMessages);
		return {};
	}
	auto result = std::vector<MsgId>();
	for (const auto &item : list) {
		if (!item.isDouble() || item.toDouble() < 1) {
			error = u"%1 must contain message ids."_q.arg(key);
			return {};
		}
		const auto id = MsgId(qint64(item.toDouble()));
		if (!ranges::contains(result, id)) {
			result.push_back(id);
		}
	}
	return result;
}

void WithMessages(
		not_null<PendingCall*> call,
		not_null<PeerData*> peer,
		std::vector<MsgId> ids,
		Fn<void(HistoryItemsList)> ready) {
	const auto session = call->session;
	const auto finish = [=] {
		auto items = HistoryItemsList();
		for (const auto id : ids) {
			const auto item = session->data().message(peer->id, id);
			if (!item) {
				call->fail(u"Message %1 was not found in %2. Read the"
					" history to get valid message ids."_q
					.arg(id.bare)
					.arg(peer->name()));
				return;
			}
			items.push_back(item);
		}
		ready(std::move(items));
	};
	auto missing = std::vector<MsgId>();
	for (const auto id : ids) {
		if (!session->data().message(peer->id, id)) {
			missing.push_back(id);
		}
	}
	if (missing.empty()) {
		finish();
		return;
	}
	const auto left = std::make_shared<int>(int(missing.size()));
	for (const auto id : missing) {
		session->api().requestMessageData(peer, id, crl::guard(call.get(), [=] {
			if (!--*left) {
				finish();
			}
		}));
	}
}

[[nodiscard]] CardTarget MessageTarget(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	return CardTarget{
		.peer = peer->id.value,
		.messageId = item->id.bare,
		.title = ChatTitle(peer),
		.preview = Preview(item),
	};
}

[[nodiscard]] HistoryItem *FindTarget(
		not_null<Main::Session*> session,
		const CardTarget &target) {
	return session->data().message(
		PeerId(target.peer),
		MsgId(target.messageId));
}

[[nodiscard]] CardStatus FinalStatus(const Card &card) {
	for (const auto &target : card.targets) {
		if (target.checked && target.error.isEmpty()) {
			return CardStatus::Done;
		}
	}
	return card.targets.empty() ? CardStatus::Done : CardStatus::Failed;
}

[[nodiscard]] QString CurrentDraftText(not_null<History*> history) {
	const auto draft = history->localDraft(MsgId(), PeerId());
	return draft ? draft->textWithTags.text : QString();
}

void ApplyDraft(
		not_null<History*> history,
		const QString &text,
		bool append) {
	const auto thread = history->threadFor(MsgId(), PeerId());
	auto current = CurrentDraftText(history);
	if (append && !current.isEmpty()) {
		current += '\n' + text;
	} else {
		current = text;
	}
	history->setLocalDraft(std::make_unique<Data::Draft>(
		TextWithTags{ current, TextWithTags::Tags() },
		FullReplyTo(),
		SuggestOptions(),
		MessageCursor{
			int(current.size()),
			int(current.size()),
			Ui::kQFixedMax,
		},
		Data::WebPageDraft()));
	history->clearLocalEditDraft(MsgId(), PeerId());
	const auto controller = history->session().tryResolveWindow();
	if (controller
		&& controller->activeChatCurrent().peer() == history->peer) {
		history->session().changes().entryUpdated(
			thread,
			Data::EntryUpdate::Flag::LocalDraftSet);
	} else {
		history->session().api().saveDraftToCloudDelayed(thread);
	}
}

void RunSendMessage(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto text = call->input.value("text").toString();
	auto error = QString();
	const auto replyTo = ParseMessageId(call->input, u"reply_to"_q, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (text.trimmed().isEmpty()) {
		call->fail(u"text must not be empty."_q);
		return;
	} else if (replyTo && call->peers.size() != 1) {
		call->fail(u"reply_to works only with exactly one chat."_q);
		return;
	}
	auto card = Card{
		.kind = CardKind::Send,
		.text = text,
		.proposedText = text,
	};
	for (const auto &peer : call->peers) {
		card.targets.push_back(CardTarget{
			.peer = peer->id.value,
			.title = ChatTitle(peer),
		});
	}
	const auto send = [=](Card card) {
		for (auto &target : card.targets) {
			if (!target.checked) {
				continue;
			}
			const auto peer = session->data().peerLoaded(PeerId(target.peer));
			if (!peer) {
				target.error = u"The chat is not loaded."_q;
				continue;
			}
			const auto history = session->data().history(peer);
			const auto thread = history->threadFor(MsgId(), PeerId());
			if (card.decided == CardAction::Alternate) {
				ApplyDraft(history, card.text, false);
				continue;
			} else if (!Data::CanSendTexts(thread)) {
				target.error = u"You can't write in this chat."_q;
				continue;
			}
			auto action = Api::SendAction(thread);
			if (replyTo) {
				action.replyTo.messageId = FullMsgId(peer->id, *replyTo);
			}
			auto message = Api::MessageToSend(std::move(action));
			message.textWithTags = TextWithTags{
				card.text,
				TextWithTags::Tags(),
			};
			session->api().sendMessage(std::move(message));
		}
		card.status = FinalStatus(card);
		if (card.decided == CardAction::Alternate
			&& card.status == CardStatus::Done) {
			card.status = CardStatus::Drafted;
		}
		call->finishCard(std::move(card));
	};
	if (!replyTo) {
		call->confirm(std::move(card), send);
		return;
	}
	const auto peer = call->peers.front();
	WithMessages(call, peer, { *replyTo }, [=](HistoryItemsList items) {
		auto copy = card;
		copy.detail = Preview(items.front());
		call->confirm(std::move(copy), send);
	});
}

void RunSetDraft(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	const auto text = call->input.value("text").toString();
	if (!peer) {
		call->fail(u"This tool works only inside a chat."_q);
		return;
	} else if (text.trimmed().isEmpty()) {
		call->fail(u"text must not be empty."_q);
		return;
	}
	const auto history = call->session->data().history(peer);
	const auto current = CurrentDraftText(history);
	if (current.trimmed().isEmpty()) {
		ApplyDraft(history, text, false);
		auto chat = QJsonObject();
		chat.insert("chat", QJsonValue(qint64(peer->id.value)));
		chat.insert("title", ChatTitle(peer));
		auto result = QJsonObject();
		result.insert("done", QJsonArray{ chat });
		result.insert("failed", QJsonArray());
		call->done(std::move(result));
		return;
	}
	auto card = Card{
		.kind = CardKind::Draft,
		.targets = { CardTarget{
			.peer = peer->id.value,
			.title = ChatTitle(peer),
		} },
		.text = text,
		.proposedText = text,
		.detail = current,
	};
	call->confirm(std::move(card), [=](Card decided) {
		ApplyDraft(
			history,
			decided.text,
			(decided.decided == CardAction::Alternate));
		decided.status = CardStatus::Done;
		call->finishCard(std::move(decided));
	});
}

void RunForwardMessages(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto from = ResolveChatRef(session, call->input.value("from_chat"));
	auto error = QString();
	const auto ids = ParseMessageIds(call->input, u"message_ids"_q, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (!from) {
		call->fail(u"Unknown from_chat. Use a chat id from an earlier tool"
			" result or an @username."_q);
		return;
	} else if (!call->gate->visible(from->id.value)) {
		call->fail(u"The user hid this chat from the agent. Tell the user"
			" you can't see it and don't try to reach it another way."_q);
		return;
	}
	WithMessages(call, from, ids, [=](HistoryItemsList items) {
		auto previews = QStringList();
		for (const auto &item : items) {
			if (!item->allowsForward()) {
				call->fail(u"Message %1 can't be forwarded: the chat"
					" forbids forwarding."_q.arg(item->id.bare));
				return;
			}
			previews.push_back(Preview(item));
		}
		auto sources = std::vector<FullMsgId>();
		for (const auto &item : items) {
			sources.push_back(item->fullId());
		}
		auto card = Card{
			.kind = CardKind::Forward,
			.text = previews.join('\n'),
			.detail = ChatTitle(from),
		};
		card.proposedText = card.text;
		for (const auto &peer : call->peers) {
			card.targets.push_back(CardTarget{
				.peer = peer->id.value,
				.title = ChatTitle(peer),
			});
		}
		call->confirm(std::move(card), [=](Card decided) {
			auto list = HistoryItemsList();
			for (const auto &id : sources) {
				if (const auto item = session->data().message(id)) {
					list.push_back(item);
				}
			}
			for (auto &target : decided.targets) {
				if (!target.checked) {
					continue;
				}
				const auto peer = session->data().peerLoaded(
					PeerId(target.peer));
				const auto thread = peer
					? session->data().history(peer)->threadFor(
						MsgId(),
						PeerId())
					: nullptr;
				if (list.empty()) {
					target.error = u"The messages were deleted."_q;
				} else if (!thread || !Data::CanSendAnything(thread)) {
					target.error = u"You can't write in this chat."_q;
				} else {
					session->api().forwardMessages(
						Data::ResolvedForwardDraft{ .items = list },
						Api::SendAction(thread));
				}
			}
			decided.status = FinalStatus(decided);
			call->finishCard(std::move(decided));
		});
	});
}

void RunEditMessage(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto text = call->input.value("text").toString();
	auto error = QString();
	const auto id = ParseMessageId(call->input, u"message_id"_q, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (!id) {
		call->fail(u"message_id is required."_q);
		return;
	} else if (!peer) {
		call->fail(u"chat is required."_q);
		return;
	} else if (text.trimmed().isEmpty()) {
		call->fail(u"text must not be empty."_q);
		return;
	}
	WithMessages(call, peer, { *id }, [=](HistoryItemsList items) {
		const auto item = items.front();
		if (!item->out()) {
			call->fail(u"Only your own messages can be edited."_q);
			return;
		} else if (!item->allowsEdit(base::unixtime::now())) {
			call->fail(u"This message can't be edited any more."_q);
			return;
		}
		auto card = Card{
			.kind = CardKind::Edit,
			.targets = { MessageTarget(item) },
			.text = text,
			.proposedText = text,
			.detail = item->originalText().text,
		};
		call->confirm(std::move(card), [=](Card decided) {
			const auto item = FindTarget(session, decided.targets.front());
			if (!item) {
				decided.targets.front().error = u"The message was deleted."_q;
				decided.status = CardStatus::Failed;
				call->finishCard(std::move(decided));
				return;
			}
			const auto done = crl::guard(call.get(), [=](mtpRequestId) {
				auto result = decided;
				result.status = CardStatus::Done;
				call->finishCard(std::move(result));
			});
			const auto fail = crl::guard(call.get(), [=](
					const QString &error,
					mtpRequestId) {
				auto result = decided;
				result.targets.front().error = error;
				result.status = CardStatus::Failed;
				call->finishCard(std::move(result));
			});
			Api::EditTextMessage(
				item,
				TextWithEntities{ decided.text },
				Data::WebPageDraft(),
				Api::SendOptions(),
				done,
				fail,
				false);
		});
	});
}

void RunDeleteMessages(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	auto error = QString();
	const auto ids = ParseMessageIds(call->input, u"message_ids"_q, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (!peer) {
		call->fail(u"chat is required."_q);
		return;
	}
	WithMessages(call, peer, ids, [=](HistoryItemsList items) {
		const auto now = base::unixtime::now();
		auto revoke = true;
		auto card = Card{ .kind = CardKind::Delete };
		for (const auto &item : items) {
			if (!item->out() || !item->canDelete()) {
				call->fail(u"Message %1 is not yours or can't be deleted;"
					" only your own messages can be deleted."_q
					.arg(item->id.bare));
				return;
			}
			revoke = revoke && item->canDeleteForEveryone(now);
			card.targets.push_back(MessageTarget(item));
		}
		card.optionAvailable = revoke;
		card.option = revoke;
		call->confirm(std::move(card), [=](Card decided) {
			auto list = MessageIdsList();
			for (auto &target : decided.targets) {
				if (!target.checked) {
					continue;
				} else if (const auto item = FindTarget(session, target)) {
					list.push_back(item->fullId());
				} else {
					target.error = u"The message was already deleted."_q;
				}
			}
			if (!list.empty()) {
				session->data().histories().deleteMessages(
					list,
					decided.option);
				session->data().sendHistoryChangeNotifications();
			}
			decided.status = FinalStatus(decided);
			call->finishCard(std::move(decided));
		});
	});
}

void RunReact(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto emoji = call->input.value("emoji").toString().trimmed();
	auto error = QString();
	const auto ids = ParseMessageIds(call->input, u"message_ids"_q, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (!peer) {
		call->fail(u"chat is required."_q);
		return;
	} else if (emoji.isEmpty() || emoji.size() > kMaxEmojiLength) {
		call->fail(u"emoji must be one emoji, like \"👍\"."_q);
		return;
	}
	WithMessages(call, peer, ids, [=](HistoryItemsList items) {
		auto card = Card{ .kind = CardKind::React, .text = emoji };
		for (const auto &item : items) {
			if (!item->canReact()) {
				call->fail(u"Message %1 doesn't accept reactions."_q
					.arg(item->id.bare));
				return;
			}
			card.targets.push_back(MessageTarget(item));
		}
		call->confirm(std::move(card), [=](Card decided) {
			const auto reaction = Data::ReactionId{ emoji };
			for (auto &target : decided.targets) {
				if (!target.checked) {
					continue;
				}
				const auto item = FindTarget(session, target);
				if (!item) {
					target.error = u"The message was deleted."_q;
				} else if (!ranges::contains(
						item->chosenReactions(),
						reaction)) {
					item->toggleReaction(
						reaction,
						HistoryReactionSource::Selector);
				}
			}
			decided.status = FinalStatus(decided);
			call->finishCard(std::move(decided));
		});
	});
}

void RunPinMessage(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto unpin = call->input.value("unpin").toBool();
	auto error = QString();
	const auto id = ParseMessageId(call->input, u"message_id"_q, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (!id) {
		call->fail(u"message_id is required."_q);
		return;
	} else if (!peer) {
		call->fail(u"chat is required."_q);
		return;
	} else if (!peer->canPinMessages()) {
		call->fail(u"You can't pin messages in this chat."_q);
		return;
	}
	WithMessages(call, peer, { *id }, [=](HistoryItemsList items) {
		auto card = Card{
			.kind = unpin ? CardKind::UnpinMessage : CardKind::PinMessage,
			.targets = { MessageTarget(items.front()) },
			.optionAvailable = !unpin,
		};
		call->confirm(std::move(card), [=](Card decided) {
			using Flag = MTPmessages_UpdatePinnedMessage::Flag;
			const auto flags = (decided.option ? Flag(0) : Flag::f_silent)
				| (unpin ? Flag::f_unpin : Flag(0));
			session->api().request(MTPmessages_UpdatePinnedMessage(
				MTP_flags(flags),
				peer->input(),
				MTP_int(*id)
			)).done(crl::guard(call.get(), [=](const MTPUpdates &result) {
				session->api().applyUpdates(result);
				auto final = decided;
				final.status = CardStatus::Done;
				call->finishCard(std::move(final));
			})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
				auto final = decided;
				final.targets.front().error = error.type();
				final.status = CardStatus::Failed;
				call->finishCard(std::move(final));
			})).send();
		});
	});
}

[[nodiscard]] ToolPolicy VisiblePolicy(bool general, bool chat, bool outbound) {
	return ToolPolicy{
		.risk = Risk::Visible,
		.general = general,
		.chat = chat,
		.outbound = outbound,
	};
}

std::vector<ToolEntry> BuildEntries() {
	auto result = std::vector<ToolEntry>();
	const auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	auto send = QJsonObject();
	send.insert("text", StringProperty(
		u"The message text. Markdown is not applied."_q));
	send.insert("reply_to", MessageIdProperty(
		u"Reply to this message id; only with exactly one chat."_q));
	push({
		.name = u"send_message"_q,
		.description = u"Send a text message to one or more chats. The"
			" user confirms it on a card, may edit the text, and may"
			" put it into the chats' drafts instead."_q,
		.inputSchema = ToolSchema(
			std::move(send),
			QJsonArray{ QJsonValue(u"text"_q) }),
		.policy = VisiblePolicy(true, false, true),
		.chatParam = ChatParam::Many,
		.run = RunSendMessage,
	});
	auto draft = QJsonObject();
	draft.insert("text", StringProperty(u"The draft text."_q));
	push({
		.name = u"set_draft"_q,
		.description = u"Put a message draft into this chat's message"
			" field; the user sends it. If the field already has text,"
			" the user chooses to replace it, append to it or cancel."_q,
		.inputSchema = ToolSchema(
			std::move(draft),
			QJsonArray{ QJsonValue(u"text"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = false,
			.chat = true,
			.background = false,
		},
		.action = true,
		.run = RunSetDraft,
	});
	auto forward = QJsonObject();
	forward.insert("from_chat", StringProperty(
		u"The chat to forward from: a chat id or an @username."_q));
	forward.insert("message_ids", MessageIdsProperty(
		u"Ids of the messages to forward, 1 to 100."_q));
	push({
		.name = u"forward_messages"_q,
		.description = u"Forward messages from one chat to one or more"
			" chats. The user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(forward),
			QJsonArray{
				QJsonValue(u"from_chat"_q),
				QJsonValue(u"message_ids"_q),
			}),
		.policy = VisiblePolicy(true, false, true),
		.chatParam = ChatParam::Many,
		.run = RunForwardMessages,
	});
	auto edit = QJsonObject();
	edit.insert("message_id", MessageIdProperty(
		u"Id of your own message to edit."_q));
	edit.insert("text", StringProperty(u"The new message text."_q));
	push({
		.name = u"edit_message"_q,
		.description = u"Edit the text of one of your own messages in this"
			" chat. The user confirms it on a card."_q,
		.generalDescription = u"Edit the text of one of your own messages"
			" in the chat. The user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(edit),
			QJsonArray{
				QJsonValue(u"message_id"_q),
				QJsonValue(u"text"_q),
			}),
		.policy = VisiblePolicy(true, true, false),
		.chatParam = ChatParam::One,
		.run = RunEditMessage,
	});
	auto remove = QJsonObject();
	remove.insert("message_ids", MessageIdsProperty(
		u"Ids of your own messages to delete, 1 to 100."_q));
	push({
		.name = u"delete_messages"_q,
		.description = u"Delete your own messages in this chat. The user"
			" confirms it on a card and chooses whether to delete them"
			" for everyone."_q,
		.generalDescription = u"Delete your own messages in the chat. The"
			" user confirms it on a card and chooses whether to delete"
			" them for everyone."_q,
		.inputSchema = ToolSchema(
			std::move(remove),
			QJsonArray{ QJsonValue(u"message_ids"_q) }),
		.policy = VisiblePolicy(true, true, false),
		.chatParam = ChatParam::One,
		.run = RunDeleteMessages,
	});
	auto react = QJsonObject();
	react.insert("message_ids", MessageIdsProperty(
		u"Ids of the messages to react to, 1 to 100."_q));
	react.insert("emoji", StringProperty(
		u"One reaction emoji allowed in the chat, like \"👍\"."_q));
	push({
		.name = u"react"_q,
		.description = u"Set a reaction on messages in this chat. The user"
			" confirms it on a card."_q,
		.generalDescription = u"Set a reaction on messages in the chat. The"
			" user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(react),
			QJsonArray{
				QJsonValue(u"message_ids"_q),
				QJsonValue(u"emoji"_q),
			}),
		.policy = VisiblePolicy(true, true, false),
		.chatParam = ChatParam::One,
		.run = RunReact,
	});
	auto pin = QJsonObject();
	pin.insert("message_id", MessageIdProperty(
		u"Id of the message to pin or unpin."_q));
	pin.insert("unpin", BoolProperty(
		u"True to unpin the message instead."_q));
	push({
		.name = u"pin_message"_q,
		.description = u"Pin or unpin a message in this chat. The user"
			" confirms it on a card and chooses whether to notify"
			" members."_q,
		.generalDescription = u"Pin or unpin a message in the chat. The"
			" user confirms it on a card and chooses whether to notify"
			" members."_q,
		.inputSchema = ToolSchema(
			std::move(pin),
			QJsonArray{ QJsonValue(u"message_id"_q) }),
		.policy = VisiblePolicy(true, true, false),
		.chatParam = ChatParam::One,
		.run = RunPinMessage,
	});
	return result;
}

} // namespace

std::vector<ToolEntry> Entries() {
	static const auto entries = BuildEntries();
	return entries;
}

} // namespace Agent::ActionTools
