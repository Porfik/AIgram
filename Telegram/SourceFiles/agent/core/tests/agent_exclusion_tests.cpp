/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_exclusion_list.h"

#include <QtCore/QDataStream>

namespace Agent::Tests {
namespace {

[[nodiscard]] QByteArray Written(Fn<void(QDataStream &)> write) {
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	write(stream);
	return bytes;
}

} // namespace

std::vector<Check> ExclusionChecks() {
	return {
		{ u"exclusions_peer_set"_q, [] {
			auto list = ExclusionList();
			if (!SetPeerExcluded(list, 10, true)) {
				return u"adding a peer must change the list"_q;
			} else if (SetPeerExcluded(list, 10, true)) {
				return u"adding the same peer twice must not change"_q;
			} else if (list.peers != std::vector<PeerKey>{ 10 }) {
				return u"unexpected peers"_q;
			}
			if (!SetPeerExcluded(list, 10, false)) {
				return u"removing a peer must change the list"_q;
			} else if (SetPeerExcluded(list, 10, false)) {
				return u"removing an absent peer must not change"_q;
			} else if (!list.peers.empty()) {
				return u"the peer was not removed"_q;
			}
			return QString();
		} },
		{ u"exclusions_folder_set"_q, [] {
			auto list = ExclusionList();
			if (!SetFolderExcluded(list, 3, true)) {
				return u"adding a folder must change the list"_q;
			} else if (SetFolderExcluded(list, 3, true)) {
				return u"adding the same folder twice must not change"_q;
			} else if (list.folders != std::vector<int>{ 3 }) {
				return u"unexpected folders"_q;
			}
			if (!SetFolderExcluded(list, 3, false)) {
				return u"removing a folder must change the list"_q;
			} else if (SetFolderExcluded(list, 3, false)) {
				return u"removing an absent folder must not change"_q;
			} else if (!list.folders.empty()) {
				return u"the folder was not removed"_q;
			}
			return QString();
		} },
		{ u"exclusions_zero_ids_never_change"_q, [] {
			auto list = ExclusionList();
			if (SetPeerExcluded(list, 0, true)
				|| SetPeerExcluded(list, 0, false)) {
				return u"peer 0 must never change the list"_q;
			}
			if (SetFolderExcluded(list, 0, true)
				|| SetFolderExcluded(list, 0, false)) {
				return u"folder 0 must never change the list"_q;
			}
			if (!list.peers.empty() || !list.folders.empty()) {
				return u"the list must stay empty"_q;
			}
			return QString();
		} },
		{ u"exclusions_keep_hide_order_and_round_trip"_q, [] {
			auto list = ExclusionList();
			SetPeerExcluded(list, 30, true);
			SetPeerExcluded(list, 10, true);
			SetPeerExcluded(list, 20, true);
			SetPeerExcluded(list, 10, false);
			SetPeerExcluded(list, 10, true);
			SetFolderExcluded(list, 2, true);
			SetFolderExcluded(list, 1, true);
			const auto expectedPeers = std::vector<PeerKey>{ 30, 20, 10 };
			if (list.peers != expectedPeers) {
				return u"peers must keep the order they were hidden"_q;
			}
			const auto expectedFolders = std::vector<int>{ 2, 1 };
			if (list.folders != expectedFolders) {
				return u"folders must keep the order they were hidden"_q;
			}
			const auto bytes = SerializeExclusions(list);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeExclusions(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			if (restored->peers != list.peers
				|| restored->folders != list.folders) {
				return u"unexpected restored list"_q;
			}
			if (SerializeExclusions(*restored) != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"exclusions_load_drops_duplicates_and_zero"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << quint32(1);
				stream << qint32(6) << quint64(10) << quint64(10)
					<< quint64(0) << quint64(5) << quint64(3)
					<< quint64(5);
				stream << qint32(2) << qint32(5) << qint32(5);
			});
			const auto restored = DeserializeExclusions(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			if (restored->peers != std::vector<PeerKey>{ 10, 5, 3 }) {
				return u"duplicate and zero peers must be dropped"_q;
			} else if (restored->folders != std::vector<int>{ 5 }) {
				return u"duplicate folders must be dropped"_q;
			}
			return QString();
		} },
		{ u"exclusions_reject_bad_data"_q, [] {
			const auto good = SerializeExclusions(
				ExclusionList{ .peers = { 10 }, .folders = { 5 } });
			const auto bad = std::vector<QByteArray>{
				QByteArray(),
				good.mid(0, good.size() - 1),
				good + QByteArray("x"),
				Written([](QDataStream &stream) {
					stream << quint32(2) << qint32(0) << qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(-1) << qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(0) << qint32(-1);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(1) << quint64(10);
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeExclusions(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
