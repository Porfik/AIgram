/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_request.h"

#include "base/algorithm.h"

#include <crl/crl_on_main.h>

#include <utility>

namespace Agent {

SingleRequest::SingleRequest(
	std::unique_ptr<Adapter> adapter,
	not_null<Transport*> transport,
	Request request,
	Fn<void(Response)> done)
: _adapter(std::move(adapter))
, _transport(transport)
, _done(std::move(done)) {
	_parser = _adapter->parser();
	_requestId = _transport->start(
		_adapter->prepare(request),
		TransportHandlers{
			crl::guard(this, [=](HttpHead head) {
				if (_parser) {
					_parser->head(head);
				}
			}),
			crl::guard(this, [=](QByteArray chunk) {
				if (_parser) {
					_parser->feed(chunk);
				}
			}),
			crl::guard(this, [=](QString error) {
				finish(error);
			}),
		});
}

SingleRequest::~SingleRequest() {
	if (const auto id = base::take(_requestId)) {
		_transport->cancel(id);
	}
}

void SingleRequest::finish(const QString &networkError) {
	if (!_parser) {
		return;
	}
	const auto parser = std::move(_parser);
	_requestId = 0;
	auto response = parser->finish(networkError);
	if (const auto done = base::take(_done)) {
		done(std::move(response));
	}
}

} // namespace Agent
