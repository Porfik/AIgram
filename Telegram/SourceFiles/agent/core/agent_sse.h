/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>

#include <vector>

namespace Agent {

struct SseEvent {
	QByteArray event;
	QByteArray data;
};

class SseParser final {
public:
	[[nodiscard]] std::vector<SseEvent> feed(const QByteArray &chunk);
	[[nodiscard]] std::vector<SseEvent> finish();

private:
	void processLine(std::vector<SseEvent> &to, const QByteArray &line);
	void processLines(std::vector<SseEvent> &to, bool final);

	QByteArray _buffer;
	QByteArray _event;
	QByteArray _data;
	bool _hasData = false;

};

} // namespace Agent
