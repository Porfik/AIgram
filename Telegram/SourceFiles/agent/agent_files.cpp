/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_files.h"

#include "crl/crl_on_main.h"
#include "history/history_item.h"

namespace Agent {

QString AgentFolderPath(not_null<Main::Session*> session) {
	return QString();
}

QString UniqueAgentFilePath(
		not_null<Main::Session*> session,
		const QString &name) {
	return QString();
}

std::vector<AgentFolderFile> ListAgentFolder(
		not_null<Main::Session*> session) {
	return {};
}

QString ResolveAgentFile(
		not_null<Main::Session*> session,
		const QString &name) {
	return QString();
}

MediaSaver::MediaSaver(
	not_null<HistoryItem*> item,
	QString path,
	Fn<void(QString error)> done)
: _path(std::move(path))
, _done(std::move(done)) {
	crl::on_main(this, [=] {
		_done(u"Not implemented yet."_q);
	});
}

MediaSaver::~MediaSaver() = default;

} // namespace Agent
