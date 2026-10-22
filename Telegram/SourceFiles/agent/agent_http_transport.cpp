/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_http_transport.h"

#include "base/algorithm.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Agent {
namespace {

constexpr auto kTransferTimeout = crl::time(120) * crl::time(1000);

[[nodiscard]] HttpHead BuildHead(const QNetworkReply &reply) {
	const auto status = reply.attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	return {
		.status = status.isValid() ? status.toInt() : 0,
		.retryAfter = reply.rawHeader("Retry-After"),
	};
}

} // namespace

struct HttpTransport::Active {
	QNetworkReply *reply = nullptr;
	TransportHandlers handlers;
	bool headSent = false;
	bool finished = false;

};

HttpTransport::HttpTransport()
: QObject(nullptr)
, _manager(std::make_unique<QNetworkAccessManager>()) {
}

HttpTransport::~HttpTransport() {
	for (const auto &entry : _active) {
		const auto &active = entry.second;
		active->handlers = TransportHandlers();
		active->reply->disconnect(this);
		active->reply->abort();
		active->reply->deleteLater();
	}
	_active.clear();
}

uint64 HttpTransport::start(HttpRequest request, TransportHandlers handlers) {
	const auto requestId = ++_autoincrement;

	auto networkRequest = QNetworkRequest(QUrl(request.url));
	networkRequest.setTransferTimeout(kTransferTimeout);
	networkRequest.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::SameOriginRedirectPolicy);
	for (const auto &[name, value] : request.headers) {
		networkRequest.setRawHeader(name, value);
	}

	auto active = std::make_unique<Active>();
	active->handlers = std::move(handlers);
	const auto raw = request.post
		? _manager->post(networkRequest, request.body)
		: _manager->get(networkRequest);
	active->reply = raw;

	const auto handleFinished = [this, requestId] {
		const auto i = _active.find(requestId);
		if (i == _active.end()) {
			return;
		}
		auto &active = *i->second;
		if (active.finished) {
			return;
		}
		const auto reply = active.reply;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute);
		auto error = status.isValid() ? QString() : reply->errorString();
		active.finished = true;
		auto taken = base::take(active.handlers);
		_active.erase(i);
		reply->deleteLater();
		if (const auto finished = taken.finished) {
			finished(std::move(error));
		}
	};

	QObject::connect(raw, &QNetworkReply::metaDataChanged, this, [this, requestId] {
		const auto i = _active.find(requestId);
		if (i == _active.end()) {
			return;
		}
		auto &active = *i->second;
		if (active.headSent || active.finished) {
			return;
		}
		active.headSent = true;
		if (const auto head = active.handlers.head) {
			head(BuildHead(*active.reply));
		}
	});

	QObject::connect(raw, &QNetworkReply::readyRead, this, [this, requestId] {
		const auto i = _active.find(requestId);
		if (i == _active.end()) {
			return;
		}
		auto &active = *i->second;
		if (active.finished) {
			return;
		}
		if (!active.headSent) {
			active.headSent = true;
			if (const auto head = active.handlers.head) {
				head(BuildHead(*active.reply));
			}
			if (_active.find(requestId) == _active.end()) {
				return;
			}
		}
		const auto chunk = active.reply->readAll();
		if (const auto data = active.handlers.data) {
			data(std::move(chunk));
		}
	});

	QObject::connect(raw, &QNetworkReply::finished, this, handleFinished);

	_active.emplace(requestId, std::move(active));
	if (raw->isFinished()) {
		QMetaObject::invokeMethod(
			this,
			std::move(handleFinished),
			Qt::QueuedConnection);
	}
	return requestId;
}

void HttpTransport::cancel(uint64 requestId) {
	const auto i = _active.find(requestId);
	if (i == _active.end()) {
		return;
	}
	auto active = base::take(i->second);
	_active.erase(i);
	active->handlers = TransportHandlers();
	active->reply->disconnect(this);
	active->reply->abort();
	active->reply->deleteLater();
}

} // namespace Agent
