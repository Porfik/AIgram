/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"
#include "agent/core/agent_transport.h"
#include "base/weak_ptr.h"

#include <rpl/producer.h>

#include <memory>
#include <vector>

namespace Agent {

class ChatGptLogin final {
public:
	explicit ChatGptLogin(Fn<void(QString error)> done);
	~ChatGptLogin();

	[[nodiscard]] bool listening() const;
	[[nodiscard]] QString authorizeUrl() const;
	void openBrowser();
	[[nodiscard]] QString paste(const QString &text);

private:
	class Flow;
	const std::unique_ptr<Flow> _flow;

};

class ChatGptTransport final
	: public Transport
	, public base::has_weak_ptr {
public:
	explicit ChatGptTransport(not_null<Transport*> inner);
	~ChatGptTransport();

	[[nodiscard]] uint64 start(
		HttpRequest request,
		TransportHandlers handlers) override;
	void cancel(uint64 requestId) override;

private:
	struct Pending;

	void run(const std::shared_ptr<Pending> &pending);
	void startInner(
		const std::shared_ptr<Pending> &pending,
		const QString &token);
	void fail(
		const std::shared_ptr<Pending> &pending,
		const QString &error);
	void finishPending(
		const std::shared_ptr<Pending> &pending,
		QString error);

	const not_null<Transport*> _inner;
	std::vector<std::shared_ptr<Pending>> _pending;
	uint64 _nextId = 0;

};

[[nodiscard]] ChatGptAccount ChatGptAccountState();
[[nodiscard]] bool ChatGptSignedIn();
[[nodiscard]] rpl::producer<> ChatGptAccountChanged();
void ChatGptSignOut();

} // namespace Agent
