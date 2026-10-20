/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_attachments.h"

#include "agent/core/agent_media.h"
#include "layout/layout_document_generic_preview.h"
#include "ui/grouped_layout.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/widgets/labels.h"
#include "styles/style_agent.h"
#include "styles/style_agent_attach.h"
#include "styles/style_chat.h"
#include "styles/style_overview.h"

#include <QtGui/QPainterPath>

namespace Agent::Ui {

class FeedAttachmentsWidget final : public FeedItemWidget {
public:
	FeedAttachmentsWidget(
		QWidget *parent,
		const FeedContext &context,
		const QString &text,
		const std::vector<MediaBlock> &attachments,
		int maxWidth);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Cell {
		QImage source;
		QPixmap prepared;
		bool video = false;
	};
	struct Document {
		Layout::DocumentGenericPreview generic;
		QString name;
		QString status;
	};

	void splitBlocks(const std::vector<MediaBlock> &attachments);
	void prepareCells();

	FeedContext _context;
	int _maxWidth = 0;
	bool _hasText = false;
	std::vector<Cell> _cells;
	std::vector<Document> _docs;
	std::vector<QRect> _cellRects;
	std::vector<QRect> _docRects;
	object_ptr<Ui::FlatLabel> _label;

};

FeedAttachmentsWidget::FeedAttachmentsWidget(
		QWidget *parent,
		const FeedContext &context,
		const QString &text,
		const std::vector<MediaBlock> &attachments,
		int maxWidth)
: FeedItemWidget(parent)
, _context(context)
, _maxWidth(maxWidth)
, _hasText(!text.isEmpty())
, _label(this, QString(), st::agentFeedUserLabel) {
	splitBlocks(attachments);
	_label->setText(text);
	setNaturalWidth(_maxWidth);
	resizeToWidth(_maxWidth);
}

void FeedAttachmentsWidget::splitBlocks(
		const std::vector<MediaBlock> &attachments) {
	const auto &padding = st::agentFeedUserPadding;
	const auto inner = std::max(
		_maxWidth - padding.left() - padding.right(),
		1);
	const auto sourceLimit = 2 * std::max(inner, st::agentAttachSingleMaxHeight);
	for (const auto &source : attachments) {
		auto block = source;
		if (block.bytes.isEmpty() && !block.attachment.isEmpty()) {
			auto read = _context.delegate.attachment(block.attachment);
			if (!read) {
				block.bytes = QByteArray();
			} else {
				block = *read;
			}
		}
		if (block.kind == Agent::MediaKind::Image
			&& !block.bytes.isEmpty()) {
			auto image = Images::Read({ .content = block.bytes }).image;
			if (!image.isNull()) {
				if (const auto side = std::max(
						image.width(),
						image.height()); side > sourceLimit) {
					image = image.scaled(
						sourceLimit,
						sourceLimit,
						Qt::KeepAspectRatio,
						Qt::SmoothTransformation);
				}
				_cells.push_back({ .source = std::move(image) });
				continue;
			}
		} else if (block.kind == Agent::MediaKind::Video
			&& !block.bytes.isEmpty()) {
			_cells.push_back({ .video = true });
			continue;
		}
		_docs.push_back({
			.generic = Layout::DocumentGenericPreview::Create(block.name),
			.name = block.name,
			.status = Ui::FormatSizeText(block.size),
		});
	}
}

void FeedAttachmentsWidget::prepareCells() {
	const auto dpr = style::DevicePixelRatio();
	for (auto i = 0; i != int(_cells.size()); ++i) {
		auto &cell = _cells[i];
		const auto &rect = _cellRects[i];
		if (cell.video) {
			continue;
		}
		const auto pixelSize = QSize(
			rect.width() * dpr,
			rect.height() * dpr);
		if (cell.prepared.size() == pixelSize) {
			continue;
		}
		const auto sourceRatio = double(cell.source.width())
			/ cell.source.height();
		const auto targetRatio = double(rect.width()) / rect.height();
		const auto width = cell.source.width();
		const auto height = cell.source.height();
		const auto crop = (sourceRatio > targetRatio)
			? QRect((width - int(height * targetRatio)) / 2, 0,
				int(height * targetRatio), height)
			: QRect(0, (height - int(width / targetRatio)) / 2,
				width, int(width / targetRatio));
		auto scaled = cell.source.copy(crop).scaled(
			pixelSize,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		auto prepared = PixmapFromImage(
			Images::Round(std::move(scaled), ImageRoundRadius::Large));
		prepared.setDevicePixelRatio(dpr);
		cell.prepared = std::move(prepared);
	}
}

int FeedAttachmentsWidget::resizeGetHeight(int newWidth) {
	const auto &padding = st::agentFeedUserPadding;
	const auto skip = st::agentAttachCellSkip;
	const auto inner = std::max(
		newWidth - padding.left() - padding.right(),
		1);
	_cellRects.clear();
	_docRects.clear();
	auto y = padding.top();
	if (_cells.size() == 1) {
		const auto &cell = _cells.front();
		const auto ratio = cell.source.isNull()
			? 1.0
			: double(cell.source.height()) / cell.source.width();
		const auto height = std::clamp(
			int(inner * ratio),
			st::agentAttachSingleMinHeight,
			st::agentAttachSingleMaxHeight);
		_cellRects.push_back({ padding.left(), y, inner, height });
		y += height + skip;
	} else if (!_cells.empty()) {
		auto sizes = std::vector<QSize>();
		for (const auto &cell : _cells) {
			sizes.push_back((cell.source.isNull() || cell.video)
				? QSize(16, 9)
				: cell.source.size());
		}
		const auto layout = Ui::LayoutMediaGroup(
			sizes,
			inner,
			std::min(inner, st::historyGroupWidthMin),
			st::historyGroupSkip);
		auto bottom = 0;
		for (const auto &part : layout) {
			_cellRects.push_back(
				part.geometry.translated(padding.left(), y));
			bottom = std::max(bottom, part.geometry.y()
				+ part.geometry.height());
		}
		y += bottom + skip;
	}
	prepareCells();
	const auto &layout = st::attachPreviewLayout;
	for (auto i = 0; i != int(_docs.size()); ++i) {
		if (i) {
			y += st::agentAttachRowSkip;
		}
		_docRects.push_back({ padding.left(), y, inner, layout.thumbSize });
		y += layout.thumbSize;
	}
	if (!_docs.empty()) {
		y += skip;
	}
	if (_hasText) {
		if (!_cells.empty() || !_docs.empty()) {
			y += st::agentAttachTextSkip - skip;
		}
		_label->resizeToWidth(inner);
		_label->moveToLeft(padding.left(), y, newWidth);
		y += _label->height();
	}
	return y + padding.bottom();
}

void FeedAttachmentsWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::agentFeedUserBubbleBg);
	p.drawRoundedRect(
		rect(),
		st::agentFeedUserRadius,
		st::agentFeedUserRadius);

