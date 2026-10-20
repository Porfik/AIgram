/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <rpl/lifetime.h>

#include <QtCore/QDateTime>
#include <QtCore/QString>

#include <vector>

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Agent {

struct AgentFolderFile {
	QString name;
	int64 size = 0;
	QDateTime modified;
};

[[nodiscard]] QString AgentFolderPath(not_null<Main::Session*> session);
[[nodiscard]] QString UniqueAgentFilePath(
	not_null<Main::Session*> session,
	const QString &name);
[[nodiscard]] std::vector<AgentFolderFile> ListAgentFolder(
	not_null<Main::Session*> session);
[[nodiscard]] QString ResolveAgentFile(
	not_null<Main::Session*> session,
	const QString &name);

class MediaSaver final : public base::has_weak_ptr {
public:
	MediaSaver(
		not_null<HistoryItem*> item,
		QString path,
		Fn<void(QString error)> done);
	~MediaSaver();

private:
	QString _path;
	Fn<void(QString error)> _done;
	bool _finished = false;
	rpl::lifetime _lifetime;

};

} // namespace Agent
