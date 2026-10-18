/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_request.h"

namespace Agent {

SingleRequest::SingleRequest(
	std::unique_ptr<Adapter> adapter,
	not_null<Transport*> transport,
	Request request,
	Fn<void(Response)> done)
: _adapter(std::move(adapter))
, _transport(transport)
, _done(std::move(done)) {
}

SingleRequest::~SingleRequest() = default;

} // namespace Agent
