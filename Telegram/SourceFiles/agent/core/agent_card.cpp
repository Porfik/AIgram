/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_card.h"

#include "base/assertion.h"
#include "base/flat_set.h"

#include <QtCore/QStringList>

namespace Agent {
namespace {

[[nodiscard]] QString TargetName(const CardTarget &target) {
	auto result = target.title;
	if (!result.isEmpty()) {
		result += u" ("_q;
	}
	result += u"chat %1"_q.arg(target.peer);
	if (target.messageId) {
		result += u", message %1"_q.arg(target.messageId);
	}
	if (!target.title.isEmpty()) {
		result += ')';
	}
	return result;
}

[[nodiscard]] QString DoneLine(const Card &card) {
	switch (card.kind) {
	case CardKind::Send: return u"The message was sent."_q;
	case CardKind::Draft: return (card.decided == CardAction::Alternate)
		? u"The text was appended to the draft."_q
		: u"The draft was replaced."_q;
	case CardKind::Forward: return u"The messages were forwarded."_q;
	case CardKind::Edit: return u"The message was edited."_q;
	case CardKind::Delete: return u"The messages were deleted."_q;
	case CardKind::React: return u"The reaction was set."_q;
	case CardKind::PinMessage: return u"The message was pinned."_q;
	case CardKind::UnpinMessage: return u"The message was unpinned."_q;
	case CardKind::DeleteFolder: return u"The folder was deleted."_q;
	case CardKind::Handoff:
		return u"The user now sees a button to continue this "
			u"conversation in the general agent."_q;
	}
	Unexpected("Card kind in CardResultText.");
}

} // namespace

bool CardFinal(CardStatus status) {
	return status != CardStatus::Pending && status != CardStatus::Running;
}

bool CardTextEditable(CardKind kind) {
	return kind == CardKind::Send
		|| kind == CardKind::Draft
		|| kind == CardKind::Edit;
}

bool CardAlwaysAsks(CardKind kind) {
	return (kind == CardKind::Draft);
}

bool CardHasAlternate(CardKind kind) {
	return kind == CardKind::Send || kind == CardKind::Draft;
}

Card ApplyDecision(Card card, const CardDecision &decision) {
	if (decision.action == CardAction::Decline) {
		card.decided = CardAction::Decline;
		card.status = CardStatus::Declined;
		return card;
	}
	Expects((decision.action != CardAction::Alternate)
		|| CardHasAlternate(card.kind));
	Expects(decision.checked.size() == card.targets.size());

	auto index = 0;
	for (auto &target : card.targets) {
		target.checked = decision.checked[index++];
	}
	if (CardTextEditable(card.kind)) {
		card.text = decision.text;
	}
	card.option = card.optionAvailable && decision.option;
	if (card.canAllowForTurn) {
		card.allowedForTurn = decision.allowForTurn;
	}
	card.decided = decision.action;
	card.status = CardStatus::Running;
	return card;
}

std::vector<PeerKey> CheckedPeers(const Card &card) {
	auto seen = base::flat_set<PeerKey>();
	auto result = std::vector<PeerKey>();
	for (const auto &target : card.targets) {
		if (target.checked && seen.insert(target.peer).second) {
			result.push_back(target.peer);
		}
	}
	return result;
}

int CheckedCount(const Card &card) {
	auto result = 0;
	for (const auto &target : card.targets) {
		if (target.checked) {
			++result;
		}
	}
	return result;
}

QString CardResultText(const Card &card) {
	Expects(CardFinal(card.status));

	auto lines = QStringList();
	if (card.status == CardStatus::Done) {
		lines.push_back(DoneLine(card));
	} else if (card.status == CardStatus::Drafted) {
		lines.push_back(u"The message was saved to the draft."_q);
	} else if (card.status == CardStatus::Declined) {
		lines.push_back(u"The user declined the action."_q);
	} else if (card.status == CardStatus::Failed) {
		lines.push_back(u"The action failed."_q);
	} else {
		lines.push_back(u"The app was closed while the action was "
			u"waiting, so it was not performed."_q);
	}
	const auto ran = (card.status == CardStatus::Done
		|| card.status == CardStatus::Drafted
		|| card.status == CardStatus::Failed);
	if (ran) {
		auto skipped = QStringList();
		auto failed = QStringList();
		for (const auto &target : card.targets) {
			if (!target.checked) {
				skipped.push_back(TargetName(target));
			} else if (!target.error.isEmpty()) {
				failed.push_back(TargetName(target) + u": "_q + target.error);
			}
		}
		if (!skipped.isEmpty()) {
			lines.push_back(u"Skipped, the user unchecked: "_q
				+ skipped.join(u"; "_q)
				+ u'.');
		}
		for (const auto &line : failed) {
			lines.push_back(u"Failed: "_q + line + u'.');
		}
		if (card.status != CardStatus::Failed
			&& CardTextEditable(card.kind)
			&& card.text != card.proposedText) {
			lines.push_back(u"The user edited the text before it was used. "
				u"The final text:"_q);
			lines.push_back(card.text);
		}
	}
	return lines.join(u'\n');
}

} // namespace Agent
