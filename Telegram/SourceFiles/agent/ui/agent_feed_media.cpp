/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_media.h"

#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "layout/layout_document_generic_preview.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"

#include "styles/style_agent_media.h"

namespace Agent::Ui {
namespace {

class FeedMediaPreview final : public Ui::AbstractButton {
public:
	FeedMediaPreview(
		QWidget *parent,
		const FeedContext &context,
		const MediaBlock &media);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void openViewer();

	FeedContext _context;
	MediaBlock _media;
	QImage _image;
	std::shared_ptr<Ui::DynamicImage> _thumbnail;
	PhotoData *_photo = nullptr;
	DocumentData *_document = nullptr;
	FullMsgId _fullId;
	QString _fileName;

};

FeedMediaPreview::FeedMediaPreview(
	QWidget *parent,
	const FeedContext &context,
	const MediaBlock &media)
: Ui::AbstractButton(parent)
, _context(std::move(context))
, _media(media) {
	setFixedSize(st::agentMediaThumbSize, st::agentMediaThumbSize);
	if (!_media.attachment.isEmpty()) {
		auto block = _context.delegate.attachment(_media.attachment);
		if (block && block->kind == MediaKind::Image) {
			_fileName = block->name;
			const auto ratio = style::DevicePixelRatio();
			const auto side = st::agentMediaThumbSize * ratio;
			auto image = QImage::fromData(block->bytes);
			if (!image.isNull()) {
				image = image.scaled(
					side,
					side,
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation);
				_image = image.copy(
					(image.width() - side) / 2,
					(image.height() - side) / 2,
					side,
					side);
				_image.setDevicePixelRatio(ratio);
			}
		}
	} else if (_media.peer && _media.messageId) {
		const auto session = &_context.controller->session();
		_fullId = FullMsgId(PeerId(_media.peer), MsgId(_media.messageId));
		const auto item = session->data().message(_fullId);
		if (item && item->media()) {
			if (const auto photo = item->media()->photo()) {
				_photo = photo;
				_thumbnail = Ui::MakePhotoThumbnail(photo, _fullId);
			} else if (const auto document = item->media()->document()) {
				_document = document;
				_thumbnail = Ui::MakeDocumentThumbnail(document, _fullId);
			}
		}
		if (_thumbnail) {
			_thumbnail->subscribeToUpdates([=] { update(); });
		}
	}
	if (_photo || _document) {
		setClickedCallback([=] { openViewer(); });
	}
}

void FeedMediaPreview::openViewer() {
	const auto controller = _context.controller;
	if (_photo) {
		controller->openPhoto(_photo, { _fullId });
	} else if (_document) {
		controller->openDocument(_document, true, { _fullId });
	}
}

void FeedMediaPreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	auto path = QPainterPath();
	path.addRoundedRect(
		rect(),
		st::agentMediaThumbRadius,
		st::agentMediaThumbRadius);
	p.save();
	p.setClipPath(path);
	p.setPen(Qt::NoPen);
	p.fillRect(rect(), st::agentMediaThumbBg);
	if (!_image.isNull()) {
		p.drawImage(0, 0, _image);
	} else if (_thumbnail) {
		p.drawImage(0, 0, _thumbnail->image(width()));
	} else {
		const auto generic = Layout::DocumentGenericPreview::Create(
			_fileName);
		const auto &icon = *generic.icon();
		icon.paint(
			p,
			QPoint(
				(width() - icon.width()) / 2,
				st::agentMediaThumbIconTop),
			width());
		p.setPen(st::agentMediaThumbIcon);
		p.setFont(st::agentMediaThumbExtFont);
		p.drawText(
			QRect(0, st::agentMediaThumbExtTop, width(), height()),
			generic.ext,
			style::al_top);
	}
	p.restore();
}

} // namespace

object_ptr<RpWidget> MakeFeedMediaPreview(
		QWidget *parent,
		const FeedContext &context,
		const MediaBlock &media) {
	if (!media.attachment.isEmpty()) {
		const auto block = context.delegate.attachment
			? context.delegate.attachment(media.attachment)
			: std::nullopt;
		if (!block
			|| block->kind != MediaKind::Image
			|| QImage::fromData(block->bytes).isNull()) {
			return nullptr;
		}
	} else if (media.peer && media.messageId) {
		const auto item = context.controller->session().data().message(
			FullMsgId(PeerId(media.peer), MsgId(media.messageId)));
		const auto itemMedia = item ? item->media() : nullptr;
		const auto document = itemMedia ? itemMedia->document() : nullptr;
		if (!itemMedia
			|| (!itemMedia->photo()
				&& !(document && document->hasThumbnail()))) {
			return nullptr;
		}
	} else {
		return nullptr;
	}
	return object_ptr<FeedMediaPreview>(parent, context, media);
}

} // namespace Agent::Ui