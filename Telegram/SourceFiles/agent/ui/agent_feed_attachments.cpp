/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_attachments.h"

namespace Agent::Ui {

object_ptr<RpWidget> MakeFeedAttachments(
		QWidget *parent,
		const FeedContext &context,
		const std::vector<MediaBlock> &attachments,
		int maxWidth) {
	return object_ptr<RpWidget>(parent);
}

} // namespace Agent::Ui
