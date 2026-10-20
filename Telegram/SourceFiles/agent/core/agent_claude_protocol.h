/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_types.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QStringList>

#include <array>
#include <optional>
#include <variant>
#include <vector>

namespace Agent::Claude {

inline constexpr auto kToolTimeoutMs = 24 * 60 * 60 * 1000;
inline constexpr auto kMaxResultChars = 500'000;
inline constexpr auto kMinVersion = std::array<int, 3>{ 2, 1, 304 };

struct SessionOptions {
	QString model;
	QString effort;
	QString system;
	int maxTurns = 25;

	friend inline bool operator==(
		const SessionOptions &,
		const SessionOptions &) = default;
};

[[nodiscard]] QString ServerName();
[[nodiscard]] QString ToolName(const QString &name);
[[nodiscard]] QString OwnToolName(const QString &qualified);

[[nodiscard]] QStringList LaunchArguments(const SessionOptions &options);
[[nodiscard]] QJsonObject InitializeRequest(const QString &system);

[[nodiscard]] QByteArray ControlRequestLine(
	const QString &requestId,
	const QJsonObject &request);
[[nodiscard]] QByteArray ControlResponseLine(
	const QString &requestId,
	const QJsonObject &response);
[[nodiscard]] QByteArray ControlErrorLine(
	const QString &requestId,
	const QString &error);
[[nodiscard]] QByteArray UserLine(const QJsonArray &content, bool query);
[[nodiscard]] QByteArray AssistantLine(const QJsonArray &content);

struct Frame {
	Role role = Role::User;
	QJsonArray content;
};

[[nodiscard]] std::vector<Frame> ReplayFrames(
	const std::vector<Message> &messages);
[[nodiscard]] QJsonArray UserContent(const Message &message);

[[nodiscard]] QJsonObject McpResult(
	const QJsonValue &id,
	const QJsonObject &result);
[[nodiscard]] QJsonObject McpError(
	const QJsonValue &id,
	int code,
	const QString &message);
[[nodiscard]] QJsonObject McpInitializeResult(const QJsonObject &params);
[[nodiscard]] QJsonObject McpToolsList(const std::vector<ToolSpec> &tools);
[[nodiscard]] QJsonObject McpToolResult(const ToolResultBlock &result);

struct ControlRequest {
	QString requestId;
	QString subtype;
	QJsonObject request;
};

struct ControlResponse {
	QString requestId;
	bool success = false;
	QJsonObject response;
	QString error;
};

struct StreamEvent {
	QJsonObject event;
	bool nested = false;
};

struct AssistantEcho {
	QString error;
	QString text;
	bool nested = false;
};

struct UserEcho {
	std::vector<ToolResultBlock> results;
	bool nested = false;
	bool replay = false;
};

struct ResultLine {
	QString subtype;
	bool isError = false;
	int numTurns = 0;
	QString result;
	QString terminalReason;
	QString stopReason;
	std::optional<int> apiErrorStatus;
	QStringList errors;
};

struct LimitWindow {
	double utilization = -1.;
	qint64 resetsAt = 0;

	friend inline bool operator==(
		const LimitWindow &,
		const LimitWindow &) = default;
};

struct Limits {
	QString status;
	LimitWindow fiveHour;
	LimitWindow sevenDay;
	qint64 received = 0;

	friend inline bool operator==(const Limits &, const Limits &) = default;
};

struct SystemInit {
	QString model;
	QString version;
	QStringList tools;
	QStringList capabilities;
};

using Line = std::variant<
	std::monostate,
	ControlRequest,
	ControlResponse,
	StreamEvent,
	AssistantEcho,
	UserEcho,
	ResultLine,
	Limits,
	SystemInit>;

[[nodiscard]] Line ParseLine(const QByteArray &line);

struct ModelOption {
	QString value;
	QString resolved;
	QString name;
	QString description;
	std::vector<QString> effortLevels;
};

[[nodiscard]] std::vector<ModelOption> ParseModels(
	const QJsonObject &initializeResponse);

struct AuthStatus {
	bool loggedIn = false;
	QString method;
	QString email;
	QString subscription;
};

[[nodiscard]] std::optional<AuthStatus> ParseAuthStatus(
	const QByteArray &json);
[[nodiscard]] std::optional<std::array<int, 3>> ParseVersion(
	const QByteArray &output);
[[nodiscard]] bool VersionSupported(const std::array<int, 3> &version);

[[nodiscard]] Error ResultError(
	const ResultLine &result,
	const QString &assistantError);

} // namespace Agent::Claude
