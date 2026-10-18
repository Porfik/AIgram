/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <utility>
#include <vector>

namespace Agent {

struct HttpRequest {
	QString url;
	std::vector<std::pair<QByteArray, QByteArray>> headers;
	QByteArray body;
	bool post = true;
};

struct HttpHead {
	int status = 0;
	QByteArray retryAfter;
};

struct TransportHandlers {
	Fn<void(HttpHead)> head;
	Fn<void(QByteArray)> data;
	Fn<void(QString)> finished;
};

class Transport {
public:
	virtual ~Transport() = default;

	[[nodiscard]] virtual uint64 start(
		HttpRequest request,
		TransportHandlers handlers) = 0;
	virtual void cancel(uint64 requestId) = 0;

};

} // namespace Agent
