/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_chatgpt_auth.h"

namespace Agent::ChatGpt {

QString AgentName() {
	return u"AIgram"_q;
}

QString RedirectUri(int port) {
	return u"http://127.0.0.1:%1/auth/callback"_q.arg(port);
}

QString CallbackPath() {
	return u"/auth/callback"_q;
}

QString UsageUrl() {
	return u"https://chatgpt.com/settings/usage"_q;
}

Pkce GeneratePkce() {
	return {};
}

QByteArray GenerateState() {
	return QByteArray();
}

QString GenerateHostId() {
	return QString();
}

QString AuthorizeUrl(const AuthorizeParams &params) {
	return QString();
}

CallbackResult ParseCallback(
		const QUrl &url,
		const QByteArray &expectedState,
		const QString &pendingClientId) {
	return {};
}

CallbackResult ParsePastedUrl(
		const QString &text,
		const QString &redirectUri,
		const QByteArray &expectedState,
		const QString &pendingClientId) {
	return {};
}

HttpRequest CodeRequest(
		const QString &clientId,
		const QString &code,
		const QByteArray &verifier,
		const QString &redirectUri) {
	return {};
}

HttpRequest RefreshRequest(
		const QString &clientId,
		const QString &refreshToken) {
	return {};
}

HttpRequest DiscoveryRequest() {
	return {};
}

QString ParseRevocationEndpoint(const QByteArray &json) {
	return QString();
}

HttpRequest RevokeRequest(
		const QString &endpoint,
		const QString &clientId,
		const QString &refreshToken) {
	return {};
}

TokenResult ParseTokenResponse(int status, const QByteArray &body) {
	return {};
}

std::optional<IdClaims> ParseIdToken(const QString &token) {
	return std::nullopt;
}

bool HasPlanScope(const QString &scopes) {
	return false;
}

bool SignedIn(const ChatGptAccount &account) {
	return !account.refreshToken.isEmpty();
}

bool NeedsRefresh(const ChatGptAccount &account, qint64 nowMs) {
	return true;
}

void ApplyTokens(
		ChatGptAccount &account,
		const TokenResult &tokens,
		qint64 nowMs) {
}

void ClearTokens(ChatGptAccount &account) {
}

QByteArray SignedOutBody(const QString &text) {
	return QByteArray();
}

} // namespace Agent::ChatGpt
