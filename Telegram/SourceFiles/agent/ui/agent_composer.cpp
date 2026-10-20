/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_composer.h"

#include "lang/lang_keys.h"
#include "ui/controls/send_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"

#include "styles/style_agent.h"
#include "styles/style_agent_attach.h"
#include "styles/style_chat_helpers.h"

namespace Agent::Ui {

class ModelChip final : public RippleButton {
public:
	explicit ModelChip(QWidget *parent);

	void setText(const QString &text);
	void resizeToMaxWidth(int maxWidth);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

private:
	QString _text;
	QString _shown;

};

ModelChip::ModelChip(QWidget *parent)
: RippleButton(parent, st::agentModelChipRipple) {
}

void ModelChip::setText(const QString &text) {
	_text = text;
	resizeToMaxWidth(QWIDGETSIZE_MAX);
}

void ModelChip::resizeToMaxWidth(int maxWidth) {
	const auto &padding = st::agentModelChipPadding;
	const auto &font = st::agentModelChipFont;
	const auto extra = padding.left()
		+ st::agentModelChipArrowSkip
		+ st::agentModelChipArrow.width()
		+ padding.right();
	const auto available = std::max(maxWidth - extra, 1);
	_shown = (font->width(_text) > available)
		? font->elided(_text, available)
		: _text;
	resize(extra + font->width(_shown), st::agentModelChipHeight);
	update();
}

void ModelChip::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto over = isOver() || isDown();
	if (over) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::agentModelChipBgOver);
		p.drawRoundedRect(
			rect(),
			st::agentModelChipRadius,
			st::agentModelChipRadius);
	}
	paintRipple(p, 0, 0);
	const auto &padding = st::agentModelChipPadding;
	const auto &font = st::agentModelChipFont;
	p.setFont(font);
	p.setPen(over ? st::agentModelChipFgOver : st::agentModelChipFg);
	p.drawTextLeft(
		padding.left(),
		(height() - font->height) / 2,
		width(),
		_shown);
	const auto &arrow = st::agentModelChipArrow;
	arrow.paint(
		p,
		padding.left() + font->width(_shown) + st::agentModelChipArrowSkip,
		(height() - arrow.height()) / 2,
		width());
}

QImage ModelChip::prepareRippleMask() const {
	return RippleAnimation::RoundRectMask(size(), st::agentModelChipRadius);
}

Composer::Composer(QWidget *parent)
: RpWidget(parent)
, _queued(this)
, _model(this)
, _attach(this, st::historyAttach)
, _field(
	this,
	st::historyComposeField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_agent_input_placeholder())
, _send(this, st::historySend) {
	_field->setMinHeight(
		st::historySendSize.height() - 2 * st::historySendPadding);
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	_field->setDocumentMargin(4.);
	_field->setAdditionalMargin(style::ConvertScale(4) - 4);
	_field->setSubmitSettings(Ui::InputSubmitSettings::Enter);
	_field->submits(
	) | rpl::on_next([=] {
		submit();
	}, lifetime());
	_attach->setClickedCallback([=] {
		_attachRequests.fire({});
	});
	_field->heightChanges(
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
	_queued->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		refresh();
	}, lifetime());
	_model->hide();
	_send->setClickedCallback([=] {
		if (_working) {
			_stops.fire({});
		} else {
			submit();
		}
	});
}

void Composer::submit() {
	auto text = _field->getLastText().trimmed();
	if (text.isEmpty()) {
		return;
	}
	_field->setText(QString());
	_sends.fire(std::move(text));
}

void Composer::refresh() {
	resize(width(), resizeGetHeight(width()));
	_heightChanges.fire({});
}

void Composer::setWorking(bool working) {
	_working = working;
	_send->setState({
		.type = working
			? Ui::SendButton::Type::Stop
			: Ui::SendButton::Type::Send,
	});
	_send->setToolTip(working ? tr::lng_agent_stop(tr::now) : QString());
}

