/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_claude_process.h"
#include "agent/core/agent_claude_protocol.h"
#include "agent/core/agent_engine.h"
#include "agent/core/agent_request.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>

#include <memory>
#include <optional>
#include <vector>

namespace Agent::Anthropic {
class StreamAssembler;
} // namespace Agent::Anthropic

namespace Agent::Claude {

struct LaunchSettings {
	QString program;
	QString workingDirectory;
	std::vector<std::pair<QString, QString>> environment;
};

struct ConnectionKey {
	QString owner;
	int messages = 0;
	int summaryUntil = 0;

	friend inline bool operator==(
		const ConnectionKey &,
		const ConnectionKey &) = default;
};

class Connection final {
public:
	Connection(
		not_null<Launcher*> launcher,
		const LaunchSettings &launch,
		SessionOptions options,
		QString owner);
	~Connection();

	[[nodiscard]] const SessionOptions &options() const;
	[[nodiscard]] const QString &owner() const;
	[[nodiscard]] const ConnectionKey &key() const;
	void setKey(ConnectionKey key);

	[[nodiscard]] bool alive() const;
	[[nodiscard]] bool initialized() const;
	void setInitialized();

	void setHandlers(Fn<void(Line)> line, Fn<void(QString)> finished);
	void write(const QByteArray &line);
	[[nodiscard]] QString nextRequestId();

private:
	void handleLine(const QByteArray &line);
	void handleFinished(const QString &error);

	const SessionOptions _options;
	const QString _owner;
	ConnectionKey _key;
	std::unique_ptr<Process> _process;
	Fn<void(Line)> _line;
	Fn<void(QString)> _finished;
	std::optional<QString> _exit;
	int _requestId = 0;
	bool _initialized = false;

};

class ConnectionPool {
public:
	virtual ~ConnectionPool() = default;

	[[nodiscard]] virtual std::unique_ptr<Connection> take(
		const QString &owner) = 0;
	virtual void give(std::unique_ptr<Connection> connection) = 0;

};

struct EngineOptions {
	SessionOptions session;
	LaunchSettings launch;
	QString owner;
	int recordMessages = 0;
	int summaryUntil = 0;
	bool keepWarm = true;
	Fn<void(Limits)> limits;
};

class Engine final : public Agent::Engine, public base::has_weak_ptr {
public:
	Engine(
		not_null<Launcher*> launcher,
		ConnectionPool *pool,
		not_null<ToolExecutor*> tools,
		EngineOptions options,
		std::vector<Message> history = {});
	~Engine();

	void send(Message message) override;
	void resume() override;
	void interrupt() override;
	[[nodiscard]] bool busy() const override;
	[[nodiscard]] const std::vector<Message> &history() const override;
	[[nodiscard]] rpl::producer<EngineEvent> events() const override;

private:
	enum class Phase : uchar {
		Idle,
		Initializing,
		Replaying,
		Querying,
	};
	struct PendingUse {
		ToolUseBlock use;
		std::optional<ToolResultBlock> result;
		QJsonValue mcpId;
		QString mcpRequestId;
		bool requested = false;
		bool running = false;
	};

	void start();
	void attach();
	void sendInitialize();
	void replayNext();
	void sendQuery();
	void handleLine(Line line);
	void handleExit(const QString &error);
	void handleStream(const QJsonObject &event);
	void handleControl(const ControlRequest &request);
	void handleMcp(const QString &requestId, const QJsonObject &message);
	void handleToolCall(
		const QString &requestId,
		const QJsonValue &id,
		const QJsonObject &params);
	void handleCancelled(const QJsonObject &params);
	void handleEcho(const UserEcho &echo);
	void handleResult(const ResultLine &result);
	void completeTool(const QString &toolUseId, ToolResultBlock result);
	void finishToolsIfReady();
	void announceToolsChange();
	void keepPartialText();
	void finishTurn(TurnEnd end, std::optional<Error> error);
	void releaseConnection(bool clean);
	[[nodiscard]] PendingUse *findUse(const QString &toolUseId);
	[[nodiscard]] QStringList toolNames() const;

	const not_null<Launcher*> _launcher;
	ConnectionPool * const _pool = nullptr;
	const not_null<ToolExecutor*> _tools;
	const EngineOptions _options;
	std::vector<Message> _history;
	rpl::event_stream<EngineEvent> _events;

	std::unique_ptr<Connection> _connection;
	std::unique_ptr<Anthropic::StreamAssembler> _assembler;
	std::vector<Frame> _replay;
	int _replayIndex = 0;
	QString _initRequestId;
	Phase _phase = Phase::Idle;
	std::vector<PendingUse> _uses;
	QStringList _announcedTools;
	QString _streamText;
	QString _assistantError;
	StopReason _lastStop = StopReason::EndTurn;
	int _appended = 0;
	int _generation = 0;
	bool _busy = false;

};

class ModelsQuery final {
public:
	ModelsQuery(
		not_null<Launcher*> launcher,
		const LaunchSettings &launch,
		Fn<void(std::vector<ModelOption> models, QString error)> done);
	~ModelsQuery();

private:
	void finish(std::vector<ModelOption> models, QString error);

	std::unique_ptr<Connection> _connection;
	QString _requestId;
	Fn<void(std::vector<ModelOption>, QString)> _done;

};

class OneShot final : public PendingRequest, public base::has_weak_ptr {
public:
	OneShot(
		not_null<Launcher*> launcher,
		LaunchSettings launch,
		SessionOptions options,
		std::vector<Message> messages,
		Fn<void(Response)> done);
	~OneShot();

private:
	class NoTools;

	const std::unique_ptr<NoTools> _tools;
	std::unique_ptr<Engine> _engine;
	Fn<void(Response)> _done;
	Usage _usage;
	rpl::lifetime _lifetime;

};

} // namespace Agent::Claude
