/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_card.h"

namespace Agent {

bool CardFinal(CardStatus status) {
	return false;
}

bool CardTextEditable(CardKind kind) {
	return false;
}

bool CardHasAlternate(CardKind kind) {
	return false;
}

Card ApplyDecision(Card card, const CardDecision &decision) {
	return card;
}

std::vector<PeerKey> CheckedPeers(const Card &card) {
	return {};
}

int CheckedCount(const Card &card) {
	return 0;
}

QString CardResultText(const Card &card) {
	return QString();
}

} // namespace Agent
