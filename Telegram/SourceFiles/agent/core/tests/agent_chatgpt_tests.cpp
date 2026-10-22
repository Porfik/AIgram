/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_chatgpt_auth.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace Agent::Tests {
namespace {

using namespace Agent::ChatGpt;

constexpr auto kPkceLength = 43;

[[nodiscard]] QString B64Url(const QByteArray &bytes) {
	return QString::fromUtf8(bytes.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

[[nodiscard]] QString UrlParam(const QUrl &url, const QString &key) {
	return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
}

[[nodiscard]] std::vector<std::pair<QString, QString>> DecodeForm(
		const QByteArray &body) {
	auto result = std::vector<std::pair<QString, QString>>();
	for (const auto &part : QString::fromUtf8(body).split(u'&')) {
		const auto pair = part.split(u'=');
		result.emplace_back(
			QUrl::fromPercentEncoding(pair.value(0).toUtf8()),
			QUrl::fromPercentEncoding(pair.value(1).toUtf8()));
	}
	return result;
}

[[nodiscard]] QString MakeJwt(const QJsonObject &payload) {
	const auto header = QJsonDocument(QJsonObject{
		{ u"alg"_q, u"RS256"_q },
		{ u"kid"_q, u"test"_q },
	}).toJson(QJsonDocument::Compact);
	const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	return u"%1.%2.%3"_q.arg(
		B64Url(header),
		B64Url(body),
		B64Url(QByteArray("signature")));
}

[[nodiscard]] AuthorizeParams TestParams(const QString &clientId = QString(),
		const QString &loginHint = QString()) {
	const auto pkce = GeneratePkce();
	return {
		.clientId = clientId,
		.hostId = GenerateHostId(),
		.redirectUri = RedirectUri(),
		.state = GenerateState(),
		.nonce = GenerateState(),
		.challenge = pkce.challenge,
		.loginHint = loginHint,
	};
}

} // namespace

std::vector<Check> ChatGptChecks() {
	return {
		{ u"chatgpt_pkce"_q, [] {
			const auto pkce = GeneratePkce();
			if (pkce.verifier.size() != kPkceLength) {
				return u"verifier must be 43 characters"_q;
			}
			const auto re = QRegularExpression(u"^[A-Za-z0-9_-]+$"_q);
			if (!re.match(QString::fromUtf8(pkce.verifier)).hasMatch()) {
				return u"verifier must be base64url only"_q;
			}
			const auto expected = B64Url(QCryptographicHash::hash(
				pkce.verifier,
				QCryptographicHash::Sha256));
			if (pkce.challenge != expected) {
				return u"challenge must be base64url SHA-256"_q;
			}
			const auto knownVerifier = QByteArray(
				"dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
			const auto knownChallenge = QByteArray(
				"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
			const auto hash = QCryptographicHash::hash(
				knownVerifier,
				QCryptographicHash::Sha256);
			if (B64Url(hash) != knownChallenge) {
				return u"known pkce pair must match"_q;
			}
			return QString();
		} },
		{ u"chatgpt_host_id"_q, [] {
			const auto id = GenerateHostId();
			if (!id.startsWith(u"urn:uuid:"_q)) {
				return u"host id must start with urn:uuid:"_q;
			}
			const auto uuid = id.mid(9);
			if (uuid.size() != 36
				|| uuid[8] != u'-'
				|| uuid[13] != u'-'
				|| uuid[18] != u'-'
				|| uuid[23] != u'-') {
				return u"host id must be a uuid"_q;
			}
			if (uuid[14] != u'4') {
				return u"host id must be a version 4 uuid"_q;
			}
			const auto re = QRegularExpression(u"^[0-9a-f-]+$"_q);
			if (!re.match(uuid).hasMatch()) {
				return u"host id must be lower case hex"_q;
			}
			return QString();
		} },
		{ u"chatgpt_authorize_url_dynamic"_q, [] {
			const auto params = TestParams();
			const auto text = AuthorizeUrl(params);
			const auto url = QUrl(text);
			if (url.host() != u"auth.openai.com"_q
				|| url.path() != u"/api/accounts/authorize"_q) {
				return u"authorize url must be auth.openai.com"_q;
			}
			if (UrlParam(url, u"client_id"_q) != u"dynamic_agent_client"_q) {
				return u"dynamic client id must be used"_q;
			}
			if (UrlParam(url, u"agent_name_hint"_q) != AgentName()) {
				return u"agent name hint must be present"_q;
			}
			if (UrlParam(url, u"login_hint"_q) != QString()) {
				return u"login hint must be empty when unset"_q;
			}
			if (UrlParam(url, u"code_challenge"_q)
				!= QString::fromUtf8(params.challenge)) {
				return u"code challenge must match"_q;
			}
			if (UrlParam(url, u"code_challenge_method"_q) != u"S256"_q) {
				return u"code challenge method must be S256"_q;
			}
			return QString();
		} },
		{ u"chatgpt_authorize_url_saved_client"_q, [] {
			const auto params = TestParams(u"saved-client-id"_q,
				u"test@example.com"_q);
			const auto url = QUrl(AuthorizeUrl(params));
			if (UrlParam(url, u"client_id"_q) != params.clientId) {
				return u"saved client id must be used"_q;
			}
			if (!UrlParam(url, u"agent_name_hint"_q).isEmpty()) {
				return u"agent name hint must be absent"_q;
			}
			if (UrlParam(url, u"login_hint"_q) != params.loginHint) {
				return u"login hint must match when set"_q;
			}
			return QString();
		} },
		{ u"chatgpt_authorize_url_encoding"_q, [] {
			const auto text = AuthorizeUrl(TestParams());
			if (!text.contains(u"redirect_uri=http%3A%2F%2F127.0.0.1%3A1455%2Fauth%2Fcallback"_q)) {
				return u"redirect_uri must be percent-encoded"_q;
			}
			if (!text.contains(u"scope=openid%20profile"_q)) {
				return u"scope spaces must be percent-encoded"_q;
			}
			if (!text.contains(u"resource=https%3A%2F%2Fapi.openai.com%2Fv1"_q)) {
				return u"resource must be percent-encoded"_q;
			}
			const auto url = QUrl(text);
			if (UrlParam(url, u"redirect_uri"_q) != RedirectUri()) {
				return u"redirect_uri must parse back"_q;
			} else if (UrlParam(url, u"resource"_q)
					!= u"https://api.openai.com/v1"_q) {
				return u"resource must parse back"_q;
			} else if (!UrlParam(url, u"scope"_q).contains(
					u"chatgpt.tokens.use.direct"_q)) {
				return u"scope must parse back"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_success"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/auth/callback"_q);
			auto query = QUrlQuery();
			query.addQueryItem(u"code"_q, u"the-code"_q);
			query.addQueryItem(u"client_id"_q, u"cid"_q);
			query.addQueryItem(u"state"_q, QString::fromUtf8(
				GenerateState()));
			url.setQuery(query);
			const auto state = QUrlQuery(url).queryItemValue(u"state"_q);
			const auto result = ParseCallback(url, state.toUtf8(), u"p"_q);
			if (!result.error.isEmpty()
				|| result.code != u"the-code"_q
				|| result.clientId != u"cid"_q) {
				return u"a good callback must parse"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_pending_client"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/auth/callback"_q);
			auto query = QUrlQuery();
			query.addQueryItem(u"code"_q, u"c"_q);
			const auto state = GenerateState();
			query.addQueryItem(u"state"_q, QString::fromUtf8(state));
			url.setQuery(query);
			const auto result = ParseCallback(url, state, u"pending"_q);
			if (!result.error.isEmpty()
				|| result.code != u"c"_q
				|| result.clientId != u"pending"_q) {
				return u"the pending client id must be kept"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_denied"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/auth/callback"_q);
			auto query = QUrlQuery();
			const auto state = GenerateState();
			query.addQueryItem(u"state"_q, QString::fromUtf8(state));
			query.addQueryItem(u"error"_q, u"access_denied"_q);
			url.setQuery(query);
			const auto result = ParseCallback(url, state, u"p"_q);
			if (result.error != u"access_denied"_q) {
				return u"access_denied must be reported"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_state_mismatch"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/auth/callback"_q);
			auto query = QUrlQuery();
			query.addQueryItem(u"state"_q, u"other"_q);
			query.addQueryItem(u"code"_q, u"c"_q);
			url.setQuery(query);
			const auto result = ParseCallback(
				url,
				GenerateState(),
				u"p"_q);
			if (result.error != u"state_mismatch"_q) {
				return u"a state mismatch must be reported"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_missing_code"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/auth/callback"_q);
			auto query = QUrlQuery();
			const auto state = GenerateState();
			query.addQueryItem(u"state"_q, QString::fromUtf8(state));
			url.setQuery(query);
			const auto result = ParseCallback(url, state, u"p"_q);
			if (result.error != u"missing_code"_q) {
				return u"a missing code must be reported"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_wrong_path"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/other"_q);
			auto query = QUrlQuery();
			const auto state = GenerateState();
			query.addQueryItem(u"state"_q, QString::fromUtf8(state));
			query.addQueryItem(u"code"_q, u"c"_q);
			url.setQuery(query);
			const auto result = ParseCallback(url, state, u"p"_q);
			if (result.error != u"not_found"_q) {
				return u"a wrong path must be not_found"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_callback_missing_client_id"_q, [] {
			auto url = QUrl(u"http://127.0.0.1:1455/auth/callback"_q);
			auto query = QUrlQuery();
			const auto state = GenerateState();
			query.addQueryItem(u"state"_q, QString::fromUtf8(state));
			query.addQueryItem(u"code"_q, u"c"_q);
			url.setQuery(query);
			const auto result = ParseCallback(url, state, QString());
			if (result.error != u"missing_client_id"_q) {
				return u"a missing client id must be reported"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_pasted_good"_q, [] {
			const auto state = GenerateState();
			const auto result = ParsePastedUrl(
				u"  http://127.0.0.1:1455/auth/callback?code=c&state=%1  "_q.arg(
					QString::fromUtf8(state)),
				RedirectUri(),
				state,
				u"p"_q);
			if (!result.error.isEmpty()
				|| result.code != u"c"_q
				|| result.clientId != u"p"_q) {
				return u"a good pasted url must parse"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_pasted_other_port"_q, [] {
			const auto state = GenerateState();
			const auto result = ParsePastedUrl(
				u"http://127.0.0.1:9999/auth/callback?code=c&state=%1"_q.arg(
					QString::fromUtf8(state)),
				RedirectUri(),
				state,
				u"p"_q);
			if (!result.error.isEmpty() || result.code != u"c"_q) {
				return u"any port on 127.0.0.1 must be accepted"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_pasted_localhost"_q, [] {
			const auto state = GenerateState();
			const auto result = ParsePastedUrl(
				u"http://localhost:1455/auth/callback?code=c&state=%1"_q.arg(
					QString::fromUtf8(state)),
				RedirectUri(),
				state,
				u"p"_q);
			if (result.error != u"wrong_url"_q) {
				return u"localhost must be refused"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_pasted_garbage"_q, [] {
			const auto result = ParsePastedUrl(
				u"not a url at all"_q,
				RedirectUri(),
				GenerateState(),
				u"p"_q);
			if (result.error != u"wrong_url"_q) {
				return u"garbage must be refused"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_pasted_wrong_path"_q, [] {
			const auto state = GenerateState();
			const auto result = ParsePastedUrl(
				u"http://127.0.0.1:1455/other?code=c&state=%1"_q.arg(
					QString::fromUtf8(state)),
				RedirectUri(),
				state,
				u"p"_q);
			if (result.error != u"wrong_url"_q) {
				return u"a wrong path must be refused"_q;
			}
			return QString();
		} },
		{ u"chatgpt_code_request_body"_q, [] {
			const auto pkce = GeneratePkce();
			const auto request = CodeRequest(
				u"cid"_q,
				u"the code"_q,
				pkce.verifier,
				RedirectUri());
			if (!request.post) {
				return u"the token request must be a post"_q;
			} else if (request.url
					!= u"https://auth.openai.com/api/accounts/oauth/token"_q) {
				return u"wrong token endpoint"_q;
			}
			const auto form = DecodeForm(request.body);
			const auto want = std::vector<std::pair<QString, QString>>{
				{ u"grant_type"_q, u"authorization_code"_q },
				{ u"client_id"_q, u"cid"_q },
				{ u"code"_q, u"the code"_q },
				{ u"code_verifier"_q, QString::fromUtf8(pkce.verifier) },
				{ u"redirect_uri"_q, RedirectUri() },
				{ u"resource"_q, u"https://api.openai.com/v1"_q },
			};
			if (form != want) {
				return u"the code form must match in order"_q;
			}
			return QString();
		} },
		{ u"chatgpt_refresh_request_body"_q, [] {
			const auto request = RefreshRequest(u"cid"_q, u"rt"_q);
			const auto form = DecodeForm(request.body);
			const auto want = std::vector<std::pair<QString, QString>>{
				{ u"grant_type"_q, u"refresh_token"_q },
				{ u"client_id"_q, u"cid"_q },
				{ u"refresh_token"_q, u"rt"_q },
				{ u"resource"_q, u"https://api.openai.com/v1"_q },
			};
			if (form != want) {
				return u"the refresh form must match in order"_q;
			}
			for (const auto &[key, value] : form) {
				if (key == u"scope"_q) {
					return u"the refresh request must have no scope"_q;
				}
			}
			return QString();
		} },
		{ u"chatgpt_revoke_request_body"_q, [] {
			const auto request = RevokeRequest(
				u"https://auth.openai.com/v1/oauth/revocation"_q,
				u"cid"_q,
				u"rt"_q);
			if (!request.post) {
				return u"the revoke request must be a post"_q;
			}
			const auto form = DecodeForm(request.body);
			const auto want = std::vector<std::pair<QString, QString>>{
				{ u"token"_q, u"rt"_q },
				{ u"token_type_hint"_q, u"refresh_token"_q },
				{ u"client_id"_q, u"cid"_q },
			};
			if (form != want) {
				return u"the revoke form must match in order"_q;
			}
			return QString();
		} },
		{ u"chatgpt_discovery_request"_q, [] {
			const auto request = DiscoveryRequest();
			if (request.post
				|| request.url != u"https://auth.openai.com/.well-known/openid-configuration"_q) {
				return u"discovery must be a get of the configuration"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_revocation_endpoint"_q, [] {
			const auto good = QJsonDocument(QJsonObject{
				{ u"revocation_endpoint"_q,
					u"https://auth.openai.com/v1/oauth/revocation"_q },
			}).toJson(QJsonDocument::Compact);
			if (ParseRevocationEndpoint(good)
					!= u"https://auth.openai.com/v1/oauth/revocation"_q) {
				return u"the good endpoint must parse"_q;
			}
			const auto missing = QJsonDocument(QJsonObject{
				{ u"issuer"_q, u"https://auth.openai.com"_q },
			}).toJson(QJsonDocument::Compact);
			if (!ParseRevocationEndpoint(missing).isEmpty()) {
				return u"a missing endpoint must be empty"_q;
			}
			const auto http = QJsonDocument(QJsonObject{
				{ u"revocation_endpoint"_q,
					u"http://auth.openai.com/v1/oauth/revocation"_q },
			}).toJson(QJsonDocument::Compact);
			if (!ParseRevocationEndpoint(http).isEmpty()) {
				return u"an http endpoint must be empty"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_token_response_success"_q, [] {
			const auto body = QJsonDocument(QJsonObject{
				{ u"access_token"_q, u"at"_q },
				{ u"refresh_token"_q, u"rt"_q },
				{ u"id_token"_q, u"it"_q },
				{ u"scope"_q, u"openid"_q },
				{ u"expires_in"_q, 3600 },
			}).toJson(QJsonDocument::Compact);
			const auto result = ParseTokenResponse(200, body);
			if (!result.errorCode.isEmpty()
				|| result.accessToken != u"at"_q
				|| result.refreshToken != u"rt"_q
				|| result.idToken != u"it"_q
				|| result.scopes != u"openid"_q
				|| result.expiresIn != 3600) {
				return u"a good token response must parse"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_token_response_bad"_q, [] {
			const auto full = QJsonObject{
				{ u"access_token"_q, u"at"_q },
				{ u"refresh_token"_q, u"rt"_q },
				{ u"scope"_q, u"openid"_q },
				{ u"expires_in"_q, 3600 },
			};
			for (const auto &key : std::vector<QString>{
					u"access_token"_q,
					u"refresh_token"_q,
					u"scope"_q,
					u"expires_in"_q }) {
				auto object = full;
				object.remove(key);
				const auto result = ParseTokenResponse(
					200,
					QJsonDocument(object).toJson(QJsonDocument::Compact));
				if (result.errorCode != u"bad_response"_q) {
					return u"missing "_q + key + u" must be bad_response"_q;
				}
			}
			const auto garbage = ParseTokenResponse(200, QByteArray("not json"));
			if (garbage.errorCode != u"bad_response"_q) {
				return u"garbage must be bad_response"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_token_response_terminal"_q, [] {
			for (const auto &code : std::vector<QString>{
					u"invalid_grant"_q,
					u"invalid_refresh_token"_q,
					u"token_expired"_q,
					u"refresh_token_expired"_q,
					u"refresh_token_invalidated"_q,
					u"refresh_token_reused"_q,
					u"invalid_client"_q }) {
				const auto body = QJsonDocument(QJsonObject{
					{ u"error"_q, code },
					{ u"error_description"_q, u"why"_q },
				}).toJson(QJsonDocument::Compact);
				const auto result = ParseTokenResponse(400, body);
				if (result.errorCode != code
					|| result.errorText != u"why"_q
					|| !result.terminal) {
					return u""_q + code + u" must be terminal"_q;
				}
			}
			return QString();
		} },
		{ u"chatgpt_parse_token_response_non_terminal"_q, [] {
			const auto body = QJsonDocument(QJsonObject{
				{ u"error"_q, u"server_error"_q },
			}).toJson(QJsonDocument::Compact);
			const auto result = ParseTokenResponse(500, body);
			if (result.errorCode != u"server_error"_q || result.terminal) {
				return u"a 5xx error must not be terminal"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_token_response_plain_text"_q, [] {
			const auto result = ParseTokenResponse(
				500,
				QByteArray("<html>oops</html>"));
			if (result.errorCode != u"http_500"_q
				|| result.errorText != u"<html>oops</html>"_q
				|| result.terminal) {
				return u"a plain text error must give http_<status>"_q;
			}
			const auto long_ = ParseTokenResponse(
				503,
				QByteArray(500, 'x'));
			if (long_.errorCode != u"http_503"_q
				|| long_.errorText.size() != 200
				|| long_.terminal) {
				return u"a long body must be cut to 200 bytes"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_token_response_error_object"_q, [] {
			const auto make = [](const QString &code) {
				return QJsonDocument(QJsonObject{
					{ u"error"_q, QJsonObject{
						{ u"code"_q, code },
						{ u"message"_q, u"why"_q },
					} },
				}).toJson(QJsonDocument::Compact);
			};
			const auto terminal = ParseTokenResponse(
				400,
				make(u"refresh_token_reused"_q));
			if (terminal.errorCode != u"refresh_token_reused"
				|| terminal.errorText != u"why"
				|| !terminal.terminal) {
				return u"the object error code must be terminal"_q;
			}
			const auto plain = ParseTokenResponse(
				503,
				make(u"temporarily_unavailable"_q));
			if (plain.errorCode != u"temporarily_unavailable"
				|| plain.errorText != u"why"
				|| plain.terminal) {
				return u"a non-terminal object error must not be terminal"_q;
			}
			const auto type = QJsonDocument(QJsonObject{
				{ u"error"_q, QJsonObject{
					{ u"type"_q, u"invalid_grant"_q },
					{ u"message"_q, u"why"_q },
				} },
			}).toJson(QJsonDocument::Compact);
			const auto byType = ParseTokenResponse(400, type);
			if (byType.errorCode != u"invalid_grant"
				|| !byType.terminal) {
				return u"the error type must be the fallback"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_id_token"_q, [] {
			const auto token = MakeJwt(QJsonObject{
				{ u"sub"_q, u"subject"_q },
				{ u"email"_q, u"me@example.com"_q },
				{ u"iss"_q, u"https://auth.openai.com"_q },
				{ u"aud"_q, QJsonArray{ u"client-a"_q, u"client-b"_q } },
				{ u"nonce"_q, u"n"_q },
				{ u"exp"_q, 1234567890 },
				{ u"https://api.openai.com/auth"_q, QJsonObject{
					{ u"chatgpt_plan_type"_q, u"plus"_q },
				} },
			});
			const auto claims = ParseIdToken(token);
			if (!claims) {
				return u"a good id token must parse"_q;
			}
			if (claims->subject != u"subject"_q
				|| claims->email != u"me@example.com"_q
				|| claims->issuer != u"https://auth.openai.com"_q
				|| claims->audience != u"client-a"_q
				|| claims->nonce != u"n"_q
				|| claims->expires != 1234567890
				|| claims->plan != u"plus"_q) {
				return u"the claims must match"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_id_token_garbage"_q, [] {
			if (ParseIdToken(u"only.two.parts.extra"_q)) {
				return u"four parts must fail"_q;
			}
			if (ParseIdToken(u"a.b"_q)) {
				return u"two parts must fail"_q;
			}
			const auto badJson = u""_q + B64Url(QByteArray("not json"))
				+ u".x.y"_q;
			if (ParseIdToken(badJson)) {
				return u"a bad payload must fail"_q;
			}
			return QString();
		} },
		{ u"chatgpt_parse_id_token_empty_aud"_q, [] {
			const auto token = MakeJwt(QJsonObject{
				{ u"sub"_q, u"s"_q },
				{ u"aud"_q, QJsonArray{} },
			});
			const auto claims = ParseIdToken(token);
			if (!claims) {
				return u"an empty aud array must not fail"_q;
			} else if (!claims->audience.isEmpty()) {
				return u"an empty aud array must give no audience"_q;
			}
			return QString();
		} },
		{ u"chatgpt_has_plan_scope"_q, [] {
			if (!HasPlanScope(u"openid chatgpt.tokens.use.direct"_q)) {
				return u"the plan scope must be found"_q;
			} else if (HasPlanScope(u"openid profile"_q)) {
				return u"other scopes must not match"_q;
			} else if (HasPlanScope(u"chatgpt.tokens.use.direct2"_q)) {
				return u"a prefix must not match"_q;
			} else if (HasPlanScope(QString())) {
				return u"empty scopes must not match"_q;
			}
			return QString();
		} },
		{ u"chatgpt_needs_refresh"_q, [] {
			auto account = ChatGptAccount();
			account.refreshToken = u"rt"_q;
			account.accessToken = u"at"_q;
			account.expiresAt = 100000;
			if (NeedsRefresh(account, 100000 - kRefreshMarginMs - 1)) {
				return u"before the margin there must be no refresh"_q;
			} else if (!NeedsRefresh(account, 100000 - kRefreshMarginMs)) {
				return u"at the margin there must be a refresh"_q;
			}
			account.accessToken = QString();
			if (!NeedsRefresh(account, 0)) {
				return u"an empty access token must need a refresh"_q;
			}
			return QString();
		} },
		{ u"chatgpt_apply_tokens"_q, [] {
			auto account = ChatGptAccount();
			account.clientId = u"cid"_q;
			account.refreshToken = u"old-rt"_q;
			ApplyTokens(account,
				{ .accessToken = u"new-at"_q,
					.scopes = u"openid"_q,
					.expiresIn = 10 },
				1000);
			if (account.accessToken != u"new-at"_q
				|| account.refreshToken != u"old-rt"_q
				|| account.scopes != u"openid"_q
				|| account.expiresAt != 11000) {
				return u"an answer without a refresh token must keep the old"_q;
			}
			ApplyTokens(account,
				{ .accessToken = u"at2"_q,
					.refreshToken = u"new-rt"_q,
					.scopes = u"s"_q,
					.expiresIn = 5 },
				2000);
			if (account.refreshToken != u"new-rt"_q
				|| account.expiresAt != 7000) {
				return u"an answer with a refresh token must replace it"_q;
			}
			return QString();
		} },
		{ u"chatgpt_plan_api_url"_q, [] {
			const auto good = {
				u"https://api.openai.com/v1/responses"_q,
				u"https://API.openai.com/v1/models"_q,
				u"https://api.openai.com:443/v1/models"_q,
			};
			const auto bad = {
				u"http://api.openai.com/v1/responses"_q,
				u"https://api.openai.com.evil.example/v1"_q,
				u"https://evil.example/api.openai.com/v1"_q,
				u"https://api.openai.com:8443/v1"_q,
				u"https://user@api.openai.com/v1"_q,
				u"https://openrouter.ai/api/v1"_q,
				QString(),
			};
			for (const auto &url : good) {
				if (!IsPlanApiUrl(url)) {
					return u"must take "_q + url;
				}
			}
			for (const auto &url : bad) {
				if (IsPlanApiUrl(url)) {
					return u"must refuse "_q + url;
				}
			}
			return QString();
		} },
		{ u"chatgpt_refresh_still_current"_q, [] {
			auto account = ChatGptAccount();
			account.refreshToken = u"rt1"_q;
			if (!RefreshStillCurrent(account, u"rt1"_q)) {
				return u"the same login must take the answer"_q;
			} else if (RefreshStillCurrent(account, u"rt0"_q)) {
				return u"a new login must not take an old answer"_q;
			} else if (RefreshStillCurrent(account, QString())) {
				return u"a refresh without a token is never current"_q;
			}
			ClearTokens(account);
			if (RefreshStillCurrent(account, u"rt1"_q)) {
				return u"a signed out account must not take an answer"_q;
			}
			return QString();
		} },
		{ u"chatgpt_clear_tokens"_q, [] {
			auto account = ChatGptAccount();
			account.clientId = u"cid"_q;
			account.hostId = u"hid"_q;
			account.email = u"me@example.com"_q;
			account.subject = u"sub"_q;
			account.plan = u"plus"_q;
			account.accessToken = u"at"_q;
			account.refreshToken = u"rt"_q;
			account.expiresAt = 42;
			account.scopes = u"openid"_q;
			ClearTokens(account);
			if (!account.accessToken.isEmpty()
				|| !account.refreshToken.isEmpty()
				|| account.expiresAt != 0
				|| !account.scopes.isEmpty()) {
				return u"the tokens must be cleared"_q;
			} else if (account.clientId != u"cid"_q
				|| account.hostId != u"hid"_q
				|| account.email != u"me@example.com"_q
				|| account.subject != u"sub"_q
				|| account.plan != u"plus"_q) {
				return u"the identity must be kept"_q;
			}
			return QString();
		} },
		{ u"chatgpt_signed_out_body"_q, [] {
			const auto body = SignedOutBody(u"no tokens"_q);
			const auto document = QJsonDocument::fromJson(body);
			const auto error = document.object().value(u"error"_q).toObject();
			if (error.value(u"code"_q).toString() != u"chatgpt_signed_out"_q
				|| error.value(u"message"_q).toString() != u"no tokens"_q) {
				return u"the signed out body must match"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
