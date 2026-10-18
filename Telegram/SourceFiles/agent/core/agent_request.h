/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_adapter.h"
#include "base/weak_ptr.h"

#include <memory>

namespace Agent {

class SingleRequest final : public base::has_weak_ptr {
public:
	SingleRequest(
		std::unique_ptr<Adapter> adapter,
		not_null<Transport*> transport,
		Request request,
		Fn<void(Response)> done);
	~SingleRequest();

private:
	const std::unique_ptr<Adapter> _adapter;
	const not_null<Transport*> _transport;
	Fn<void(Response)> _done;
	std::unique_ptr<ResponseParser> _parser;
	uint64 _requestId = 0;

};

} // namespace Agent
