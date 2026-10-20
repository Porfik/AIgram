/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_types.h"
#include "base/weak_ptr.h"
#include "data/data_file_origin.h"

#include <rpl/lifetime.h>

#include <optional>

class HistoryItem;
class PhotoData;
class DocumentData;

namespace Data {
class PhotoMedia;
class DocumentMedia;
} // namespace Data

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
	void startPhoto(not_null<PhotoData*> photo);
	void startDocument(not_null<DocumentData*> document);
	void check();
	void checkPhoto();
	void checkDocument();
	void finish(QByteArray bytes, QString error);

	const MediaPart _part;
	Data::FileOrigin _origin;
	Fn<void(QByteArray bytes, QString error)> _done;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	QString _tempPath;
	bool _finished = false;
	rpl::lifetime _lifetime;

};

} // namespace Agent
