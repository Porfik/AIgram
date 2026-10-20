/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_media_convert.h"

namespace Agent {

QByteArray PrepareImageForModel(const QImage &image) {
	return QByteArray();
}

QByteArray PrepareImageForModel(const QByteArray &bytes) {
	return QByteArray();
}

std::optional<DecodedAudio> DecodeAudio(
		const QByteArray &bytes,
		int fromSeconds,
		int seconds) {
	return std::nullopt;
}

std::optional<std::vector<OfficeEntry>> UnzipOffice(
		const QByteArray &zip,
		DocumentKind kind) {
	return std::nullopt;
}

} // namespace Agent
