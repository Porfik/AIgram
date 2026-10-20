/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_types.h"

#include <QtCore/QStringList>

#include <optional>

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Agent::Ui {

[[nodiscard]] QJsonObject ToolResultObject(
	const std::optional<ToolResultBlock> &result);
[[nodiscard]] int64 JsonIdValue(const QJsonValue &value);
[[nodiscard]] HistoryItem *ToolResultMessage(
	Main::Session *session,
	const QJsonObject &input,
	const QJsonObject &object);
[[nodiscard]] int64 MediaDurationSeconds(HistoryItem *item);
[[nodiscard]] int64 ResultDurationSeconds(
	const QJsonObject &object,
	HistoryItem *item);
[[nodiscard]] QString WithDuration(QString title, int64 seconds);
[[nodiscard]] bool ResultFromMediaModel(const QJsonObject &object);
[[nodiscard]] QString ToolTitleBaseText(
	const ToolUseBlock &call,
	const std::optional<ToolResultBlock> &result,
	Main::Session *session);
[[nodiscard]] QString ToolTitleText(
	const ToolUseBlock &call,
	const std::optional<ToolResultBlock> &result,
	Main::Session *session);
[[nodiscard]] bool IsMediaTool(const QString &name);
[[nodiscard]] MediaBlock ResultMediaBlock(const QJsonObject &object);
[[nodiscard]] QString ResultTranscriptText(
	const QString &name,
	const QJsonObject &object);
[[nodiscard]] QString CutForDisplay(const QString &text);
[[nodiscard]] QString IndentedJson(const QJsonDocument &document);
[[nodiscard]] QString PrettyJson(const QString &text);
[[nodiscard]] QString ArgumentsText(const ToolUseBlock &call);
[[nodiscard]] QString ResultText(const ToolResultBlock &result);
[[nodiscard]] TextWithEntities Monospace(QString text);
[[nodiscard]] QString CardTitleText(CardKind kind);

struct ParsedAction {
	QStringList done;
	QStringList failed;
	QStringList failedTitles;
	QString folder;
	QString fact;
	QStringList saved;
	QString savedPath;
	QString taskTitle;
	QString taskState;
	QString name;

	[[nodiscard]] QString chats() const {
		return (done.isEmpty() ? failedTitles : done).join(u", "_q);
	}
};

[[nodiscard]] ParsedAction ParseActionResult(
	const std::optional<ToolResultBlock> &result);
[[nodiscard]] QString ActionMainText(
	const QString &name,
	const QJsonObject &input,
	const QString &chats,
	const QString &folder,
	const QString &fact,
	const ParsedAction &parsed);

} // namespace Agent::Ui