	for (auto i = 0; i != int(_cells.size()); ++i) {
		const auto &cell = _cells[i];
		const auto &rect = _cellRects[i];
		if (cell.video) {
			const auto radius = st::agentAttachCellRadius;
			QPainterPath path;
			path.addRoundedRect(rect, radius, radius);
			p.save();
			p.setClipPath(path);
			p.fillRect(rect, st::agentAttachVideoBg);
			p.restore();
			st::videoIcon.paintInCenter(p, rect);
		} else {
			p.drawPixmap(rect.topLeft(), cell.prepared);
		}
	}
	const auto &layout = st::attachPreviewLayout;
	for (auto i = 0; i != int(_docs.size()); ++i) {
		const auto &document = _docs[i];
		const auto &rect = _docRects[i];
		const auto thumb = QRect(
			rect.topLeft(),
			QSize(layout.thumbSize, layout.thumbSize));
		p.setPen(Qt::NoPen);
		p.setBrush(document.generic.color);
		p.drawRoundedRect(thumb, st::roundRadiusSmall, st::roundRadiusSmall);
		if (const auto ext = document.generic.ext; !ext.isEmpty()) {
			p.setFont(st::overviewFileExtFont);
			p.setPen(st::overviewFileExtFg);
			const auto w = st::overviewFileExtFont->width(ext);
			p.drawText(
				thumb.left() + (thumb.width() - w) / 2,
				thumb.top()
					+ (thumb.height() - st::overviewFileExtFont->height) / 2
					+ st::overviewFileExtFont->ascent,
				ext);
		}
		const auto nameLeft = thumb.right() + layout.thumbSkip;
		p.setFont(st::semiboldFont);
		p.setPen(st::agentAttachDocFg);
		p.drawTextLeft(
			nameLeft,
			rect.top() + layout.nameTop,
			width(),
			st::semiboldFont->elided(
				document.name,
				rect.right() - nameLeft + 1,
				Qt::ElideMiddle));
		p.setFont(st::normalFont);
		p.setPen(st::agentAttachDocStatusFg);
		p.drawTextLeft(
			nameLeft,
			rect.top() + layout.statusTop,
			width(),
			document.status);
	}
}

object_ptr<FeedItemWidget> MakeFeedAttachments(
		QWidget *parent,
		const FeedContext &context,
		const QString &text,
		const std::vector<MediaBlock> &attachments,
		int maxWidth) {
	return object_ptr<FeedAttachmentsWidget>(
		parent,
		context,
		text,
		attachments,
		maxWidth);
}

} // namespace Agent::Ui