void Composer::setQueued(const std::vector<QString> &texts) {
	_queued->clear();
	if (!texts.empty()) {
		_queued->add(
			object_ptr<FlatLabel>(
				_queued,
				tr::lng_agent_queued(tr::now, lt_count, int(texts.size())),
				st::agentQueuedLabel),
			st::agentQueuedLabelMargin);
		for (const auto &text : texts) {
			auto bubble = object_ptr<BubbleWidget>(
				_queued,
				st::agentQueuedTextLabel,
				st::agentQueuedBubbleBg,
				st::agentQueuedBubbleRadius,
				st::agentQueuedBubblePadding,
				st::agentFeedMaxTextWidth);
			bubble->setText(text);
			_queued->add(
				std::move(bubble),
				st::agentQueuedBubbleMargin,
				style::al_topright);
		}
	}
	refresh();
}

rpl::producer<QString> Composer::sends() const {
	return _sends.events();
}

rpl::producer<> Composer::stops() const {
	return _stops.events();
}

rpl::producer<> Composer::heightChanges() const {
	return _heightChanges.events();
}

void Composer::setModel(const QString &title) {
	const auto shown = !title.isEmpty();
	_model->setText(title);
	if (_modelShown != shown) {
		_modelShown = shown;
		_model->setVisible(shown);
		refresh();
	} else if (shown) {
		resizeGetHeight(width());
	}
}

rpl::producer<> Composer::modelRequests() const {
	return _model->clicks() | rpl::to_empty;
}

rpl::producer<> Composer::attachRequests() const {
	return _attachRequests.events();
}

void Composer::setMimeDataHook(InputField::MimeDataHook hook) {
	_field->setMimeDataHook(std::move(hook));
}

void Composer::focusField() {
	_field->setFocus();
}

QString Composer::draft() const {
	return _field->getLastText();
}

void Composer::setDraft(const QString &text) {
	_field->setText(text);
	_field->setCursorPosition(text.size());
}

int Composer::resizeGetHeight(int newWidth) {
	auto top = st::lineWidth;
	if (_queued->count() > 0) {
		const auto &padding = st::agentComposerQueuedPadding;
		_queued->resizeToWidth(std::max(
			newWidth - padding.left() - padding.right(),
			1));
		_queued->moveToLeft(padding.left(), top + padding.top(), newWidth);
		_queued->show();
		top += padding.top() + _queued->height() + padding.bottom();
	} else {
		_queued->hide();
	}
	if (_modelShown) {
		const auto &margin = st::agentModelChipMargin;
		_model->resizeToMaxWidth(newWidth - margin.left() - margin.right());
		top += margin.top();
		_model->moveToLeft(margin.left(), top, newWidth);
		top += _model->height() + margin.bottom();
	}
	const auto sendSize = st::historySendSize;
	const auto fieldLeft = st::historyAttach.width;
	_field->resizeToWidth(
		std::max(newWidth - fieldLeft - sendSize.width(), 1));
	const auto rowHeight = std::max(
		_field->height() + 2 * st::historySendPadding,
		sendSize.height());
	_attach->resize(st::historyAttach.width, st::historyAttach.height);
	_attach->moveToLeft(
		0,
		top + rowHeight - _attach->height(),
		newWidth);
	_field->moveToLeft(
		fieldLeft,
		top + rowHeight - st::historySendPadding - _field->height(),
		newWidth);
	_send->moveToLeft(
		newWidth - sendSize.width(),
		top + rowHeight - sendSize.height(),
		newWidth);
	return top + rowHeight;
}

void Composer::setShadowAtBottom(bool bottom) {
	_shadowAtBottom = bottom;
	update();
}

void Composer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), st::historyComposeAreaBg);
	p.fillRect(
		0,
		_shadowAtBottom ? (height() - st::lineWidth) : 0,
		width(),
		st::lineWidth,
		st::shadowFg);
}

} // namespace Agent::Ui
