/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_composer.h"

#include "lang/lang_keys.h"
#include "ui/controls/send_button.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"

#include "styles/style_agent.h"
#include "styles/style_chat_helpers.h"

namespace Agent::Ui {

Composer::Composer(QWidget *parent)
: RpWidget(parent)
, _field(
	this,
	st::agentComposerField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_agent_input_placeholder())
, _send(this, st::agentComposerSend)
, _queuedLabel(this, QString(), st::agentQueuedLabel) {
	_queuedLabel->hide();
	_field->setSubmitSettings(Ui::InputSubmitSettings::Enter);
	_field->submits(
	) | rpl::on_next([=] {
		submit();
	}, lifetime());
	_field->heightChanges(
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
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
	_sends.fire(std::move(text));
	_field->setText(QString());
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
	_queuedBubbles.clear();
	for (const auto &text : texts) {
		auto bubble = object_ptr<BubbleWidget>(
			this,
			st::agentQueuedLabel,
			st::agentQueuedBubbleBg,
			st::agentQueuedBubbleRadius,
			st::agentQueuedBubblePadding,
			st::agentFeedMaxTextWidth);
		bubble->setText(text);
		_queuedBubbles.push_back(std::move(bubble));
	}
	if (_queuedBubbles.empty()) {
		_queuedLabel->hide();
	} else {
		_queuedLabel->setText(
			tr::lng_agent_queued(tr::now, lt_count, int(texts.size())));
		_queuedLabel->show();
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

void Composer::focusField() {
	_field->setFocus();
}

int Composer::resizeGetHeight(int newWidth) {
	const auto padding = st::agentComposerPadding;
	const auto inner = newWidth - padding.left() - padding.right();
	const auto sendWidth = st::agentComposerSend.inner.width;
	auto top = padding.top();
	if (!_queuedBubbles.empty()) {
		_queuedLabel->move(padding.left(), top);
		top += _queuedLabel->height();
		for (const auto &bubble : _queuedBubbles) {
			bubble->resizeToNaturalWidth(inner);
			bubble->move(
				padding.left() + inner - bubble->width(),
				top);
			top += bubble->height() + st::agentQueuedBubbleMargin.top();
		}
		top += st::agentQueuedBottomGap;
	}
	const auto rowHeight = std::max(_field->height(), _send->height());
	const auto fieldLeft = padding.left();
	const auto fieldWidth = std::max(
		inner - sendWidth - st::agentComposerRowGap,
		0);
	_field->setGeometry(fieldLeft, top, fieldWidth, rowHeight);
	_send->setGeometry(
		fieldLeft + fieldWidth + st::agentComposerRowGap,
		top + (rowHeight - _send->height()) / 2,
		sendWidth,
		_send->height());
	return top + rowHeight + padding.bottom();
}

} // namespace Agent::Ui