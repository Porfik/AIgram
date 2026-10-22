/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_chatgpt.h"

#include "agent/agent_settings.h"
#include "agent/core/agent_chatgpt_auth.h"

#include <rpl/event_stream.h>

namespace Agent {
namespace {

[[nodiscard]] rpl::event_stream<> &AccountChangedStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

} // namespace

class ChatGptLogin::Flow final {
public:
	explicit Flow(Fn<void(QString error)> done) : _done(std::move(done)) {
	}

private:
	Fn<void(QString error)> _done;

};

struct ChatGptTransport::Pending {
	uint64 id = 0;
	uint64 innerId = 0;
};

ChatGptLogin::ChatGptLogin(Fn<void(QString error)> done)
: _flow(std::make_unique<Flow>(std::move(done))) {
}

ChatGptLogin::~ChatGptLogin() = default;

bool ChatGptLogin::listening() const {
	return false;
}

QString ChatGptLogin::authorizeUrl() const {
	return QString();
}

void ChatGptLogin::openBrowser() {
}

QString ChatGptLogin::paste(const QString &text) {
	return QString();
}

ChatGptTransport::ChatGptTransport(not_null<Transport*> inner)
: _inner(inner) {
}

ChatGptTransport::~ChatGptTransport() = default;

uint64 ChatGptTransport::start(
		HttpRequest request,
		TransportHandlers handlers) {
	return _inner->start(std::move(request), std::move(handlers));
}

void ChatGptTransport::cancel(uint64 requestId) {
	_inner->cancel(requestId);
}

ChatGptAccount ChatGptAccountState() {
	return LoadProviders().chatGpt;
}

bool ChatGptSignedIn() {
	return ChatGpt::SignedIn(ChatGptAccountState());
}

rpl::producer<> ChatGptAccountChanged() {
	return AccountChangedStream().events();
}

void ChatGptSignOut() {
}

} // namespace Agent
