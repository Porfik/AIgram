/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_items.h"

#include "agent/ui/agent_feed_format.h"
#include "ui/painter.h"

#include "styles/style_agent.h"

namespace Agent::Ui {
namespace {

class CodeBox final : public RpWidget {
public:
	CodeBox(QWidget *parent, const QString &text);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<FlatLabel> _label;

};

CodeBox::CodeBox(QWidget *parent, const QString &text)
: RpWidget(parent)
, _label(this, QString(), st::agentStepsCodeLabel) {
	_label->setMarkedText(Monospace(text));
	_label->setSelectable(true);
}

int CodeBox::resizeGetHeight(int newWidth) {
	const auto &padding = st::agentStepsCodePadding;
	_label->resizeToWidth(
		std::max(newWidth - padding.left() - padding.right(), 1));
	_label->moveToLeft(padding.left(), padding.top(), newWidth);
	return padding.top() + _label->height() + padding.bottom();
}

void CodeBox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::agentStepsCodeBg);
	const auto radius = st::agentStepsCodeRadius;
	p.drawRoundedRect(rect(), radius, radius);
}

} // namespace

object_ptr<RpWidget> MakeCodeBlock(QWidget *parent, const QString &text) {
	return object_ptr<CodeBox>(parent, text);
}

} // namespace Agent::Ui
