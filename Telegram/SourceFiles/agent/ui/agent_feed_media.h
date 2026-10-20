/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/ui/agent_feed_items.h"

namespace Agent::Ui {

[[nodiscard]] object_ptr<RpWidget> MakeFeedMediaPreview(
	QWidget *parent,
	const FeedContext &context,
	const MediaBlock &media);

} // namespace Agent::Ui
