/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;

namespace Agent::Ui {

void PaintAgentUserpic(
	QPainter &p,
	int x,
	int y,
	int outerWidth,
	int size);

} // namespace Agent::Ui
