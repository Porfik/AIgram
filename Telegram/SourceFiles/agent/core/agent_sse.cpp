/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_sse.h"

namespace Agent {

void SseParser::processLine(
		std::vector<SseEvent> &to,
		const QByteArray &line) {
	if (line.isEmpty()) {
		if (_hasData) {
			to.push_back({ _event, _data });
			_event = QByteArray();
			_data = QByteArray();
			_hasData = false;
		}
		return;
	} else if (line.startsWith(':')) {
		return;
	}
	const auto colon = line.indexOf(':');
	const auto field = (colon < 0) ? line : line.mid(0, colon);
	auto value = (colon < 0) ? QByteArray() : line.mid(colon + 1);
	if (value.startsWith(' ')) {
		value.remove(0, 1);
	}
	if (field == "data") {
		if (_hasData) {
			_data += '\n';
		}
		_data += value;
		_hasData = true;
	} else if (field == "event") {
		_event = value;
	}
}

void SseParser::processLines(
		std::vector<SseEvent> &to,
		bool final) {
	auto start = qsizetype(0);
	const auto size = _buffer.size();
	for (auto i = qsizetype(0); i < size;) {
		if (_buffer[i] == '\n') {
			processLine(to, _buffer.mid(start, i - start));
			start = ++i;
		} else if (_buffer[i] == '\r') {
			if (!final && i + 1 == size) {
				break;
			}
			processLine(to, _buffer.mid(start, i - start));
			i += (i + 1 < size && _buffer[i + 1] == '\n') ? 2 : 1;
			start = i;
		} else {
			++i;
		}
	}
	_buffer = _buffer.mid(start);
}

std::vector<SseEvent> SseParser::feed(const QByteArray &chunk) {
	_buffer += chunk;
	auto result = std::vector<SseEvent>();
	processLines(result, false);
	return result;
}

std::vector<SseEvent> SseParser::finish() {
	auto result = std::vector<SseEvent>();
	processLines(result, true);
	if (!_buffer.isEmpty()) {
		processLine(result, _buffer);
		_buffer.clear();
	}
	if (_hasData) {
		result.push_back({ _event, _data });
		_event = QByteArray();
		_data = QByteArray();
		_hasData = false;
	}
	return result;
}

} // namespace Agent
