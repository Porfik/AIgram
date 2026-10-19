/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_exclusion_list.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

#include <algorithm>

namespace Agent {
namespace {

constexpr auto kExclusionsVersion = quint32(1);
constexpr auto kMaxEntries = 1'000'000;

} // namespace

bool SetPeerExcluded(ExclusionList &list, PeerKey peer, bool excluded) {
	if (peer == 0) {
		return false;
	}
	const auto it = std::find(
		list.peers.begin(),
		list.peers.end(),
		peer);
	if (excluded == (it != list.peers.end())) {
		return false;
	} else if (excluded) {
		list.peers.push_back(peer);
	} else {
		list.peers.erase(it);
	}
	return true;
}

bool SetFolderExcluded(ExclusionList &list, int folder, bool excluded) {
	if (folder == 0) {
		return false;
	}
	const auto it = std::find(
		list.folders.begin(),
		list.folders.end(),
		folder);
	if (excluded == (it != list.folders.end())) {
		return false;
	} else if (excluded) {
		list.folders.push_back(folder);
	} else {
		list.folders.erase(it);
	}
	return true;
}

QByteArray SerializeExclusions(const ExclusionList &list) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kExclusionsVersion;
	stream << qint32(list.peers.size());
	for (const auto peer : list.peers) {
		stream << quint64(peer);
	}
	stream << qint32(list.folders.size());
	for (const auto folder : list.folders) {
		stream << qint32(folder);
	}
	return result;
}

std::optional<ExclusionList> DeserializeExclusions(
		const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version != kExclusionsVersion) {
		return std::nullopt;
	}
	auto list = ExclusionList();
	auto peerCount = qint32(0);
	stream >> peerCount;
	if (stream.status() != QDataStream::Ok
		|| peerCount < 0
		|| peerCount > kMaxEntries) {
		return std::nullopt;
	}
	list.peers.reserve(peerCount);
	for (auto i = 0; i != peerCount; ++i) {
		auto peer = quint64(0);
		stream >> peer;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		const auto it = std::find(
			list.peers.begin(),
			list.peers.end(),
			peer);
		if (peer != 0 && it == list.peers.end()) {
			list.peers.push_back(peer);
		}
	}
	auto folderCount = qint32(0);
	stream >> folderCount;
	if (stream.status() != QDataStream::Ok
		|| folderCount < 0
		|| folderCount > kMaxEntries) {
		return std::nullopt;
	}
	list.folders.reserve(folderCount);
	for (auto i = 0; i != folderCount; ++i) {
		auto folder = qint32(0);
		stream >> folder;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		const auto it = std::find(
			list.folders.begin(),
			list.folders.end(),
			folder);
		if (folder != 0 && it == list.folders.end()) {
			list.folders.push_back(folder);
		}
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return list;
}

} // namespace Agent
