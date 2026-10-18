/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_context.h"

namespace Agent {

int EstimateTokens(const QString &text) {
	return 0;
}

int EstimateTokens(const Message &message) {
	return 0;
}

int EstimateTokens(const Request &request) {
	return 0;
}

QString CapToolResult(QString content, int maxChars) {
	return content;
}

std::vector<Message> ContextMessages(const SessionRecord &record) {
	return record.messages;
}

int PlanCompaction(
		const SessionRecord &record,
		int fixedTokens,
		int contextWindow) {
	return record.summaryUntil;
}

Request CompactionRequest(
		const SessionRecord &record,
		int until,
		const QString &model) {
	return { .model = model };
}

Request TitleRequest(const SessionRecord &record, const QString &model) {
	return { .model = model };
}

QString ResponseText(const Message &message) {
	return QString();
}

QString CleanTitle(const QString &text) {
	return text;
}

bool IsContextOverflow(const Error &error) {
	return false;
}

} // namespace Agent
