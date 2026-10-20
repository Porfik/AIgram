/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_chat_participant_status.h"

#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <optional>

namespace Agent::ToolRights {

[[nodiscard]] const QStringList &AllAdminRightNames();
[[nodiscard]] QStringList AdminRightNames(ChatAdminRights rights);
[[nodiscard]] std::optional<ChatAdminRights> ParseAdminRights(
	const QJsonValue &value,
	QString &error);

[[nodiscard]] const QStringList &AllRestrictionNames();
[[nodiscard]] QStringList RestrictionNames(ChatRestrictions restrictions);
[[nodiscard]] std::optional<ChatRestrictions> ParseRestrictions(
	const QJsonValue &value,
	QString &error);

} // namespace Agent::ToolRights
