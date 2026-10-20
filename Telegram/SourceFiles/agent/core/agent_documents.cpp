/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_documents.h"

namespace Agent {

DocumentKind DetectDocument(const QString &name, const QString &mimeType) {
	return DocumentKind::Other;
}

bool IsOfficeDocument(DocumentKind kind) {
	return false;
}

std::optional<QString> DecodeText(const QByteArray &bytes) {
	return std::nullopt;
}

bool WantOfficeEntry(DocumentKind kind, const QString &path) {
	return false;
}

QString ExtractOfficeText(
		DocumentKind kind,
		const std::vector<OfficeEntry> &entries) {
	return QString();
}

TextPage PageText(const QString &text, int offset, int limit) {
	return TextPage();
}

} // namespace Agent
