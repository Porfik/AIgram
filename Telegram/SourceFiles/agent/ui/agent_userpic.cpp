/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_userpic.h"

#include "ui/painter.h"
#include "styles/style_agent_general.h"

namespace Agent::Ui {

void PaintAgentUserpic(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;
	auto bg = QLinearGradient(x, y, x, y + size);
	bg.setStops({
		{ 0., st::historyPeer5UserpicBg->c },
		{ 1., st::historyPeer5UserpicBg2->c },
	});
	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(x, y, size, size);

	const auto &icon = st::agentUserpicIcon;
	const auto scale = size / float64(st::agentUserpicIconBase);
	p.save();
	p.translate(x + size / 2., y + size / 2.);
	p.scale(scale, scale);
	icon.paint(p, -icon.width() / 2, -icon.height() / 2, icon.width());
	p.restore();
}

} // namespace Agent::Ui
