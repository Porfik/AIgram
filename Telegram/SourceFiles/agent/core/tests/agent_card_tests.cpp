/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_card.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] Card SampleCard() {
	auto card = Card();
	card.toolUseId = u"call1"_q;
	card.kind = CardKind::Send;
	card.targets = {
		CardTarget{ 111, 0, u"Alpha"_q, u"preview"_q, true, QString() },
		CardTarget{ 222, 456, u"Beta"_q, QString(), true, QString() },
	};
	card.text = u"Hello!"_q;
	card.proposedText = u"Hello!"_q;
	card.detail = u"detail"_q;
	card.optionAvailable = true;
	card.canAllowForTurn = true;
	return card;
}

} // namespace

std::vector<Check> CardChecks() {
	return {
		{ u"card_kinds_flags"_q, [] {
			for (const auto kind : {
				CardKind::Send, CardKind::Draft, CardKind::Edit,
			}) {
				if (!CardTextEditable(kind)) {
					return u"expected editable: "_q
						+ QString::number(int(kind));
				}
			}
			for (const auto kind : {
				CardKind::Forward, CardKind::Delete, CardKind::React,
				CardKind::PinMessage, CardKind::UnpinMessage,
				CardKind::DeleteFolder, CardKind::Handoff,
			}) {
				if (CardTextEditable(kind)) {
					return u"expected not editable: "_q
						+ QString::number(int(kind));
				}
				if (CardHasAlternate(kind)) {
					return u"expected no alternate: "_q
						+ QString::number(int(kind));
				}
			}
			if (!CardHasAlternate(CardKind::Send)
				|| !CardHasAlternate(CardKind::Draft)) {
				return u"expected alternates for send and draft"_q;
			}
			if (CardFinal(CardStatus::Pending)
				|| CardFinal(CardStatus::Running)) {
				return u"pending and running are not final"_q;
			}
			for (const auto status : {
				CardStatus::Done, CardStatus::Drafted,
				CardStatus::Declined, CardStatus::Failed,
				CardStatus::NotDone,
			}) {
				if (!CardFinal(status)) {
					return u"expected a final status"_q;
				}
			}
			return QString();
		} },
		{ u"card_decision_decline"_q, [] {
			const auto card = SampleCard();
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Decline,
				{},
				QString(),
				false,
				false });
			if (decided.status != CardStatus::Declined
				|| decided.decided != CardAction::Decline) {
				return u"decline state wrong"_q;
			}
			if (!decided.targets[0].checked || !decided.targets[1].checked) {
				return u"decline must keep the targets"_q;
			}
			if (decided.text != card.text
				|| decided.detail != card.detail
				|| decided.option != card.option
				|| decided.allowedForTurn != card.allowedForTurn) {
				return u"decline must keep everything else"_q;
			}
			return QString();
		} },
		{ u"card_decision_execute"_q, [] {
			const auto card = SampleCard();
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Execute,
				{ false, true },
				u"Edited text"_q,
				true,
				true });
			if (decided.status != CardStatus::Running
				|| decided.decided != CardAction::Execute) {
				return u"execute state wrong"_q;
			}
			if (decided.targets[0].checked || !decided.targets[1].checked) {
				return u"checked not copied"_q;
			}
			if (decided.text != u"Edited text"_q) {
				return u"text not copied"_q;
			}
			if (!decided.option || !decided.allowedForTurn) {
				return u"option and allowance not copied"_q;
			}
			return QString();
		} },
		{ u"card_decision_execute_defaults"_q, [] {
			const auto card = SampleCard();
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Execute,
				{ true, true },
				u"other"_q,
				false,
				false });
			if (decided.option || decided.allowedForTurn) {
				return u"option and allowance must default to false"_q;
			}
			return QString();
		} },
		{ u"card_decision_alternate_send"_q, [] {
			const auto card = SampleCard();
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Alternate,
				{ true, true },
				QString(),
				false,
				false });
			if (decided.status != CardStatus::Running
				|| decided.decided != CardAction::Alternate) {
				return u"alternate state wrong"_q;
			}
			return QString();
		} },
		{ u"card_decision_alternate_draft"_q, [] {
			auto card = SampleCard();
			card.kind = CardKind::Draft;
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Alternate,
				{ true, true },
				QString(),
				false,
				false });
			if (decided.status != CardStatus::Running
				|| decided.decided != CardAction::Alternate) {
				return u"draft alternate must run"_q;
			}
			return QString();
		} },
		{ u"card_decision_keeps_text_when_not_editable"_q, [] {
			auto card = SampleCard();
			card.kind = CardKind::Forward;
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Execute,
				{ true, true },
				u"ignored"_q,
				true,
				true });
			if (decided.text != card.text) {
				return u"text must not change for a forward"_q;
			}
			if (!decided.option || !decided.allowedForTurn) {
				return u"option and allowance must still copy"_q;
			}
			return QString();
		} },
		{ u"card_decision_option_hidden"_q, [] {
			auto card = SampleCard();
			card.optionAvailable = false;
			card.option = true;
			const auto decided = ApplyDecision(card, CardDecision{
				CardAction::Execute,
				{ true, true },
				QString(),
				true,
				false });
			if (decided.option) {
				return u"a hidden option must stay false"_q;
			}
			return QString();
		} },
		{ u"card_checked_peers_and_count"_q, [] {
			auto card = SampleCard();
			card.targets = {
				CardTarget{ 111, 0, u"A"_q, QString(), true, QString() },
				CardTarget{ 222, 0, u"B"_q, QString(), false, QString() },
				CardTarget{ 111, 5, u"A"_q, QString(), true, QString() },
				CardTarget{ 333, 0, u"C"_q, QString(), true, QString() },
			};
			const auto peers = CheckedPeers(card);
			const auto expected = std::vector<PeerKey>{ 111, 333 };
			if (peers != expected) {
				return u"unexpected checked peers"_q;
			}
			if (CheckedCount(card) != 3) {
				return u"unexpected checked count"_q;
			}
			return QString();
		} },
		{ u"card_result_text_statuses"_q, [] {
			auto card = SampleCard();
			card.status = CardStatus::Done;
			if (CardResultText(card) != u"The message was sent."_q) {
				return u"done send: "_q + CardResultText(card);
			}
			card.kind = CardKind::Draft;
			card.decided = CardAction::Alternate;
			if (CardResultText(card)
				!= u"The text was appended to the draft."_q) {
				return u"appended: "_q + CardResultText(card);
			}
			card.decided = CardAction::Execute;
			if (CardResultText(card) != u"The draft was replaced."_q) {
				return u"replaced: "_q + CardResultText(card);
			}
			card.kind = CardKind::Send;
			card.status = CardStatus::Drafted;
			if (CardResultText(card)
				!= u"The message was saved to the draft."_q) {
				return u"drafted: "_q + CardResultText(card);
			}
			card.status = CardStatus::Declined;
			if (CardResultText(card) != u"The user declined the action."_q) {
				return u"declined: "_q + CardResultText(card);
			}
			card.status = CardStatus::NotDone;
			if (!CardResultText(card).contains(u"was not performed"_q)) {
				return u"not done: "_q + CardResultText(card);
			}
			return QString();
		} },
		{ u"card_result_text_unchecked_failed_edited"_q, [] {
			auto card = SampleCard();
			card.targets = {
				CardTarget{ 111, 0, u"Alpha"_q, QString(), false, QString() },
				CardTarget{
					222,
					456,
					QString(),
					QString(),
					true,
					u"Flood wait"_q },
			};
			card.status = CardStatus::Failed;
			const auto text = CardResultText(card);
			if (!text.contains(u"The action failed."_q)) {
				return u"failed headline missing: "_q + text;
			}
			if (!text.contains(
					u"Skipped, the user unchecked: Alpha (chat 111)"_q)) {
				return u"unchecked missing: "_q + text;
			}
			if (!text.contains(
					u"Failed: chat 222, message 456: Flood wait."_q)) {
				return u"failed target missing: "_q + text;
			}
			card.status = CardStatus::Done;
			card.text = u"Changed"_q;
			card.proposedText = u"Original"_q;
			const auto edited = CardResultText(card);
			if (!edited.contains(
					u"The user edited the text before it was used. "
					u"The final text:\nChanged"_q)) {
				return u"edited text missing: "_q + edited;
			}
			if (edited.contains(u"The action failed."_q)) {
				return u"failed headline on a done card"_q;
			}
			return QString();
		} },
		{ u"card_new_kinds_flags"_q, [] {
			if (!CardTextEditable(CardKind::SendFiles)) {
				return u"SendFiles must be editable"_q;
			} else if (CardTextEditable(CardKind::Transcribe)
				|| CardTextEditable(CardKind::Download)) {
				return u"Transcribe and Download must not be editable"_q;
			}
			for (const auto kind : {
				CardKind::Draft, CardKind::Transcribe,
				CardKind::Download,
			}) {
				if (!CardAlwaysAsks(kind)) {
					return u"expected always asks: "_q
						+ QString::number(int(kind));
				}
			}
			if (CardAlwaysAsks(CardKind::Send)
				|| CardAlwaysAsks(CardKind::SendFiles)) {
				return u"unexpected always asks"_q;
			}
			return QString();
		} },
		{ u"card_decision_new_kinds"_q, [] {
			auto transcribe = SampleCard();
			transcribe.kind = CardKind::Transcribe;
			transcribe.targets = {
				CardTarget{ 111, 12, u"Voice"_q, QString(), true, QString() },
			};
			const auto decidedTranscribe = ApplyDecision(
				transcribe,
				CardDecision{ CardAction::Execute, { true }, QString(), false, false });
			if (decidedTranscribe.status != CardStatus::Running
				|| decidedTranscribe.decided != CardAction::Execute) {
				return u"transcribe execute state wrong"_q;
			}
			auto sendFiles = SampleCard();
			sendFiles.kind = CardKind::SendFiles;
			sendFiles.text = u"caption"_q;
			sendFiles.proposedText = u"caption"_q;
			sendFiles.optionAvailable = false;
			const auto decidedSendFiles = ApplyDecision(
				sendFiles,
				CardDecision{
					CardAction::Execute,
					{ true, true },
					u"new"_q,
					true,
					false,
				});
			if (decidedSendFiles.text != u"new"_q
				|| decidedSendFiles.option) {
				return u"sendfiles text/option wrong"_q;
			}
			return QString();
		} },
		{ u"card_result_text_new_kinds"_q, [] {
			auto card = SampleCard();
			card.kind = CardKind::Transcribe;
			card.status = CardStatus::Declined;
			if (CardResultText(card)
				!= u"The user declined to spend a trial transcription."_q) {
				return u"transcribe declined: "_q + CardResultText(card);
			}
			card.kind = CardKind::Download;
			card.status = CardStatus::Declined;
			if (CardResultText(card)
				!= u"The user declined the download."_q) {
				return u"download declined: "_q + CardResultText(card);
			}
			card.status = CardStatus::Done;
			card.detail = QString();
			if (CardResultText(card) != u"The files were downloaded."_q) {
				return u"download read: "_q + CardResultText(card);
			}
			card.detail = u"/agent/folder"_q;
			card.files = {
				CardFile{ u"ref1"_q, u"a.txt"_q, u"text/plain"_q, 100 },
				CardFile{ u"ref2"_q, u"b.jpg"_q, u"image/jpeg"_q, 200 },
			};
			if (CardResultText(card)
				!= u"The files were saved to /agent/folder.\n"
				u"Saved files: a.txt, b.jpg."_q) {
				return u"download saved: "_q + CardResultText(card);
			}
			card.kind = CardKind::SendFiles;
			card.files.clear();
			if (CardResultText(card) != u"The files were sent."_q) {
				return u"sendfiles done: "_q + CardResultText(card);
			}
			card.kind = CardKind::Transcribe;
			if (CardResultText(card) != u"The message was transcribed."_q) {
				return u"transcribe done: "_q + CardResultText(card);
			}
			return QString();
		} },
		{ u"card_result_text_handoff"_q, [] {
			auto card = SampleCard();
			card.kind = CardKind::Handoff;
			card.status = CardStatus::Done;
			const auto text = CardResultText(card);
			if (!text.contains(
					u"button to continue this conversation "
					u"in the general agent"_q)) {
				return u"handoff line missing: "_q + text;
			}
			return QString();
		} },
		{ u"card_task_kinds"_q, [] {
			for (const auto kind : {
				CardKind::CreateTask, CardKind::UpdateTask,
			}) {
				if (!CardTextEditable(kind) || !CardAlwaysAsks(kind)) {
					return u"task cards must be editable and always ask"_q;
				} else if (CardHasAlternate(kind)) {
					return u"task cards have no alternate"_q;
				}
			}
			if (!CardFinal(CardStatus::Expired)) {
				return u"expired must be final"_q;
			}
			auto card = Card();
			card.kind = CardKind::CreateTask;
			card.text = card.proposedText = u"Reply politely."_q;
			card = ApplyDecision(card, CardDecision{
				.action = CardAction::Execute,
				.text = u"Reply briefly."_q,
			});
			if (card.text != u"Reply briefly."_q) {
				return u"instruction not taken from the decision"_q;
			}
			card.status = CardStatus::Done;
			const auto created = CardResultText(card);
			if (!created.contains(u"it is active now"_q)
				|| !created.contains(u"Reply briefly."_q)) {
				return u"create result: "_q + created;
			}
			card.kind = CardKind::UpdateTask;
			card.text = card.proposedText;
			const auto updated = CardResultText(card);
			if (!updated.contains(u"approved the changes"_q)
				|| updated.contains(u"edited"_q)) {
				return u"update result: "_q + updated;
			}
			return QString();
		} },
		{ u"card_result_text_expired"_q, [] {
			auto card = SampleCard();
			card.status = CardStatus::Expired;
			const auto text = CardResultText(card);
			if (!text.contains(u"expired and nothing was done"_q)
				|| text.contains(u"Skipped"_q)) {
				return u"expired: "_q + text;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
