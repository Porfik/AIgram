/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_record.h"

#include <vector>

namespace Agent {

inline constexpr auto kDefaultContextWindow = 32768;
inline constexpr auto kToolResultMaxChars = 12000;
inline constexpr auto kHandoffMaxChars = 24000;

[[nodiscard]] int EstimateTokens(const QString &text);
[[nodiscard]] int EstimateTokens(const Message &message);
[[nodiscard]] int EstimateTokens(const Request &request);

[[nodiscard]] QString CapToolResult(QString content, int maxChars);

[[nodiscard]] std::vector<Message> ContextMessages(
	const SessionRecord &record);
bool DropMediaBytes(SessionRecord &record);
[[nodiscard]] int PlanCompaction(
	const SessionRecord &record,
	int fixedTokens,
	int contextWindow);
[[nodiscard]] Request CompactionRequest(
	const SessionRecord &record,
	int until,
	const QString &model);
[[nodiscard]] Request TitleRequest(
	const SessionRecord &record,
	const QString &model);

[[nodiscard]] QString HandoffText(
	const SessionRecord &record,
	const QString &chatTitle);

[[nodiscard]] QString ResponseText(const Message &message);
[[nodiscard]] QString CleanTitle(const QString &text);
[[nodiscard]] bool IsContextOverflow(const Error &error);

} // namespace Agent
