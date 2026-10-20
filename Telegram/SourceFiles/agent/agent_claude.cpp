/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_claude.h"

#include "base/unixtime.h"
#include "base/weak_ptr.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkProxy>

#include <rpl/variable.h>

namespace Agent {
namespace {

constexpr auto kMaxWarm = 3;
constexpr auto kIdleTimeout = 10 * 60 * crl::time(1000);
constexpr auto kErrorTail = 4096;
constexpr auto kKillWait = 1000;

[[nodiscard]] bool RemovedVariable(const QString &name) {
	if (name == u"ANTHROPIC_API_KEY"_q
		|| name == u"ANTHROPIC_AUTH_TOKEN"_q) {
		return true;
	}
	return name.startsWith(u"CLAUDE"_q)
		&& (name != u"CLAUDE_CONFIG_DIR"_q);
}

[[nodiscard]] QProcessEnvironment ProcessEnvironment(
		const std::vector<std::pair<QString, QString>> &set) {
	auto result = QProcessEnvironment::systemEnvironment();
	for (const auto &name : result.keys()) {
		if (RemovedVariable(name)) {
			result.remove(name);
		}
	}
	for (const auto &[name, value] : set) {
		result.insert(name, value);
	}
	return result;
}

[[nodiscard]] QString LastLine(const QByteArray &bytes) {
	const auto lines = QString::fromUtf8(bytes).split(
		'\n',
		Qt::SkipEmptyParts);
	for (auto i = lines.size(); i > 0;) {
		const auto line = lines[--i].trimmed();
		if (!line.isEmpty()) {
			return line;
		}
	}
	return QString();
}

void Kill(QProcess &process) {
	process.disconnect();
	if (process.state() != QProcess::NotRunning) {
		process.kill();
		process.waitForFinished(kKillWait);
	}
}

class QtProcess final : public Claude::Process, public base::has_weak_ptr {
public:
	QtProcess(Claude::LaunchSpec spec, Claude::ProcessHandlers handlers);
	~QtProcess();

	void write(const QByteArray &line) override;

private:
	void readOutput();
	void readErrors();
	void finish(QString error);

	QProcess _process;
	Claude::ProcessHandlers _handlers;
	QByteArray _buffer;
	QByteArray _errors;
	bool _finished = false;

};

QtProcess::QtProcess(
	Claude::LaunchSpec spec,
	Claude::ProcessHandlers handlers)
: _handlers(std::move(handlers)) {
	if (!spec.workingDirectory.isEmpty()) {
		QDir().mkpath(spec.workingDirectory);
		_process.setWorkingDirectory(spec.workingDirectory);
	}
	_process.setProcessEnvironment(ProcessEnvironment(spec.environment));
	QObject::connect(&_process, &QProcess::readyReadStandardOutput, [=] {
		readOutput();
	});
	QObject::connect(&_process, &QProcess::readyReadStandardError, [=] {
		readErrors();
	});
	QObject::connect(&_process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			finish(u"could not start %1"_q.arg(spec.program));
		}
	});
	QObject::connect(&_process, &QProcess::finished, [=](
			int exitCode,
			QProcess::ExitStatus status) {
		const auto weak = base::make_weak(this);
		readOutput();
		if (!weak) {
			return;
		}
		readErrors();
		const auto tail = LastLine(_errors);
		finish((status == QProcess::NormalExit && !exitCode)
			? QString()
			: (status == QProcess::CrashExit)
			? u"crashed"_q
			: tail.isEmpty()
			? u"exit code %1"_q.arg(exitCode)
			: u"exit code %1, %2"_q.arg(exitCode).arg(tail));
	});
	_process.start(spec.program, spec.arguments);
}

QtProcess::~QtProcess() {
	Kill(_process);
}

void QtProcess::write(const QByteArray &line) {
	if (_process.state() == QProcess::Running) {
		_process.write(line + '\n');
	}
}

