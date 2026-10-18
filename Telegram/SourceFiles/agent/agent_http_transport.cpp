/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_http_transport.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Agent {

struct HttpTransport::Active {
};

HttpTransport::HttpTransport()
: _manager(std::make_unique<QNetworkAccessManager>()) {
}

HttpTransport::~HttpTransport() = default;

uint64 HttpTransport::start(HttpRequest request, TransportHandlers handlers) {
	return ++_autoincrement;
}

void HttpTransport::cancel(uint64 requestId) {
	_active.remove(requestId);
}

} // namespace Agent
