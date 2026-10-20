/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QFile>

#include <vector>

namespace Agent::Tests {

struct Check {
	QString name;
	Fn<QString()> run;
};

[[nodiscard]] inline QByteArray ReadFixture(const QString &name) {
	auto file = QFile(
		QString::fromUtf8(AGENT_TEST_FIXTURES_PATH) + u"/"_q + name);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

[[nodiscard]] inline QByteArray ReadApiTl() {
	auto file = QFile(QString::fromUtf8(AGENT_TEST_API_TL_PATH));
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

[[nodiscard]] std::vector<Check> SseChecks();
[[nodiscard]] std::vector<Check> OpenAiChecks();
[[nodiscard]] std::vector<Check> ProvidersChecks();
[[nodiscard]] std::vector<Check> LoopChecks();
[[nodiscard]] std::vector<Check> PolicyChecks();
[[nodiscard]] std::vector<Check> RecordChecks();
[[nodiscard]] std::vector<Check> FeedChecks();
[[nodiscard]] std::vector<Check> ContextChecks();
[[nodiscard]] std::vector<Check> PromptsChecks();
[[nodiscard]] std::vector<Check> RequestChecks();
[[nodiscard]] std::vector<Check> CardChecks();
[[nodiscard]] std::vector<Check> NavigationChecks();
[[nodiscard]] std::vector<Check> MemoryChecks();
[[nodiscard]] std::vector<Check> SpendingChecks();
[[nodiscard]] std::vector<Check> ExclusionChecks();
[[nodiscard]] std::vector<Check> MediaChecks();
[[nodiscard]] std::vector<Check> DocumentsChecks();
[[nodiscard]] std::vector<Check> TaskChecks();
[[nodiscard]] std::vector<Check> TlChecks();
[[nodiscard]] std::vector<Check> MtprotoChecks();

} // namespace Agent::Tests
