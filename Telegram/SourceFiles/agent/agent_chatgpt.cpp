/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_chatgpt.h"

#include "agent/agent_http_transport.h"
#include "agent/agent_settings.h"
#include "agent/core/agent_chatgpt_auth.h"
#include "base/unixtime.h"
#include "core/file_utilities.h"
#include "crl/crl_on_main.h"
#include "lang/lang_keys.h"

#include <rpl/event_stream.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

namespace Agent {
namespace {

constexpr auto kMaxRequestSize = 16 * 1024;

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

class Tokens final {
public:
	[[nodiscard]] static Tokens &Instance();

	[[nodiscard]] const ChatGptAccount &account();
	void set(ChatGptAccount account, bool notify);
	[[nodiscard]] rpl::producer<> changes() const;

	[[nodiscard]] uint64 request(
		const QString &failedToken,
		Fn<void(QString token, QString error)> done);
	void cancel(uint64 waiterId);

	[[nodiscard]] Transport &transport();

private:
	struct Waiter {
		uint64 id = 0;
		Fn<void(QString token, QString error)> done;
	};

	void startRefresh();
	void finishRefresh(
		const QString &used,
		int status,
		const QByteArray &body,
		QString error);
	void resolve(const QString &token, const QString &error);

	std::optional<ChatGptAccount> _account;
	std::vector<Waiter> _waiters;
	QPointer<HttpTransport> _transport;
	uint64 _nextWaiter = 0;
	bool _refreshing = false;
	rpl::event_stream<> _changes;

};

Tokens &Tokens::Instance() {
	static auto result = Tokens();
	return result;
}

const ChatGptAccount &Tokens::account() {
	if (!_account) {
		_account = LoadProviders().chatGpt;
	}
	return *_account;
}

void Tokens::set(ChatGptAccount account, bool notify) {
	_account = std::move(account);
	SaveChatGptAccount(*_account);
	if (notify) {
		_changes.fire({});
	}
}

rpl::producer<> Tokens::changes() const {
	return _changes.events();
}

Transport &Tokens::transport() {
	if (!_transport) {
		_transport = new HttpTransport();
		_transport->setParent(QCoreApplication::instance());
	}
	return *_transport;
}

uint64 Tokens::request(
		const QString &failedToken,
		Fn<void(QString token, QString error)> done) {
	const auto &current = account();
	if (!ChatGpt::SignedIn(current)) {
		done(QString(), tr::lng_agent_chatgpt_error_signed_out(tr::now));
		return 0;
	}
	const auto stale = ChatGpt::NeedsRefresh(current, NowMs())
		|| (!failedToken.isEmpty() && current.accessToken == failedToken);
	if (!stale && !_refreshing) {
		done(current.accessToken, QString());
		return 0;
	}
	const auto id = ++_nextWaiter;
	_waiters.push_back({ id, std::move(done) });
	if (!_refreshing) {
		startRefresh();
	}
	return id;
}

void Tokens::cancel(uint64 waiterId) {
	if (!waiterId) {
		return;
	}
	_waiters.erase(
		ranges::remove(_waiters, waiterId, &Waiter::id),
		end(_waiters));
}

void Tokens::startRefresh() {
	_refreshing = true;
	const auto &current = account();
	const auto used = current.refreshToken;
	const auto status = std::make_shared<int>(0);
	const auto body = std::make_shared<QByteArray>();
	(void)transport().start(
		ChatGpt::RefreshRequest(current.clientId, current.refreshToken),
		TransportHandlers{
			[=](HttpHead head) {
				*status = head.status;
			},
			[=](QByteArray chunk) {
				*body += chunk;
			},
			[=](QString error) {
				finishRefresh(used, *status, *body, std::move(error));
			},
		});
}

void Tokens::finishRefresh(
		const QString &used,
		int status,
		const QByteArray &body,
		QString error) {
	_refreshing = false;
	if (!ChatGpt::RefreshStillCurrent(account(), used)) {
		LOG(("Agent Info: dropped a ChatGPT token refresh of an old login."));
		const auto &current = account();
		if (!ChatGpt::SignedIn(current)) {
			resolve(
				QString(),
				tr::lng_agent_chatgpt_error_signed_out(tr::now));
		} else if (ChatGpt::NeedsRefresh(current, NowMs())) {
			startRefresh();
		} else {
			resolve(current.accessToken, QString());
		}
		return;
	} else if (!error.isEmpty()) {
		resolve(QString(), error);
		return;
	}
	const auto tokens = ChatGpt::ParseTokenResponse(status, body);
	auto updated = account();
	if (tokens.errorCode.isEmpty()) {
		ChatGpt::ApplyTokens(updated, tokens, NowMs());
		set(updated, false);
		resolve(updated.accessToken, QString());
		return;
	}
	LOG(("Agent Error: ChatGPT token refresh failed, %1 (%2)."
		).arg(tokens.errorCode
		).arg(status));
	if (tokens.terminal) {
		ChatGpt::ClearTokens(updated);
		set(updated, true);
		resolve(QString(), tr::lng_agent_chatgpt_error_signed_out(tr::now));
	} else {
		resolve(QString(), tokens.errorText.isEmpty()
			? tokens.errorCode
			: tokens.errorText);
	}
}

void Tokens::resolve(const QString &token, const QString &error) {
	auto waiters = base::take(_waiters);
	for (auto &waiter : waiters) {
		waiter.done(token, error);
	}
}

[[nodiscard]] QByteArray CallbackPage(const QString &text) {
	const auto escaped = text.toHtmlEscaped().toUtf8();
	return "<!doctype html><html><head><meta charset=\"utf-8\">"
		"<title>AIgram</title></head>"
		"<body style=\"font-family: sans-serif; margin: 48px;\">"
		"<p>" + escaped + "</p></body></html>";
}

void Reply(
		not_null<QTcpSocket*> socket,
		int status,
		const QByteArray &body) {
	const auto reason = (status == 200)
		? QByteArray("OK")
		: (status == 404)
		? QByteArray("Not Found")
		: QByteArray("Bad Request");
	socket->write("HTTP/1.1 "
		+ QByteArray::number(status) + ' ' + reason + "\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		"\r\n" + body);
	socket->disconnectFromHost();
}

} // namespace

class ChatGptLogin::Flow final : public QObject {
public:
	explicit Flow(Fn<void(QString error)> done);
	~Flow();

