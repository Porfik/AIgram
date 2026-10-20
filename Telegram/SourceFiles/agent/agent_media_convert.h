/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_documents.h"

#include <QtGui/QImage>

#include <optional>
#include <vector>

namespace Agent {

[[nodiscard]] QByteArray PrepareImageForModel(const QImage &image);
[[nodiscard]] QByteArray PrepareImageForModel(const QByteArray &bytes);

struct DecodedAudio {
	QByteArray wav;
	int totalSeconds = 0;
	int fromSeconds = 0;
	int toSeconds = 0;
};

[[nodiscard]] std::optional<DecodedAudio> DecodeAudio(
	const QByteArray &bytes,
	int fromSeconds,
	int seconds);

[[nodiscard]] std::optional<std::vector<OfficeEntry>> UnzipOffice(
	const QByteArray &zip,
	DocumentKind kind);

} // namespace Agent
