/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_memory.h"

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

std::vector<Check> MemoryChecks() {
	return {
		{ u"memory_add_basic_and_ids"_q, [] {
			auto memory = Memory();
			const auto first = MemoryAdd(memory, u"Likes tea"_q);
			const auto second = MemoryAdd(memory, u"Hates spam"_q);
			if (first.error != MemoryError::None
				|| first.fact.id != u"m1"_q
				|| first.fact.text != u"Likes tea"_q
				|| first.index != 0) {
				return u"first add: %1, %2"_q.arg(first.fact.id);
			} else if (second.error != MemoryError::None
				|| second.fact.id != u"m2"_q
				|| second.index != 1) {
				return u"second add: %1, %2"_q.arg(second.fact.id);
			} else if (memory.nextId != 3) {
				return u"nextId: %1"_q.arg(memory.nextId);
			}
			return QString();
		} },
		{ u"memory_add_error_order"_q, [] {
			auto memory = Memory();
			while (memory.facts.size() < kMemoryMaxFacts) {
				const auto added = MemoryAdd(memory, u"fact"_q);
				if (added.error != MemoryError::None) {
					return u"unexpected error while filling"_q;
				}
			}
			if (MemoryAdd(memory, u""_q).error != MemoryError::Empty) {
				return u"empty must win over full"_q;
			}
			const auto longText = QString(kMemoryMaxChars + 1, u'x');
			if (MemoryAdd(memory, longText).error != MemoryError::TooLong) {
				return u"too long must win over full"_q;
			}
			return QString();
		} },
		{ u"memory_add_limits_at_edge"_q, [] {
			auto memory = Memory();
			const auto edge = QString(kMemoryMaxChars, u'y');
			const auto added = MemoryAdd(memory, edge);
			if (added.error != MemoryError::None) {
				return u"%1 characters must fit"_q.arg(kMemoryMaxChars);
			}
			const auto over = QString(kMemoryMaxChars + 1, u'y');
			if (MemoryAdd(memory, over).error != MemoryError::TooLong) {
				return u"%1 characters must be too long"_q
					.arg(kMemoryMaxChars + 1);
			}
			auto full = Memory();
			for (auto i = 0; i != kMemoryMaxFacts - 1; ++i) {
				MemoryAdd(full, u"fact %1"_q.arg(i));
			}
			if (MemoryAdd(full, u"one more"_q).error != MemoryError::None) {
				return u"the 50th fact must fit"_q;
			}
			if (MemoryAdd(full, u"no room"_q).error != MemoryError::Full) {
				return u"the 51st fact must not fit"_q;
			}
			return QString();
		} },
		{ u"memory_add_normalizes_line_breaks"_q, [] {
			auto memory = Memory();
			const auto added = MemoryAdd(
				memory,
				u"  line one\r\nline two\nline three  "_q);
			if (added.error != MemoryError::None) {
				return u"unexpected error"_q;
			}
			const auto expected = u"line one line two line three"_q;
			if (added.fact.text != expected) {
				return u"got: %1"_q.arg(added.fact.text);
			}
			return QString();
		} },
		{ u"memory_add_failed_leaves_memory_unchanged"_q, [] {
			auto memory = Memory();
			MemoryAdd(memory, u"kept"_q);
			const auto before = memory;
			MemoryAdd(memory, u""_q);
			MemoryAdd(memory, QString(kMemoryMaxChars + 1, u'x'));
			auto full = Memory();
			for (auto i = 0; i != kMemoryMaxFacts; ++i) {
				MemoryAdd(full, u"fact %1"_q.arg(i));
			}
			const auto fullBefore = full;
			MemoryAdd(full, u"one more"_q);
			if (memory.facts != before.facts
				|| memory.nextId != before.nextId
				|| full.facts != fullBefore.facts
				|| full.nextId != fullBefore.nextId) {
				return u"a failed add changed the memory"_q;
			}
			return QString();
		} },
		{ u"memory_update"_q, [] {
			auto memory = Memory();
			MemoryAdd(memory, u"first"_q);
			MemoryAdd(memory, u"second"_q);
			const auto updated = MemoryUpdate(memory, u"m2"_q, u"renamed"_q);
			if (updated.error != MemoryError::None
				|| updated.fact.id != u"m2"_q
				|| updated.fact.text != u"renamed"_q
				|| updated.index != 1
				|| updated.previousText != u"second"_q) {
				return u"unexpected update result"_q;
			} else if (memory.facts[1].text != u"renamed"_q) {
				return u"the text was not replaced"_q;
			}
			if (MemoryUpdate(memory, u"m9"_q, u"text"_q).error
				!= MemoryError::UnknownId) {
				return u"expected UnknownId"_q;
			}
			if (MemoryUpdate(memory, u"m1"_q, u""_q).error
				!= MemoryError::Empty) {
				return u"expected Empty for an empty text"_q;
			}
			if (MemoryUpdate(
					memory,
					u"m1"_q,
					QString(kMemoryMaxChars + 1, u'x')).error
				!= MemoryError::TooLong) {
				return u"expected TooLong for a long text"_q;
			}
			if (MemoryUpdate(memory, u"m1"_q, u"ok"_q).error
				!= MemoryError::None) {
				return u"a short text must update"_q;
			}
			return QString();
		} },
		{ u"memory_remove"_q, [] {
			auto memory = Memory();
			MemoryAdd(memory, u"first"_q);
			MemoryAdd(memory, u"second"_q);
			const auto removed = MemoryRemove(memory, u"m1"_q);
			if (removed.error != MemoryError::None
				|| removed.fact.id != u"m1"_q
				|| removed.fact.text != u"first"_q
				|| removed.index != 0) {
				return u"unexpected remove result"_q;
			} else if (memory.facts.size() != 1
				|| memory.facts[0].text != u"second"_q) {
				return u"the fact was not removed"_q;
			}
			if (MemoryRemove(memory, u"m1"_q).error
				!= MemoryError::UnknownId) {
				return u"expected UnknownId for a removed id"_q;
			}
			return QString();
		} },
		{ u"memory_remove_never_reuses_ids"_q, [] {
			auto memory = Memory();
			MemoryAdd(memory, u"first"_q);
			MemoryRemove(memory, u"m1"_q);
			const auto added = MemoryAdd(memory, u"again"_q);
			if (added.error != MemoryError::None
				|| added.fact.id != u"m2"_q) {
				return u"id reused after remove: %1"_q.arg(added.fact.id);
			}
			return QString();
		} },
		{ u"memory_restore"_q, [] {
			auto memory = Memory();
			MemoryAdd(memory, u"first"_q);
			MemoryAdd(memory, u"second"_q);
			const auto removed = MemoryRemove(memory, u"m1"_q);
			const auto restored = MemoryRestore(
				memory,
				removed.fact,
				removed.index);
			if (restored.error != MemoryError::None
				|| restored.index != 0
				|| memory.facts[0].text != u"first"_q) {
				return u"unexpected restore result"_q;
			}
			if (MemoryRestore(memory, removed.fact, 0).error
				!= MemoryError::DuplicateId) {
				return u"expected DuplicateId for a present id"_q;
			}
			return QString();
		} },
		{ u"memory_restore_high_id_raises_next_id"_q, [] {
			auto memory = Memory();
			MemoryAdd(memory, u"first"_q);
			const auto restored = MemoryRestore(
				memory,
				MemoryFact{ u"m9"_q, u"high id"_q },
				0);
			if (restored.error != MemoryError::None) {
				return u"unexpected restore error"_q;
			}
			const auto added = MemoryAdd(memory, u"next"_q);
			if (added.fact.id != u"m10"_q) {
				return u"nextId not raised above a restored high id: %1"_q
					.arg(added.fact.id);
			}
			return QString();
		} },
		{ u"memory_restore_full_and_clamped_index"_q, [] {
			auto memory = Memory();
			for (auto i = 0; i != kMemoryMaxFacts; ++i) {
				MemoryAdd(memory, u"fact %1"_q.arg(i));
			}
			if (MemoryRestore(memory, MemoryFact{ u"m99"_q, u"high"_q }, 0)
				.error != MemoryError::Full) {
				return u"expected Full at the limit"_q;
			}
			auto small = Memory();
			MemoryAdd(small, u"only"_q);
			const auto restored = MemoryRestore(
				small,
				MemoryFact{ u"m5"_q, u"clamped"_q },
				100);
			if (restored.error != MemoryError::None
				|| restored.index != 1
				|| small.facts[1].text != u"clamped"_q) {
				return u"expected the index clamped to the list"_q;
			}
			return QString();
		} },
		{ u"memory_error_text"_q, [] {
			if (!MemoryErrorText(MemoryError::None).isEmpty()) {
				return u"None must have no text"_q;
			}
			const auto checks = std::vector<std::pair<
				MemoryError,
				QString>>{
				{
					MemoryError::Empty,
					u"The fact is empty."_q },
				{
					MemoryError::TooLong,
					u"The fact is longer than 300 characters; "
					u"make it shorter."_q },
				{
					MemoryError::Full,
					u"Memory is full (50 facts); update or remove "
					u"facts first."_q },
				{
					MemoryError::UnknownId,
					u"There is no fact with this id."_q },
				{
					MemoryError::DuplicateId,
					u"A fact with this id already exists."_q },
			};
			for (const auto &[error, expected] : checks) {
				if (MemoryErrorText(error) != expected) {
					return u"text of %1: %2"_q
						.arg(int(error)).arg(MemoryErrorText(error));
				}
			}
			return QString();
		} },
		{ u"memory_format"_q, [] {
			auto memory = Memory();
			if (!FormatMemory(memory).isEmpty()) {
				return u"an empty memory must format to an empty string"_q;
			}
			MemoryAdd(memory, u"first fact"_q);
			MemoryAdd(memory, u"second fact"_q);
			const auto expected = u"- [m1] first fact\n- [m2] second fact"_q;
			if (FormatMemory(memory) != expected) {
				return u"got: %1"_q.arg(FormatMemory(memory));
			}
			return QString();
		} },
		{ u"memory_book_round_trip"_q, [] {
			auto book = MemoryBook();
			auto chat = Memory();
			MemoryAdd(chat, u"chat fact one"_q);
			MemoryAdd(chat, u"chat fact two"_q);
			MemoryRemove(chat, u"m1"_q);
			book.emplace_or_assign(PeerKey(123), std::move(chat));
			auto general = Memory();
			MemoryAdd(general, u"general fact"_q);
			book.emplace_or_assign(PeerKey(0), std::move(general));
			const auto bytes = SerializeMemoryBook(book);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeMemoryBook(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			auto restoredBook = *restored;
			if (restoredBook.size() != 2
				|| restoredBook[123].facts[0].id != u"m2"
				|| restoredBook[123].facts[0].text != u"chat fact two"_q
				|| restoredBook[123].nextId != 3
				|| restoredBook[0].facts[0].text != u"general fact"_q) {
				return u"unexpected restored book"_q;
			}
			const auto again = SerializeMemoryBook(*restored);
			if (again != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"memory_book_skips_untouched_owners"_q, [] {
			auto book = MemoryBook();
			auto cleared = Memory();
			cleared.nextId = 5;
			book.emplace_or_assign(PeerKey(1), std::move(cleared));
			auto fresh = Memory();
			book.emplace_or_assign(PeerKey(2), std::move(fresh));
			const auto bytes = SerializeMemoryBook(book);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeMemoryBook(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			auto restoredBook = *restored;
			if (restoredBook.size() != 1
				|| restoredBook.find(PeerKey(2)) != restoredBook.end()
				|| restoredBook[PeerKey(1)].nextId != 5
				|| !restoredBook[PeerKey(1)].facts.empty()) {
				return u"untouched owners must be skipped"_q;
			}
			if (SerializeMemoryBook(*restored) != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"memory_book_rejects_bad_data"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(),
				Written([](QDataStream &stream) {
					stream << quint32(2) << qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(-1);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(1) << quint64(1)
						<< qint32(0) << qint32(kMemoryMaxFacts + 1);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(1) << quint64(1)
						<< qint32(0) << qint32(1) << u"m1"_q << u"text"_q;
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(1) << quint64(1)
						<< qint32(-3) << qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream << quint32(1) << qint32(1) << quint64(1)
						<< qint32(1) << qint32(1) << u"m1"_q;
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeMemoryBook(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
