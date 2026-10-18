/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_transport.h"
#include "base/flat_map.h"

#include <QtCore/QObject>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace Agent {

class HttpTransport final : public QObject, public Transport {
public:
	HttpTransport();
	~HttpTransport();

	[[nodiscard]] uint64 start(
		HttpRequest request,
		TransportHandlers handlers) override;
	void cancel(uint64 requestId) override;

private:
	struct Active;

	const std::unique_ptr<QNetworkAccessManager> _manager;
	base::flat_map<uint64, std::unique_ptr<Active>> _active;
	uint64 _autoincrement = 0;

};

} // namespace Agent
