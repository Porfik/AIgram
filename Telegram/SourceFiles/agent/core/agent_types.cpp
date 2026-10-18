/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_types.h"

#include "base/assertion.h"

namespace Agent {

QString ErrorKindName(ErrorKind kind) {
	switch (kind) {
	case ErrorKind::Network: return u"network"_q;
	case ErrorKind::Auth: return u"auth"_q;
	case ErrorKind::RateLimited: return u"rate_limited"_q;
	case ErrorKind::Overloaded: return u"overloaded"_q;
	case ErrorKind::BadRequest: return u"bad_request"_q;
	case ErrorKind::BadResponse: return u"bad_response"_q;
	}
	Unexpected("Kind in Agent::ErrorKindName.");
}

} // namespace Agent
