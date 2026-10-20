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

#include <optional>
#include <vector>

namespace Agent {

enum class DocumentKind : uchar {
	Text,
	Docx,
	Xlsx,
	Pptx,
	OpenDocument,
	Pdf,
	Other,
};

[[nodiscard]] DocumentKind DetectDocument(
	const QString &name,
	const QString &mimeType);
[[nodiscard]] bool IsOfficeDocument(DocumentKind kind);
[[nodiscard]] std::optional<QString> DecodeText(const QByteArray &bytes);
[[nodiscard]] bool WantOfficeEntry(DocumentKind kind, const QString &path);

struct OfficeEntry {
	QString path;
	QByteArray bytes;
};

[[nodiscard]] QString ExtractOfficeText(
	DocumentKind kind,
	const std::vector<OfficeEntry> &entries);

struct TextPage {
	QString text;
	int offset = 0;
	int total = 0;
	int nextOffset = -1;
};

[[nodiscard]] TextPage PageText(const QString &text, int offset, int limit);

} // namespace Agent
