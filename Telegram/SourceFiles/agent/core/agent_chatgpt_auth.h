/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"
#include "agent/core/agent_transport.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <optional>
#include <vector>

namespace Agent::ChatGpt {

inline constexpr auto kCallbackPort = 1455;
inline constexpr auto kRefreshMarginMs = qint64(3 * 60 * 1000);

struct Pkce {
	QByteArray verifier;
	QByteArray challenge;
};

struct AuthorizeParams {
	QString clientId;
	QString hostId;
	QString redirectUri;
	QByteArray state;
	QByteArray nonce;
	QByteArray challenge;
	QString loginHint;
};

struct CallbackResult {
	QString code;
	QString clientId;
	QString error;
};

struct TokenResult {
	QString accessToken;
	QString refreshToken;
	QString idToken;
	QString scopes;
	qint64 expiresIn = 0;
	QString errorCode;
	QString errorText;
	bool terminal = false;
};

struct IdClaims {
	QString subject;
	QString email;
	QString plan;
	QString issuer;
	QString audience;
	QString nonce;
	qint64 expires = 0;
};

[[nodiscard]] QString AgentName();
[[nodiscard]] QString RedirectUri(int port = kCallbackPort);
[[nodiscard]] QString CallbackPath();
[[nodiscard]] QString UsageUrl();

[[nodiscard]] Pkce GeneratePkce();
[[nodiscard]] QByteArray GenerateState();
[[nodiscard]] QString GenerateHostId();

[[nodiscard]] QString AuthorizeUrl(const AuthorizeParams &params);
[[nodiscard]] CallbackResult ParseCallback(
	const QUrl &url,
	const QByteArray &expectedState,
	const QString &pendingClientId);
[[nodiscard]] CallbackResult ParsePastedUrl(
	const QString &text,
	const QString &redirectUri,
	const QByteArray &expectedState,
	const QString &pendingClientId);

[[nodiscard]] HttpRequest CodeRequest(
	const QString &clientId,
	const QString &code,
	const QByteArray &verifier,
	const QString &redirectUri);
[[nodiscard]] HttpRequest RefreshRequest(
	const QString &clientId,
	const QString &refreshToken);
[[nodiscard]] HttpRequest DiscoveryRequest();
[[nodiscard]] QString ParseRevocationEndpoint(const QByteArray &json);
[[nodiscard]] HttpRequest RevokeRequest(
	const QString &endpoint,
	const QString &clientId,
	const QString &refreshToken);

[[nodiscard]] TokenResult ParseTokenResponse(
	int status,
	const QByteArray &body);
[[nodiscard]] std::optional<IdClaims> ParseIdToken(const QString &token);
[[nodiscard]] bool HasPlanScope(const QString &scopes);

[[nodiscard]] bool SignedIn(const ChatGptAccount &account);
[[nodiscard]] bool NeedsRefresh(
	const ChatGptAccount &account,
	qint64 nowMs);
void ApplyTokens(
	ChatGptAccount &account,
	const TokenResult &tokens,
	qint64 nowMs);
void ClearTokens(ChatGptAccount &account);
[[nodiscard]] bool RefreshStillCurrent(
	const ChatGptAccount &account,
	const QString &usedRefreshToken);

[[nodiscard]] QByteArray SignedOutBody(const QString &text);

} // namespace Agent::ChatGpt
