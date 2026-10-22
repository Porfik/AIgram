/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_chatgpt_auth.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRandomGenerator>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace Agent::ChatGpt {
namespace {

constexpr auto kRandomBytes = 32;
constexpr auto kTokenUrl = "https://auth.openai.com/api/accounts/oauth/token";
constexpr auto kDiscoveryUrl
	= "https://auth.openai.com/.well-known/openid-configuration";
constexpr auto kAuthorizeUrl
	= "https://auth.openai.com/api/accounts/authorize";
constexpr auto kResource = "https://api.openai.com/v1";
constexpr auto kScope = "openid profile email offline_access "
	"resource.invoke chatgpt.tokens.use.direct";
constexpr auto kPlanScope = "chatgpt.tokens.use.direct";
constexpr auto kDynamicClientId = "dynamic_agent_client";

[[nodiscard]] QByteArray ToBase64Url(const QByteArray &bytes) {
	return bytes.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

[[nodiscard]] QByteArray RandomBytes() {
	auto bytes = QByteArray(kRandomBytes, Qt::Uninitialized);
	auto generator = QRandomGenerator::system();
	for (auto &byte : bytes) {
		byte = static_cast<char>(generator->generate());
	}
	return bytes;
}

[[nodiscard]] QString FormValue(const QString &value) {
	return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

[[nodiscard]] QByteArray FormBody(const std::vector<std::pair<QString, QString>> &pairs) {
	auto result = QByteArray();
	for (auto i = 0, count = int(pairs.size()); i != count; ++i) {
		if (i > 0) {
			result.append('&');
		}
		result.append(FormValue(pairs[i].first).toUtf8());
		result.append('=');
		result.append(FormValue(pairs[i].second).toUtf8());
	}
	return result;
}

[[nodiscard]] bool IsTerminalError(const QString &errorCode) {
	return errorCode == u"invalid_grant"_q
		|| errorCode == u"invalid_refresh_token"_q
		|| errorCode == u"token_expired"_q
		|| errorCode == u"refresh_token_expired"_q
		|| errorCode == u"refresh_token_invalidated"_q
		|| errorCode == u"refresh_token_reused"_q
		|| errorCode == u"invalid_client"_q;
}

[[nodiscard]] HttpRequest TokenRequest(
		const std::vector<std::pair<QString, QString>> &pairs) {
	auto request = HttpRequest();
	request.url = QString::fromUtf8(kTokenUrl);
	request.headers = {
		{ "content-type", "application/x-www-form-urlencoded" },
		{ "accept", "application/json" },
	};
	request.body = FormBody(pairs);
	request.post = true;
	return request;
}

} // namespace

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
	const auto verifier = ToBase64Url(RandomBytes());
	const auto hash = QCryptographicHash::hash(
		verifier,
		QCryptographicHash::Sha256);
	return {
		.verifier = verifier,
		.challenge = ToBase64Url(hash),
	};
}

QByteArray GenerateState() {
	return ToBase64Url(RandomBytes());
}

QString GenerateHostId() {
	auto bytes = RandomBytes();
	auto data = reinterpret_cast<uchar *>(bytes.data());
	data[6] = (data[6] & 0x0F) | 0x40;
	data[8] = (data[8] & 0x3F) | 0x80;
	const auto hex = bytes.toHex();
	return u"urn:uuid:%1-%2-%3-%4-%5"_q.arg(
		QString::fromLatin1(hex.mid(0, 8)),
		QString::fromLatin1(hex.mid(8, 4)),
		QString::fromLatin1(hex.mid(12, 4)),
		QString::fromLatin1(hex.mid(16, 4)),
		QString::fromLatin1(hex.mid(20, 12)));
}

// WHY: QUrlQuery leaves ':' and '/' literal inside values, but the
// contract percent-encodes every value.
QString AuthorizeUrl(const AuthorizeParams &params) {
	const auto clientId = params.clientId.isEmpty()
		? QString::fromUtf8(kDynamicClientId)
		: params.clientId;
	auto pairs = std::vector<std::pair<QString, QString>>{
		{ u"response_type"_q, u"code"_q },
		{ u"client_id"_q, clientId },
	};
	if (clientId == QString::fromUtf8(kDynamicClientId)) {
		pairs.push_back({ u"agent_name_hint"_q, AgentName() });
	}
	pairs.push_back({ u"ext_agent_host_id"_q, params.hostId });
	pairs.push_back({ u"redirect_uri"_q, params.redirectUri });
	pairs.push_back({ u"scope"_q, QString::fromUtf8(kScope) });
	pairs.push_back({ u"resource"_q, QString::fromUtf8(kResource) });
	pairs.push_back({ u"state"_q, QString::fromUtf8(params.state) });
	pairs.push_back({ u"nonce"_q, QString::fromUtf8(params.nonce) });
	pairs.push_back({ u"code_challenge"_q, QString::fromUtf8(params.challenge) });
	pairs.push_back({ u"code_challenge_method"_q, u"S256"_q });
	if (!params.loginHint.isEmpty()) {
		pairs.push_back({ u"login_hint"_q, params.loginHint });
	}
	return QString::fromUtf8(kAuthorizeUrl)
		+ u"?"_q
		+ QString::fromUtf8(FormBody(pairs));
}

CallbackResult ParseCallback(
		const QUrl &url,
		const QByteArray &expectedState,
		const QString &pendingClientId) {
	auto result = CallbackResult();
	const auto query = QUrlQuery(url);
	const auto state = query.queryItemValue(u"state"_q);
	if (url.path() != CallbackPath()) {
		result.error = u"not_found"_q;
		return result;
	} else if (state.isEmpty() || state.toUtf8() != expectedState) {
		result.error = u"state_mismatch"_q;
		return result;
	}
	const auto error = query.queryItemValue(u"error"_q);
	if (!error.isEmpty()) {
		result.error = error;
		return result;
	}
	const auto code = query.queryItemValue(u"code"_q);
	if (code.isEmpty()) {
		result.error = u"missing_code"_q;
		return result;
	}
	result.code = code;
	result.clientId = query.queryItemValue(u"client_id"_q);
	if (result.clientId.isEmpty()) {
		result.clientId = pendingClientId;
	}
	if (result.clientId.isEmpty()) {
		result.error = u"missing_client_id"_q;
	}
	return result;
}

CallbackResult ParsePastedUrl(
		const QString &text,
		const QString &redirectUri,
		const QByteArray &expectedState,
		const QString &pendingClientId) {
	auto result = CallbackResult();
	const auto url = QUrl(text.trimmed());
	const auto expected = QUrl(redirectUri);
	if (url.scheme() != expected.scheme()
		|| url.host() != expected.host()
		|| url.path() != expected.path()) {
		result.error = u"wrong_url"_q;
		return result;
	} else if (url.port() != expected.port()
		&& url.host() != u"127.0.0.1"_q) {
		result.error = u"wrong_url"_q;
		return result;
	}
	return ParseCallback(url, expectedState, pendingClientId);
}

HttpRequest CodeRequest(
		const QString &clientId,
		const QString &code,
		const QByteArray &verifier,
		const QString &redirectUri) {
	return TokenRequest({
		{ u"grant_type"_q, u"authorization_code"_q },
		{ u"client_id"_q, clientId },
		{ u"code"_q, code },
		{ u"code_verifier"_q, QString::fromUtf8(verifier) },
		{ u"redirect_uri"_q, redirectUri },
		{ u"resource"_q, QString::fromUtf8(kResource) },
	});
}

HttpRequest RefreshRequest(
		const QString &clientId,
		const QString &refreshToken) {
	return TokenRequest({
		{ u"grant_type"_q, u"refresh_token"_q },
		{ u"client_id"_q, clientId },
		{ u"refresh_token"_q, refreshToken },
		{ u"resource"_q, QString::fromUtf8(kResource) },
	});
}

HttpRequest DiscoveryRequest() {
	auto request = HttpRequest();
	request.url = QString::fromUtf8(kDiscoveryUrl);
	request.post = false;
	return request;
}

QString ParseRevocationEndpoint(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return QString();
	}
	const auto endpoint = document.object().value(
		u"revocation_endpoint"_q).toString();
	if (endpoint.isEmpty() || !endpoint.startsWith(u"https://"_q)) {
		return QString();
	}
	return endpoint;
}

HttpRequest RevokeRequest(
		const QString &endpoint,
		const QString &clientId,
		const QString &refreshToken) {
	auto request = HttpRequest();
	request.url = endpoint;
	request.post = true;
	request.headers = {
		{ "content-type", "application/x-www-form-urlencoded" },
		{ "accept", "application/json" },
	};
	request.body = FormBody({
		{ u"token"_q, refreshToken },
		{ u"token_type_hint"_q, u"refresh_token"_q },
		{ u"client_id"_q, clientId },
	});
	return request;
}

TokenResult ParseTokenResponse(int status, const QByteArray &body) {
	auto result = TokenResult();
	if (status != 200) {
		const auto object = QJsonDocument::fromJson(body).object();
		const auto error = object.value(u"error"_q);
		auto errorCode = error.toString();
		auto errorText = object.value(u"error_description"_q).toString();
		if (error.isObject()) {
			const auto data = error.toObject();
			errorCode = data.value(u"code"_q).toString();
			if (errorCode.isEmpty()) {
				errorCode = data.value(u"type"_q).toString();
			}
			errorText = data.value(u"message"_q).toString();
		}
		if (!errorCode.isEmpty()) {
			result.errorCode = errorCode;
			result.errorText = errorText;
			result.terminal = IsTerminalError(result.errorCode);
		} else {
			result.errorCode = u"http_%1"_q.arg(status);
			result.errorText = QString::fromUtf8(body.left(200));
		}
		return result;
	}
	const auto document = QJsonDocument::fromJson(body);
	if (!document.isObject()) {
		result.errorCode = u"bad_response"_q;
		return result;
	}
	const auto object = document.object();
	const auto accessToken = object.value(u"access_token"_q).toString();
	const auto refreshToken = object.value(u"refresh_token"_q).toString();
	const auto scope = object.value(u"scope"_q).toString();
	const auto expiresIn = object.value(u"expires_in"_q).toInt();
	if (accessToken.isEmpty()
		|| refreshToken.isEmpty()
		|| scope.isEmpty()
		|| expiresIn <= 0) {
		result.errorCode = u"bad_response"_q;
		return result;
	}
	result.accessToken = accessToken;
	result.refreshToken = refreshToken;
	result.scopes = scope;
	result.expiresIn = expiresIn;
	result.idToken = object.value(u"id_token"_q).toString();
	return result;
}

std::optional<IdClaims> ParseIdToken(const QString &token) {
	const auto parts = token.split(QChar('.'));
	if (parts.size() != 3) {
		return std::nullopt;
	}
	const auto json = QByteArray::fromBase64(
		parts[1].toUtf8(),
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return std::nullopt;
	}
	auto claims = IdClaims();
	const auto object = document.object();
	claims.subject = object.value(u"sub"_q).toString();
	claims.email = object.value(u"email"_q).toString();
	claims.issuer = object.value(u"iss"_q).toString();
	const auto aud = object.value(u"aud"_q);
	if (aud.isArray()) {
		const auto array = aud.toArray();
		claims.audience = array.isEmpty()
			? QString()
			: array.first().toString();
	} else {
		claims.audience = aud.toString();
	}
	claims.nonce = object.value(u"nonce"_q).toString();
	claims.expires = object.value(u"exp"_q).toInteger();
	const auto auth = object.value(u"https://api.openai.com/auth"_q);
	if (auth.isObject()) {
		claims.plan = auth.toObject().value(u"chatgpt_plan_type"_q).toString();
	}
	return claims;
}

bool HasPlanScope(const QString &scopes) {
	return scopes.split(QChar(' '), Qt::SkipEmptyParts).contains(
		QString::fromUtf8(kPlanScope));
}

bool SignedIn(const ChatGptAccount &account) {
	return !account.refreshToken.isEmpty();
}

bool NeedsRefresh(const ChatGptAccount &account, qint64 nowMs) {
	return account.accessToken.isEmpty()
		|| (account.expiresAt - kRefreshMarginMs <= nowMs);
}

void ApplyTokens(
		ChatGptAccount &account,
		const TokenResult &tokens,
		qint64 nowMs) {
	account.accessToken = tokens.accessToken;
	if (!tokens.refreshToken.isEmpty()) {
		account.refreshToken = tokens.refreshToken;
	}
	account.scopes = tokens.scopes;
	account.expiresAt = nowMs + tokens.expiresIn * 1000;
}

void ClearTokens(ChatGptAccount &account) {
	account.accessToken = QString();
	account.refreshToken = QString();
	account.expiresAt = 0;
	account.scopes = QString();
}

bool RefreshStillCurrent(
		const ChatGptAccount &account,
		const QString &usedRefreshToken) {
	return !usedRefreshToken.isEmpty()
		&& (account.refreshToken == usedRefreshToken);
}

QByteArray SignedOutBody(const QString &text) {
	return QJsonDocument(QJsonObject{
		{ u"error"_q, QJsonObject{
			{ u"code"_q, u"chatgpt_signed_out"_q },
			{ u"message"_q, text },
		} },
	}).toJson(QJsonDocument::Compact);
}

} // namespace Agent::ChatGpt
