/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>
#include <utility>
#include <vector>

namespace Agent::Claude {

struct LaunchSpec {
	QString program;
	QStringList arguments;
	QString workingDirectory;
	std::vector<std::pair<QString, QString>> environment;
};

struct ProcessHandlers {
	Fn<void(QByteArray)> line;
	Fn<void(QString)> finished;
};

class Process {
public:
	virtual ~Process() = default;

	virtual void write(const QByteArray &line) = 0;

};

class Launcher {
public:
	virtual ~Launcher() = default;

	[[nodiscard]] virtual std::unique_ptr<Process> start(
		LaunchSpec spec,
		ProcessHandlers handlers) = 0;

};

} // namespace Agent::Claude
