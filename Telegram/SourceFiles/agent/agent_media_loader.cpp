/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_media_loader.h"

#include "base/random.h"
#include "crl/crl_on_main.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "storage/storage_account.h"
#include "ui/image/image.h"

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QIODevice>
#include <QtGui/QImage>

namespace Agent {

std::optional<MessageMedia> FindMessageMedia(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return std::nullopt;
	}
	auto result = MessageMedia();
	if (const auto photo = media->photo()) {
		result.type = MessageMediaType::Photo;
		result.photo = photo;
		result.name = u"photo.jpg"_q;
		result.mimeType = u"image/jpeg"_q;
		result.size = photo->imageByteSize(Data::PhotoSize::Large);
		return result;
	}
	const auto document = media->document();
	if (!document) {
		return std::nullopt;
	}
	result.document = document;
	if (document->sticker()) {
		result.type = MessageMediaType::Sticker;
	} else if (document->isVideoMessage()) {
		result.type = MessageMediaType::RoundVideo;
	} else if (document->isVoiceMessage()) {
		result.type = MessageMediaType::Voice;
	} else if (document->isAnimation()) {
		result.type = MessageMediaType::Animation;
	} else if (document->isVideoFile()) {
		result.type = MessageMediaType::Video;
	} else if (document->isAudioFile() || document->isSong()) {
		result.type = MessageMediaType::Audio;
	} else if (document->isImage()) {
		result.type = MessageMediaType::ImageFile;
	} else {
		result.type = MessageMediaType::Document;
	}
	result.name = document->filename();
	result.mimeType = document->mimeString();
	result.size = document->size;
	result.duration = document->hasDuration()
		? int(std::max(document->duration(), crl::time(0)) / 1000)
		: 0;
	return result;
}

MediaLoader::MediaLoader(
	not_null<HistoryItem*> item,
	MediaPart part,
	Fn<void(QByteArray bytes, QString error)> done)
: _part(part)
, _origin(Data::FileOriginMessage(item->fullId()))
, _done(std::move(done)) {
	const auto media = FindMessageMedia(item);
	if (!media) {
		finish(QByteArray(), u"The message has no photo or file."_q);
		return;
	}
	const auto session = &item->history()->session();
	rpl::merge(
		session->downloaderTaskFinished(),
		session->data().documentLoadProgress() | rpl::to_empty
	) | rpl::on_next([=] {
		check();
	}, _lifetime);
	if (media->photo) {
		startPhoto(media->photo);
	} else {
		startDocument(media->document);
	}
	check();
}

MediaLoader::~MediaLoader() {
	if (!_tempPath.isEmpty()) {
		if (_documentMedia && !_finished) {
			_documentMedia->owner()->cancel();
		}
		QFile::remove(_tempPath);
	}
}

void MediaLoader::startPhoto(not_null<PhotoData*> photo) {
	_photoMedia = photo->createMediaView();
	_photoMedia->wanted(
		(_part == MediaPart::Full)
			? Data::PhotoSize::Large
			: Data::PhotoSize::Thumbnail,
		_origin);
}

void MediaLoader::startDocument(not_null<DocumentData*> document) {
	_documentMedia = document->createMediaView();
	if (_part == MediaPart::Thumbnail) {
		if (document->hasThumbnail()) {
			_documentMedia->thumbnailWanted(_origin);
		}
		return;
	} else if (_documentMedia->loaded(true) || document->loading()) {
		return;
	} else if (document->size <= Storage::kMaxFileInMemory) {
		document->save(_origin, QString());
		return;
	}
	const auto folder = document->session().local().tempDirectory()
		+ u"agent/%1/"_q.arg(base::RandomValue<uint64>());
	QDir().mkpath(folder);
	auto name = QFileInfo(document->filename()).fileName();
	if (name.isEmpty()) {
		name = u"file"_q;
	}
	_tempPath = folder + name;
	document->save(_origin, _tempPath);
}

void MediaLoader::check() {
	if (_finished) {
		return;
	} else if (_photoMedia) {
		checkPhoto();
	} else if (_documentMedia) {
		checkDocument();
	}
}

void MediaLoader::checkPhoto() {
	const auto size = (_part == MediaPart::Full)
		? Data::PhotoSize::Large
		: Data::PhotoSize::Thumbnail;
	if (auto bytes = _photoMedia->imageBytes(size); !bytes.isEmpty()) {
		finish(std::move(bytes), QString());
	} else if (const auto image = _photoMedia->image(size)) {
		auto result = QByteArray();
		auto buffer = QBuffer(&result);
		buffer.open(QIODevice::WriteOnly);
		image->original().save(&buffer, "JPEG", 87);
		finish(std::move(result), QString());
	} else if (_photoMedia->owner()->failed(size)) {
		finish(QByteArray(), u"Could not download the photo."_q);
	}
}

void MediaLoader::checkDocument() {
	const auto document = _documentMedia->owner();
	if (_part == MediaPart::Thumbnail) {
		if (const auto thumbnail = _documentMedia->thumbnail()) {
			auto result = QByteArray();
			auto buffer = QBuffer(&result);
			buffer.open(QIODevice::WriteOnly);
			thumbnail->original().save(&buffer, "JPEG", 87);
			finish(std::move(result), QString());
		} else if (!document->hasThumbnail()
			|| document->thumbnailFailed()) {
			finish(QByteArray(), u"This file has no preview image."_q);
		}
		return;
	} else if (document->loading()) {
		return;
	}
	auto bytes = _documentMedia->bytes();
	if (bytes.isEmpty()) {
		const auto path = _tempPath.isEmpty()
			? document->filepath(true)
			: _tempPath;
		auto file = QFile(path);
		if (!path.isEmpty() && file.open(QIODevice::ReadOnly)) {
			bytes = file.readAll();
		}
	}
	if (!bytes.isEmpty()) {
		finish(std::move(bytes), QString());
	} else if (document->cancelled()
		|| document->status == FileDownloadFailed
		|| !_documentMedia->loaded(true)) {
		finish(QByteArray(), u"Could not download the file."_q);
	}
}

void MediaLoader::finish(QByteArray bytes, QString error) {
	if (_finished) {
		return;
	}
	_finished = true;
	if (!_tempPath.isEmpty()) {
		QFile::remove(base::take(_tempPath));
	}
	crl::on_main(this, [=, bytes = std::move(bytes)] {
		if (const auto done = base::take(_done)) {
			done(bytes, error);
		}
	});
}

} // namespace Agent
