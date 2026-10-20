/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_claude_engine.h"
#include "base/timer.h"

#include <rpl/producer.h>

#include <memory>
#include <vector>

namespace Agent {

class ClaudeLauncher final : public Claude::Launcher {
public:
	[[nodiscard]] std::unique_ptr<Claude::Process> start(
		Claude::LaunchSpec spec,
		Claude::ProcessHandlers handlers) override;

};

class ClaudePool final : public Claude::ConnectionPool {
public:
	ClaudePool();
	~ClaudePool();

	[[nodiscard]] std::unique_ptr<Claude::Connection> take(
		const QString &owner) override;
	void give(std::unique_ptr<Claude::Connection> connection) override;
	void clear();

private:
	struct Entry {
		std::unique_ptr<Claude::Connection> connection;
		crl::time given = 0;
	};

	void clearIdle();

	std::vector<Entry> _entries;
	base::Timer _timer;

};

class ClaudeCommand final {
public:
	ClaudeCommand(
		const QString &program,
		QStringList arguments,
		Fn<void(int exitCode, QByteArray output)> done,
		Fn<void(QByteArray output)> progress = nullptr);
	~ClaudeCommand();

private:
	class Runner;

	const std::unique_ptr<Runner> _runner;

};

[[nodiscard]] QString FindClaudeProgram(const QString &configured);
[[nodiscard]] Claude::LaunchSettings ClaudeLaunchSettings(
	const QString &program);
[[nodiscard]] QString ClaudeInstallCommand();
[[nodiscard]] QString ClaudeMissingText();

[[nodiscard]] const Claude::Limits &ClaudeLimits();
[[nodiscard]] rpl::producer<Claude::Limits> ClaudeLimitsValue();
void UpdateClaudeLimits(Claude::Limits limits);

} // namespace Agent