	[[nodiscard]] bool listening() const;
	[[nodiscard]] QString authorizeUrl() const;
	void openBrowser();
	[[nodiscard]] QString paste(const QString &text);

private:
	void accept();
	void read(not_null<QTcpSocket*> socket);
	void handle(not_null<QTcpSocket*> socket, const QByteArray &target);
	void exchange(const QString &code, const QString &clientId);
	void exchanged(
		const QString &clientId,
		int status,
		const QByteArray &body,
		const QString &error);
	void fail(QString error);
	void finish(QString error);

	Fn<void(QString error)> _done;
	QTcpServer _server;
	ChatGpt::Pkce _pkce;
	QByteArray _state;
	QByteArray _nonce;
	QString _clientId;
	QString _hostId;
	QString _email;
	QString _redirectUri;
	QString _url;
	uint64 _exchangeId = 0;
	bool _exchanging = false;
	bool _finished = false;

};

ChatGptLogin::Flow::Flow(Fn<void(QString error)> done)
: _done(std::move(done))
, _pkce(ChatGpt::GeneratePkce())
, _state(ChatGpt::GenerateState())
, _nonce(ChatGpt::GenerateState()) {
	const auto &account = Tokens::Instance().account();
	_clientId = account.clientId;
	_hostId = account.hostId.isEmpty()
		? ChatGpt::GenerateHostId()
		: account.hostId;
	_email = account.email;
	_redirectUri = ChatGpt::RedirectUri();
	_url = ChatGpt::AuthorizeUrl({
		.clientId = _clientId,
		.hostId = _hostId,
		.redirectUri = _redirectUri,
		.state = _state,
		.nonce = _nonce,
		.challenge = _pkce.challenge,
		.loginHint = _email,
	});
	if (_server.listen(QHostAddress::LocalHost, ChatGpt::kCallbackPort)) {
		connect(&_server, &QTcpServer::newConnection, this, [=] {
			accept();
		});
	} else {
		LOG(("Agent Info: ChatGPT sign-in can't listen on port %1."
			).arg(ChatGpt::kCallbackPort));
	}
	openBrowser();
}

ChatGptLogin::Flow::~Flow() {
	if (_exchangeId) {
		Tokens::Instance().transport().cancel(_exchangeId);
	}
}

bool ChatGptLogin::Flow::listening() const {
	return _server.isListening();
}

QString ChatGptLogin::Flow::authorizeUrl() const {
	return _url;
}

void ChatGptLogin::Flow::openBrowser() {
	File::OpenUrl(_url);
}

QString ChatGptLogin::Flow::paste(const QString &text) {
	if (_finished || _exchanging) {
		return QString();
	}
	const auto result = ChatGpt::ParsePastedUrl(
		text,
		_redirectUri,
		_state,
		_clientId);
	if (result.error == u"access_denied"_q) {
		finish(tr::lng_agent_chatgpt_denied(tr::now));
		return QString();
	} else if (!result.error.isEmpty()) {
		return tr::lng_agent_chatgpt_paste_wrong(tr::now);
	}
	exchange(result.code, result.clientId);
	return QString();
}

void ChatGptLogin::Flow::accept() {
	while (const auto socket = _server.nextPendingConnection()) {
		socket->setParent(this);
		connect(socket, &QTcpSocket::readyRead, this, [=] {
			read(socket);
		});
		connect(socket, &QTcpSocket::disconnected, socket, [=] {
			socket->deleteLater();
		});
	}
}

void ChatGptLogin::Flow::read(not_null<QTcpSocket*> socket) {
	if (socket->property("aigram_handled").toBool()) {
		socket->readAll();
		return;
	}
	auto buffer = socket->property("aigram_buffer").toByteArray();
	buffer += socket->readAll();
	if (buffer.size() > kMaxRequestSize) {
		socket->abort();
		return;
	}
	const auto end = buffer.indexOf("\r\n\r\n");
	if (end < 0) {
		socket->setProperty("aigram_buffer", buffer);
		return;
	}
	socket->setProperty("aigram_handled", true);
	const auto line = buffer.left(buffer.indexOf("\r\n"));
	const auto parts = line.split(' ');
	if (parts.size() < 3 || parts[0] != "GET") {
		Reply(socket, 400, CallbackPage(
			tr::lng_agent_chatgpt_page_failed(tr::now)));
		return;
	}
	handle(socket, parts[1]);
}

void ChatGptLogin::Flow::handle(
		not_null<QTcpSocket*> socket,
		const QByteArray &target) {
	const auto url = QUrl(
		u"http://127.0.0.1:%1"_q.arg(ChatGpt::kCallbackPort)
		+ QString::fromLatin1(target));
	const auto result = ChatGpt::ParseCallback(url, _state, _clientId);
	if (result.error == u"not_found"_q) {
		Reply(socket, 404, QByteArray());
		return;
	} else if (_finished || _exchanging) {
		Reply(socket, 200, CallbackPage(
			tr::lng_agent_chatgpt_page_done(tr::now)));
		return;
	} else if (result.error == u"access_denied"_q) {
		Reply(socket, 200, CallbackPage(
			tr::lng_agent_chatgpt_page_failed(tr::now)));
		finish(tr::lng_agent_chatgpt_denied(tr::now));
		return;
	} else if (result.error == u"state_mismatch"_q
		|| result.error == u"missing_code"_q) {
		Reply(socket, 400, CallbackPage(
			tr::lng_agent_chatgpt_page_failed(tr::now)));
		return;
	} else if (!result.error.isEmpty()) {
		Reply(socket, 200, CallbackPage(
			tr::lng_agent_chatgpt_page_failed(tr::now)));
		fail(result.error);
		return;
	}
	Reply(socket, 200, CallbackPage(
		tr::lng_agent_chatgpt_page_done(tr::now)));
	exchange(result.code, result.clientId);
}

void ChatGptLogin::Flow::exchange(
		const QString &code,
		const QString &clientId) {
	_exchanging = true;
	_server.close();
	const auto status = std::make_shared<int>(0);
	const auto body = std::make_shared<QByteArray>();
	const auto weak = QPointer<Flow>(this);
	_exchangeId = Tokens::Instance().transport().start(
		ChatGpt::CodeRequest(clientId, code, _pkce.verifier, _redirectUri),
		TransportHandlers{
			[=](HttpHead head) {
				*status = head.status;
			},
			[=](QByteArray chunk) {
				*body += chunk;
			},
			[=](QString error) {
				if (const auto strong = weak.data()) {
					strong->_exchangeId = 0;
					strong->exchanged(clientId, *status, *body, error);
				}
			},
		});
}

void ChatGptLogin::Flow::exchanged(
		const QString &clientId,
		int status,
		const QByteArray &body,
		const QString &error) {
	_exchanging = false;
	if (!error.isEmpty()) {
		finish(tr::lng_agent_chatgpt_login_failed(
			tr::now,
			lt_error,
			error));
		return;
	}
	const auto tokens = ChatGpt::ParseTokenResponse(status, body);
	if (!tokens.errorCode.isEmpty()) {
		fail(tokens.errorText.isEmpty()
			? tokens.errorCode
			: tokens.errorText);
		return;
	} else if (!ChatGpt::HasPlanScope(tokens.scopes)) {
		finish(tr::lng_agent_chatgpt_no_plan(tr::now));
		return;
	}
	auto account = Tokens::Instance().account();
	account.clientId = clientId;
	account.hostId = _hostId;
	if (const auto claims = ChatGpt::ParseIdToken(tokens.idToken)) {
		account.email = claims->email;
		account.subject = claims->subject;
		account.plan = claims->plan;
	}
	ChatGpt::ApplyTokens(account, tokens, NowMs());
	Tokens::Instance().set(std::move(account), true);
	finish(QString());
}

void ChatGptLogin::Flow::fail(QString error) {
	if (!_clientId.isEmpty()) {
		auto account = Tokens::Instance().account();
		if (account.clientId == _clientId && !ChatGpt::SignedIn(account)) {
			account.clientId = QString();
			Tokens::Instance().set(std::move(account), false);
		}
	}
	finish(tr::lng_agent_chatgpt_login_failed(
		tr::now,
		lt_error,
		error));
}

void ChatGptLogin::Flow::finish(QString error) {
	if (_finished) {
		return;
	}
	_finished = true;
	_server.close();
	crl::on_main(this, [=] {
		if (const auto done = base::take(_done)) {
			done(error);
		}
	});
}

ChatGptLogin::ChatGptLogin(Fn<void(QString error)> done)
: _flow(std::make_unique<Flow>(std::move(done))) {
}

ChatGptLogin::~ChatGptLogin() = default;

bool ChatGptLogin::listening() const {
	return _flow->listening();
}

QString ChatGptLogin::authorizeUrl() const {
	return _flow->authorizeUrl();
}

void ChatGptLogin::openBrowser() {
	_flow->openBrowser();
}

QString ChatGptLogin::paste(const QString &text) {
	return _flow->paste(text);
}

struct ChatGptTransport::Pending {
	uint64 id = 0;
	HttpRequest request;
	TransportHandlers handlers;
	uint64 innerId = 0;
	uint64 waiterId = 0;
	QString token;
	bool retried = false;
	bool swallow = false;
};

ChatGptTransport::ChatGptTransport(not_null<Transport*> inner)
: _inner(inner) {
}

ChatGptTransport::~ChatGptTransport() {
	for (const auto &pending : base::take(_pending)) {
		Tokens::Instance().cancel(pending->waiterId);
		if (pending->innerId) {
			_inner->cancel(pending->innerId);
		}
	}
}

uint64 ChatGptTransport::start(
		HttpRequest request,
		TransportHandlers handlers) {
	auto pending = std::make_shared<Pending>();
	pending->id = ++_nextId;
	pending->request = std::move(request);
	pending->handlers = std::move(handlers);
	_pending.push_back(pending);
	if (!ChatGpt::IsPlanApiUrl(pending->request.url)) {
		crl::on_main(this, [=] {
			if (ranges::contains(_pending, pending)) {
				finishPending(
					pending,
					u"The ChatGPT sign-in works only with"
					" https://api.openai.com/."_q);
			}
		});
		return pending->id;
	}
	run(pending);
	return pending->id;
}

void ChatGptTransport::run(const std::shared_ptr<Pending> &pending) {
	const auto weak = base::make_weak(this);
	const auto failedToken = pending->token;
	pending->waiterId = Tokens::Instance().request(failedToken, [=](
			QString token,
			QString error) {
		if (!weak || !ranges::contains(_pending, pending)) {
			return;
		}
		pending->waiterId = 0;
		if (error.isEmpty()) {
			startInner(pending, token);
		} else {
			crl::on_main(this, [=] {
				fail(pending, error);
			});
		}
	});
}

void ChatGptTransport::startInner(
		const std::shared_ptr<Pending> &pending,
		const QString &token) {
	pending->token = token;
	auto request = pending->request;
	auto &headers = request.headers;
	headers.erase(ranges::remove_if(headers, [](const auto &header) {
		return header.first.compare("authorization", Qt::CaseInsensitive)
			== 0;
	}), end(headers));
	headers.emplace_back("Authorization", "Bearer " + token.toUtf8());
	const auto weak = base::make_weak(this);
	pending->innerId = _inner->start(std::move(request), TransportHandlers{
		[=](HttpHead head) {
			if (head.status == 401 && !pending->retried) {
				pending->swallow = true;
			} else if (const auto &callback = pending->handlers.head) {
				callback(std::move(head));
			}
		},
		[=](QByteArray chunk) {
			if (pending->swallow) {
				return;
			} else if (const auto &callback = pending->handlers.data) {
				callback(std::move(chunk));
			}
		},
		[=](QString error) {
			if (!weak) {
				return;
			}
			pending->innerId = 0;
			if (pending->swallow) {
				pending->swallow = false;
				pending->retried = true;
				run(pending);
				return;
			}
			finishPending(pending, std::move(error));
		},
	});
}

void ChatGptTransport::fail(
		const std::shared_ptr<Pending> &pending,
		const QString &error) {
	if (!ranges::contains(_pending, pending)) {
		return;
	}
	const auto signedOut = !ChatGpt::SignedIn(
		Tokens::Instance().account());
	if (signedOut) {
		const auto weak = base::make_weak(this);
		if (const auto &head = pending->handlers.head) {
			head(HttpHead{ .status = 401 });
		}
		if (!weak || !ranges::contains(_pending, pending)) {
			return;
		}
		if (const auto &data = pending->handlers.data) {
			data(ChatGpt::SignedOutBody(error));
		}
		if (!weak || !ranges::contains(_pending, pending)) {
			return;
		}
		finishPending(pending, QString());
	} else {
		finishPending(pending, error);
	}
}

void ChatGptTransport::finishPending(
		const std::shared_ptr<Pending> &pending,
		QString error) {
	_pending.erase(ranges::remove(_pending, pending), end(_pending));
	if (const auto finished = base::take(pending->handlers.finished)) {
		finished(std::move(error));
	}
}

void ChatGptTransport::cancel(uint64 requestId) {
	const auto i = ranges::find(_pending, requestId, &Pending::id);
	if (i == end(_pending)) {
		return;
	}
	const auto pending = *i;
	_pending.erase(i);
	Tokens::Instance().cancel(pending->waiterId);
	if (pending->innerId) {
		_inner->cancel(pending->innerId);
	}
}

ChatGptAccount ChatGptAccountState() {
	return Tokens::Instance().account();
}

bool ChatGptSignedIn() {
	return ChatGpt::SignedIn(ChatGptAccountState());
}

rpl::producer<> ChatGptAccountChanged() {
	return Tokens::Instance().changes();
}

void ChatGptSignOut() {
	auto account = Tokens::Instance().account();
	if (!ChatGpt::SignedIn(account)) {
		return;
	}
	const auto clientId = account.clientId;
	const auto refreshToken = account.refreshToken;
	ChatGpt::ClearTokens(account);
	Tokens::Instance().set(std::move(account), true);

	auto &transport = Tokens::Instance().transport();
	const auto body = std::make_shared<QByteArray>();
	(void)transport.start(ChatGpt::DiscoveryRequest(), TransportHandlers{
		[](HttpHead) {},
		[=](QByteArray chunk) {
			*body += chunk;
		},
		[=](QString error) {
			const auto endpoint = error.isEmpty()
				? ChatGpt::ParseRevocationEndpoint(*body)
				: QString();
			if (endpoint.isEmpty()) {
				LOG(("Agent Info: ChatGPT sign-out without revocation."));
				return;
			}
			(void)Tokens::Instance().transport().start(
				ChatGpt::RevokeRequest(endpoint, clientId, refreshToken),
				TransportHandlers{
					[](HttpHead) {},
					[](QByteArray) {},
					[](QString) {},
				});
		},
	});
}

} // namespace Agent
