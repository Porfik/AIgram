/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_types.h"
#include "base/weak_ptr.h"

#include <rpl/lifetime.h>

#include <optional>

class HistoryItem;
class PhotoData;
class DocumentData;

namespace Agent {

enum class MessageMediaType : uchar {
	Photo,
	Sticker,
	Animation,
	Video,
	RoundVideo,
	Voice,
	Audio,
	ImageFile,
	Document,
};

struct MessageMedia {
	MessageMediaType type = MessageMediaType::Document;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	QString name;
	QString mimeType;
	int64 size = 0;
	int duration = 0;
};

[[nodiscard]] std::optional<MessageMedia> FindMessageMedia(
	not_null<HistoryItem*> item);

enum class MediaPart : uchar {
	Full,
	Thumbnail,
};

class MediaLoader final : public base::has_weak_ptr {
public:
	MediaLoader(
		not_null<HistoryItem*> item,
		MediaPart part,
		Fn<void(QByteArray bytes, QString error)> done);
	~MediaLoader();

private:
	Fn<void(QByteArray bytes, QString error)> _done;
	rpl::lifetime _lifetime;

};

} // namespace Agent
