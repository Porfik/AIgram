/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_tl_schema.h"

#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <optional>
#include <vector>

namespace Agent {

struct TlRefs {
	Fn<std::optional<QByteArray>(
		const QString &type,
		const QJsonValue &ref,
		QString &error)> input;
};

[[nodiscard]] std::optional<QByteArray> EncodeTlCall(
	const TlSchema &schema,
	const QString &method,
	const QJsonObject &params,
	const TlRefs &refs,
	QString &error);

struct TlSpan {
	QString type;
	int begin = 0;
	int end = 0;
};

struct TlDecodeOptions {
	std::vector<QString> spanTypes;
	int maxBytesShown = 256;
};

[[nodiscard]] std::optional<QJsonValue> DecodeTl(
	const TlSchema &schema,
	const QString &type,
	const QByteArray &bytes,
	QString &error,
	const TlDecodeOptions &options = {},
	std::vector<TlSpan> *spans = nullptr);

[[nodiscard]] QJsonValue ScrubExcludedPeers(
	QJsonValue value,
	Fn<bool(const QString &kind, int64 id)> excluded);

} // namespace Agent
