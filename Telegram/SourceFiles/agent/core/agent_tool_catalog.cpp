/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_tool_catalog.h"

namespace Agent {

QString FormatToolCatalog(
		const std::vector<CatalogEntry> &entries,
		const std::vector<CatalogGroup> &groups) {
	return QString();
}

std::vector<int> RankToolDocuments(
		const QString &query,
		const std::vector<ToolDocument> &documents,
		int limit) {
	return {};
}

std::vector<QString> NearestToolNames(
		const QString &name,
		const std::vector<QString> &names,
		int limit) {
	return {};
}

QString MatchSnippet(
		const QString &text,
		const std::vector<QString> &queries,
		int width) {
	return text.left(width);
}

QString FormatTurnContext(const TurnContext &context) {
	return QString();
}

} // namespace Agent
