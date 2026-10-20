/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_files.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "crl/crl_on_main.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Agent {
namespace {

const auto kFolderName = u"AIgram Agent"_q;

[[nodiscard]] QString CleanFileName(const QString &name) {
	auto result = QString();
	result.reserve(name.size());
	for (const auto ch : name) {
		if (ch == u'/' || ch == u'\\' || ch.unicode() < 32) {
			continue;
		}
#ifdef Q_OS_WIN
		if (QStringView(u":*?\"<>|").contains(ch)) {
			continue;
		}
#endif // Q_OS_WIN
		result.append(ch);
	}
	while (result.startsWith(u'.')) {
		result.remove(0, 1);
	}
	result = result.trimmed();
	return result.isEmpty() ? u"file"_q : result;
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &bytes) {
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(bytes) == bytes.size());
}

} // namespace

QString AgentFolderPath(not_null<Main::Session*> session) {
	auto base = Core::App().settings().downloadPath();
	if (base.isEmpty() || base == FileDialog::Tmp()) {
		base = File::DefaultDownloadPath(session);
	}
	if (!base.endsWith(u'/')) {
		base += u'/';
	}
	const auto result = base + kFolderName + u'/';
	QDir().mkpath(result);
	return result;
}

QString UniqueAgentFilePath(
		not_null<Main::Session*> session,
		const QString &name) {
	const auto folder = AgentFolderPath(session);
	const auto clean = CleanFileName(name);
	const auto dot = clean.lastIndexOf(u'.');
	const auto base = (dot > 0) ? clean.mid(0, dot) : clean;
	const auto extension = (dot > 0) ? clean.mid(dot) : QString();
	auto result = folder + clean;
	for (auto i = 1; QFileInfo::exists(result); ++i) {
		result = folder + base + u" (%1)"_q.arg(i) + extension;
	}
	return result;
}

std::vector<AgentFolderFile> ListAgentFolder(
		not_null<Main::Session*> session) {
	auto result = std::vector<AgentFolderFile>();
	const auto entries = QDir(AgentFolderPath(session)).entryInfoList(
		QDir::Files | QDir::NoDotAndDotDot,
		QDir::Time);
	for (const auto &info : entries) {
		result.push_back({
			.name = info.fileName(),
			.size = info.size(),
			.modified = info.lastModified(),
		});
	}
	return result;
}

QString ResolveAgentFile(
		not_null<Main::Session*> session,
		const QString &name) {
	if (name.isEmpty()
		|| name.contains(u'/')
		|| name.contains(u'\\')
		|| name == u"."_q
		|| name == u".."_q) {
		return QString();
	}
	const auto info = QFileInfo(AgentFolderPath(session) + name);
	return (info.isFile() && !info.isSymLink())
		? info.absoluteFilePath()
		: QString();
}

MediaSaver::MediaSaver(
	not_null<HistoryItem*> item,
	QString path,
	Fn<void(QString error)> done)
: _path(std::move(path))
, _origin(Data::FileOriginMessage(item->fullId()))
, _done(std::move(done)) {
	const auto media = item->media();
	const auto photo = media ? media->photo() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (!photo && !document) {
		finish(u"The message has no photo or file."_q);
		return;
	}
	const auto session = &item->history()->session();
	rpl::merge(
		session->downloaderTaskFinished(),
		session->data().documentLoadProgress() | rpl::to_empty
	) | rpl::on_next([=] {
		check();
	}, _lifetime);
	if (photo) {
		_photoMedia = photo->createMediaView();
		_photoMedia->wanted(Data::PhotoSize::Large, _origin);
	} else {
		_documentMedia = document->createMediaView();
		if (!_documentMedia->loaded(true) && !document->loading()) {
			_started = true;
			document->save(_origin, _path);
		}
	}
	check();
}

MediaSaver::~MediaSaver() {
	if (_finished) {
		return;
	}
	if (_started && _documentMedia && _documentMedia->owner()->loading()) {
		_documentMedia->owner()->cancel();
	}
	QFile::remove(_path);
}

void MediaSaver::check() {
	if (_finished) {
		return;
	} else if (_photoMedia) {
		const auto bytes = _photoMedia->imageBytes(Data::PhotoSize::Large);
		if (!bytes.isEmpty()) {
			finish(WriteFile(_path, bytes)
				? QString()
				: u"Could not write the file."_q);
		} else if (_photoMedia->owner()->failed(Data::PhotoSize::Large)) {
			finish(u"Could not download the photo."_q);
		}
		return;
	}
	const auto document = _documentMedia->owner();
	if (document->loading()) {
		return;
	} else if (_started) {
		finish((QFileInfo(_path).isFile() && !document->cancelled())
			? QString()
			: u"Could not download the file."_q);
		return;
	}
	const auto bytes = _documentMedia->bytes();
	if (!bytes.isEmpty()) {
		finish(WriteFile(_path, bytes)
			? QString()
			: u"Could not write the file."_q);
		return;
	}
	const auto from = document->filepath(true);
	if (from.isEmpty()) {
		_started = true;
		document->save(_origin, _path);
		check();
		return;
	}
	QFile::remove(_path);
	finish(QFile::copy(from, _path)
		? QString()
		: u"Could not copy the file."_q);
}

void MediaSaver::finish(QString error) {
	if (_finished) {
		return;
	}
	_finished = true;
	if (!error.isEmpty()) {
		QFile::remove(_path);
	}
	crl::on_main(this, [=] {
		if (const auto done = base::take(_done)) {
			done(error);
		}
	});
}

} // namespace Agent
