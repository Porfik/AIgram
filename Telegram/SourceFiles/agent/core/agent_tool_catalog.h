/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <vector>

namespace Agent {

enum class CatalogSection : uchar {
	Read,
	Messages,
	ChatState,
	Folders,
	MediaFiles,
	Memory,
	Tasks,
	Admin,
	Contacts,
	Profile,
	TelegramAi,
	Mtproto,
	Other,
};

struct CatalogEntry {
	QString name;
	QString snippet;
	QString group;
	CatalogSection section = CatalogSection::Other;
};

struct CatalogGroup {
	QString name;
	QString when;
};

[[nodiscard]] QString FormatToolCatalog(
	const std::vector<CatalogEntry> &entries,
	const std::vector<CatalogGroup> &groups);

struct ToolDocument {
	QString name;
	QString text;
};

[[nodiscard]] std::vector<int> RankToolDocuments(
	const QString &query,
	const std::vector<ToolDocument> &documents,
	int limit);

[[nodiscard]] std::vector<QString> NearestToolNames(
	const QString &name,
	const std::vector<QString> &names,
	int limit);

[[nodiscard]] QString MatchSnippet(
	const QString &text,
	const std::vector<QString> &queries,
	int width);

struct TurnContext {
	QDateTime now;
	QString zone;
	std::vector<QString> loadedGroups;
	QString topic;
	int pendingCards = 0;
};

[[nodiscard]] QString FormatTurnContext(const TurnContext &context);

[[nodiscard]] QJsonObject CoerceToolInput(
	const QJsonObject &schema,
	QJsonObject input);

} // namespace Agent