void QtProcess::readOutput() {
	_buffer += _process.readAllStandardOutput();
	const auto weak = base::make_weak(this);
	while (true) {
		const auto newline = _buffer.indexOf('\n');
		if (newline < 0) {
			return;
		}
		auto line = _buffer.left(newline);
		_buffer.remove(0, newline + 1);
		if (line.trimmed().isEmpty()) {
			continue;
		} else if (const auto handler = _handlers.line) {
			handler(std::move(line));
			if (!weak) {
				return;
			}
		}
	}
}

void QtProcess::readErrors() {
	_errors += _process.readAllStandardError();
	if (_errors.size() > kErrorTail) {
		_errors = _errors.mid(_errors.size() - kErrorTail);
	}
}

void QtProcess::finish(QString error) {
	if (_finished) {
		return;
	}
	_finished = true;
	if (const auto finished = base::take(_handlers.finished)) {
		_handlers.line = nullptr;
		finished(std::move(error));
	}
}

[[nodiscard]] QString ProxyUrl(const QNetworkProxy &proxy) {
	auto url = QUrl();
	if (proxy.type() == QNetworkProxy::HttpProxy) {
		url.setScheme(u"http"_q);
	} else if (proxy.type() == QNetworkProxy::Socks5Proxy) {
		url.setScheme(u"socks5h"_q);
	} else {
		return QString();
	}
	url.setHost(proxy.hostName());
	url.setPort(proxy.port());
	if (!proxy.user().isEmpty()) {
		url.setUserName(proxy.user());
		url.setPassword(proxy.password());
	}
	return url.toString(QUrl::FullyEncoded);
}

[[nodiscard]] rpl::variable<Claude::Limits> &LimitsVariable() {
	static auto result = rpl::variable<Claude::Limits>();
	return result;
}

} // namespace

std::unique_ptr<Claude::Process> ClaudeLauncher::start(
		Claude::LaunchSpec spec,
		Claude::ProcessHandlers handlers) {
	return std::make_unique<QtProcess>(std::move(spec), std::move(handlers));
}

ClaudePool::ClaudePool()
: _timer([=] { clearIdle(); }) {
}

ClaudePool::~ClaudePool() = default;

std::unique_ptr<Claude::Connection> ClaudePool::take(const QString &owner) {
	for (auto i = begin(_entries); i != end(_entries); ++i) {
		if (i->connection->owner() == owner) {
			auto result = std::move(i->connection);
			_entries.erase(i);
			return result->alive() ? std::move(result) : nullptr;
		}
	}
	return nullptr;
}

void ClaudePool::give(std::unique_ptr<Claude::Connection> connection) {
	if (!connection->alive()) {
		return;
	}
	const auto owner = connection->owner();
	_entries.erase(ranges::remove(_entries, owner, [](const Entry &entry) {
		return entry.connection->owner();
	}), end(_entries));
	_entries.push_back({ std::move(connection), crl::now() });
	while (int(_entries.size()) > kMaxWarm) {
		_entries.erase(begin(_entries));
	}
	if (!_timer.isActive()) {
		_timer.callOnce(kIdleTimeout);
	}
}

void ClaudePool::clear() {
	_timer.cancel();
	base::take(_entries);
}

void ClaudePool::clearIdle() {
	const auto now = crl::now();
	auto next = crl::time(0);
	for (auto i = begin(_entries); i != end(_entries);) {
		const auto expires = i->given + kIdleTimeout;
		if (expires <= now || !i->connection->alive()) {
			i = _entries.erase(i);
		} else {
			next = next ? std::min(next, expires) : expires;
			++i;
		}
	}
	if (next) {
		_timer.callOnce(next - now);
	}
}

class ClaudeCommand::Runner final {
public:
	Runner(
		const QString &program,
		QStringList arguments,
		Fn<void(int, QByteArray)> done,
		Fn<void(QByteArray)> progress);
	~Runner();

private:
	QProcess _process;
	QByteArray _output;
	Fn<void(int, QByteArray)> _done;

};

