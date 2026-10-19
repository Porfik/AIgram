/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QJsonObject>
#include <QtCore/QString>

class HistoryItem;
class PeerData;

struct TextWithEntities;

namespace Agent::ToolFormat {

[[nodiscard]] QString PeerKind(not_null<PeerData*> peer);
[[nodiscard]] QString PeerUsername(not_null<PeerData*> peer);
[[nodiscard]] QString EntitiesToMarkdown(const TextWithEntities &text);
[[nodiscard]] QString MediaDescription(not_null<HistoryItem*> item);
[[nodiscard]] QJsonObject SerializeMessage(not_null<HistoryItem*> item);
[[nodiscard]] QJsonObject SerializeMessageWithChat(not_null<HistoryItem*> item);

} // namespace Agent::ToolFormat
