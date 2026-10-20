/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_media_loader.h"

#include "crl/crl_on_main.h"
#include "history/history_item.h"

namespace Agent {

std::optional<MessageMedia> FindMessageMedia(not_null<HistoryItem*> item) {
	return std::nullopt;
}

MediaLoader::MediaLoader(
	not_null<HistoryItem*> item,
	MediaPart part,
	Fn<void(QByteArray bytes, QString error)> done)
: _done(std::move(done)) {
	crl::on_main(this, [=] {
		_done(QByteArray(), u"Not implemented yet."_q);
	});
}

MediaLoader::~MediaLoader() = default;

} // namespace Agent