ClaudeCommand::Runner::Runner(
	const QString &program,
	QStringList arguments,
	Fn<void(int, QByteArray)> done,
	Fn<void(QByteArray)> progress)
: _done(std::move(done)) {
	_process.setProcessEnvironment(ProcessEnvironment({
		{ u"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC"_q, u"1"_q },
	}));
	_process.setProcessChannelMode(QProcess::MergedChannels);
	QObject::connect(&_process, &QProcess::readyRead, [=] {
		const auto chunk = _process.readAll();
		_output += chunk;
		if (progress) {
			progress(chunk);
		}
	});
	const auto finish = [=](int code) {
		if (const auto done = base::take(_done)) {
			_output += _process.readAll();
			done(code, base::take(_output));
		}
	};
	QObject::connect(&_process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			finish(-1);
		}
	});
	QObject::connect(&_process, &QProcess::finished, [=](
			int exitCode,
			QProcess::ExitStatus status) {
		finish((status == QProcess::NormalExit) ? exitCode : -1);
	});
	_process.start(program, arguments);
	_process.closeWriteChannel();
}

ClaudeCommand::Runner::~Runner() {
	Kill(_process);
}

ClaudeCommand::ClaudeCommand(
	const QString &program,
	QStringList arguments,
	Fn<void(int exitCode, QByteArray output)> done,
	Fn<void(QByteArray output)> progress)
: _runner(std::make_unique<Runner>(
	program,
	std::move(arguments),
	std::move(done),
	std::move(progress))) {
}

ClaudeCommand::~ClaudeCommand() = default;

QString FindClaudeProgram(const QString &configured) {
	const auto executable = [](const QString &path) {
		const auto info = QFileInfo(path);
		return info.isFile() && info.isExecutable();
	};
	if (!configured.isEmpty()) {
		return executable(configured) ? configured : QString();
	}
	const auto found = QStandardPaths::findExecutable(u"claude"_q);
	if (!found.isEmpty()) {
		return found;
	}
	const auto home = QDir::homePath();
	const auto candidates = {
#ifdef Q_OS_WIN
		home + u"/.local/bin/claude.exe"_q,
#else // Q_OS_WIN
		home + u"/.local/bin/claude"_q,
		home + u"/.claude/local/claude"_q,
		u"/usr/local/bin/claude"_q,
		u"/opt/homebrew/bin/claude"_q,
#endif // Q_OS_WIN
	};
	for (const auto &candidate : candidates) {
		if (executable(candidate)) {
			return candidate;
		}
	}
	return QString();
}

Claude::LaunchSettings ClaudeLaunchSettings(const QString &program) {
	auto result = Claude::LaunchSettings();
	result.program = program;
	result.workingDirectory = QDir::tempPath() + u"/aigram-claude"_q;
	result.environment.push_back({
		u"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC"_q,
		u"1"_q,
	});
	const auto proxy = ProxyUrl(QNetworkProxy::applicationProxy());
	if (!proxy.isEmpty()) {
		result.environment.push_back({ u"HTTPS_PROXY"_q, proxy });
		result.environment.push_back({ u"HTTP_PROXY"_q, proxy });
	}
	return result;
}

QString ClaudeMissingText() {
	return u"Claude Code (claude) was not found."_q;
}

QString ClaudeInstallCommand() {
#ifdef Q_OS_WIN
	return u"irm https://claude.ai/install.ps1 | iex"_q;
#else // Q_OS_WIN
	return u"curl -fsSL https://claude.ai/install.sh | bash"_q;
#endif // Q_OS_WIN
}

const Claude::Limits &ClaudeLimits() {
	return LimitsVariable().current();
}

rpl::producer<Claude::Limits> ClaudeLimitsValue() {
	return LimitsVariable().value();
}

void UpdateClaudeLimits(Claude::Limits limits) {
	limits.received = base::unixtime::now();
	LimitsVariable() = std::move(limits);
}

} // namespace Agent
