/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_file_origin.h"

#include <rpl/lifetime.h>

#include <QtCore/QDateTime>
#include <QtCore/QString>

#include <vector>

class HistoryItem;

namespace Data {
class PhotoMedia;
class DocumentMedia;
} // namespace Data

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
	void check();
	void finish(QString error);

	QString _path;
	Data::FileOrigin _origin;
	Fn<void(QString error)> _done;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	bool _started = false;
	bool _finished = false;
	rpl::lifetime _lifetime;

};

} // namespace Agent
