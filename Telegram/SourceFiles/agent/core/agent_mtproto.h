/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_tl_schema.h"

#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <vector>

namespace Agent {

enum class MethodClass : uchar {
	Read,
	Write,
	Forbidden,
};

[[nodiscard]] MethodClass ClassifyMethod(
	const TlSchema &schema,
	const QString &name);
[[nodiscard]] std::vector<QString> MethodRedirect(const QString &name);

enum class PeerRefKind : uchar {
	None,
	Peer,
	User,
	Chat,
	Channel,
	Bare,
};

struct PeerRef {
	PeerRefKind kind = PeerRefKind::None;
	uint64 id = 0;
};

[[nodiscard]] PeerRef ParsePeerRef(const QJsonValue &value);

struct MethodNamespace {
	QString name;
	int count = 0;
};

[[nodiscard]] std::vector<MethodNamespace> MethodNamespaces(
	const TlSchema &schema);
[[nodiscard]] std::vector<const TlCombinator*> FindMethods(
	const TlSchema &schema,
	const QString &query,
	int limit,
	bool *partial = nullptr);
[[nodiscard]] QString MethodSignature(const TlCombinator &method);
[[nodiscard]] std::optional<QString> DescribeTl(
	const TlSchema &schema,
	const QString &name,
	QString &error);

} // namespace Agent
