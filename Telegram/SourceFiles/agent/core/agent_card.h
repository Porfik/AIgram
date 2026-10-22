/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"

#include <QtCore/QString>

#include <vector>

namespace Agent {

enum class CardKind : uchar {
	Send,
	Draft,
	Forward,
	Edit,
	Delete,
	React,
	PinMessage,
	UnpinMessage,
	DeleteFolder,
	Handoff,
	Transcribe,
	Download,
	SendFiles,
	CreateTask,
	UpdateTask,
	Action,
	Change,
	Method,
};

enum class CardStatus : uchar {
	Pending,
	Running,
	Done,
	Drafted,
	Declined,
	Failed,
	NotDone,
	Expired,
};

enum class CardAction : uchar {
	Execute,
	Alternate,
	Decline,
};

struct CardTarget {
	PeerKey peer = 0;
	int64 messageId = 0;
	QString title;
	QString preview;
	bool checked = true;
	QString error;
};

struct CardFile {
	QString ref;
	QString name;
	QString mimeType;
	int64 size = 0;
};

struct CardRow {
	QString label;
	QString value;
	QString previous;
};

struct Card {
	QString toolUseId;
	CardKind kind = CardKind::Send;
	CardStatus status = CardStatus::Pending;
	CardAction decided = CardAction::Decline;
	std::vector<CardTarget> targets;
	QString text;
	QString proposedText;
	QString detail;
	bool option = false;
	bool optionAvailable = false;
	bool canAllowForTurn = false;
	bool allowedForTurn = false;
	bool autoApproved = false;
	std::vector<CardFile> files;
	int64 amount = 0;
	int64 quota = 0;
	QByteArray task;
	QByteArray previousTask;
	std::vector<CardRow> rows;
};

struct CardDecision {
	CardAction action = CardAction::Decline;
	std::vector<bool> checked;
	QString text;
	bool option = false;
	bool allowForTurn = false;
};

[[nodiscard]] bool CardFinal(CardStatus status);
[[nodiscard]] bool CardTextEditable(CardKind kind);
[[nodiscard]] bool CardHasAlternate(CardKind kind);
[[nodiscard]] bool CardAlwaysAsks(CardKind kind);
[[nodiscard]] Card ApplyDecision(Card card, const CardDecision &decision);
[[nodiscard]] std::vector<PeerKey> CheckedPeers(const Card &card);
[[nodiscard]] int CheckedCount(const Card &card);
[[nodiscard]] std::vector<QString> CardAllowanceKeys(
	const Card &card,
	bool checkedOnly);
[[nodiscard]] CardStatus TargetsStatus(const Card &card);
[[nodiscard]] QString CardResultText(const Card &card);

} // namespace Agent
