/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

namespace Agent {

struct SettingEntry {
	QString id;
	QString title;
	QStringList keywords;
	QString path;
	QString deeplink;
	std::optional<bool> checked;
};

[[nodiscard]] std::vector<int> RankSettings(
	const QString &query,
	const std::vector<SettingEntry> &entries,
	int limit);

} // namespace Agent
