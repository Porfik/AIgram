/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_record.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QRegularExpression>

namespace Agent::Tests {
namespace {

[[nodiscard]] QByteArray Written(Fn<void(QDataStream &)> write) {
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	write(stream);
	return bytes;
}

[[nodiscard]] SessionRecord SampleRecord() {
	auto record = SessionRecord();
	record.info.id = u"aabbccdd00112233"_q;
	record.info.scope = Scope::Chat(12345);
	record.info.title = u"Тестовая сессия"_q;
	record.info.titleFinal = true;
	record.info.created = 1234567890;
	record.info.updated = 1234567900;
	record.info.touched = { 1, 2, 3 };
	record.messages = {
		Message{ Role::User, {
			TextBlock{ u"Привет, мир"_q, false },
			ImageBlock{ QByteArray("png-bytes"), u"image/png"_q },
			ToolResultBlock{ u"call_0"_q, u"результат"_q, true },
		} },
		Message{ Role::Assistant, {
			TextBlock{ u"скрытая подсказка"_q, true },
			ThinkingBlock{ u"thinking text"_q, QByteArray("signature") },
			TextBlock{ u"Answer text"_q, false },
			ToolUseBlock{
				u"call_1"_q,
				u"read_history"_q,
				QJsonObject{ { u"peer"_q, 42 } },
				u"input error"_q,
				QByteArray("raw input") },
		} },
	};
	record.summaryUntil = 1;
	record.summary = u"Краткое содержание"_q;
	record.usage = Usage{ 100, 200, 30 };
	record.tainted = true;
	return record;
}

[[nodiscard]] bool BlocksEqual(const Block &a, const Block &b) {
	auto result = false;
	std::visit([&](const auto &value) {
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, TextBlock>) {
			const auto other = std::get_if<TextBlock>(&b);
			result = other
				&& other->text == value.text
				&& other->hidden == value.hidden;
		} else if constexpr (std::is_same_v<T, ImageBlock>) {
			const auto other = std::get_if<ImageBlock>(&b);
			result = other
				&& other->bytes == value.bytes
				&& other->mimeType == value.mimeType;
		} else if constexpr (std::is_same_v<T, ThinkingBlock>) {
			const auto other = std::get_if<ThinkingBlock>(&b);
			result = other
				&& other->text == value.text
				&& other->signature == value.signature;
		} else if constexpr (std::is_same_v<T, ToolUseBlock>) {
			const auto other = std::get_if<ToolUseBlock>(&b);
			result = other
				&& other->id == value.id
				&& other->name == value.name
				&& other->input == value.input
				&& other->inputError == value.inputError
				&& other->rawInput == value.rawInput;
		} else {
			const auto other = std::get_if<ToolResultBlock>(&b);
			result = other
				&& other->toolUseId == value.toolUseId
				&& other->content == value.content
				&& other->error == value.error;
		}
	}, a);
	return result;
}

[[nodiscard]] QString RecordsEqual(
		const SessionRecord &a,
		const SessionRecord &b) {
	if (a.info.id != b.info.id) {
		return u"id differs"_q;
	} else if (a.info.scope != b.info.scope) {
		return u"scope differs"_q;
	} else if (a.info.title != b.info.title) {
		return u"title differs"_q;
	} else if (a.info.titleFinal != b.info.titleFinal) {
		return u"titleFinal differs"_q;
	} else if (a.info.created != b.info.created) {
		return u"created differs"_q;
	} else if (a.info.updated != b.info.updated) {
		return u"updated differs"_q;
	} else if (a.info.touched != b.info.touched) {
		return u"touched differs"_q;
	} else if (a.summaryUntil != b.summaryUntil) {
		return u"summaryUntil differs"_q;
	} else if (a.summary != b.summary) {
		return u"summary differs"_q;
	} else if (a.usage.inputTokens != b.usage.inputTokens
		|| a.usage.outputTokens != b.usage.outputTokens
		|| a.usage.cachedInputTokens != b.usage.cachedInputTokens) {
		return u"usage differs"_q;
	} else if (a.tainted != b.tainted) {
		return u"tainted differs"_q;
	} else if (a.messages.size() != b.messages.size()) {
		return u"message count differs"_q;
	}
	for (auto i = 0, count = int(a.messages.size()); i != count; ++i) {
		const auto &x = a.messages[i];
		const auto &y = b.messages[i];
		if (x.role != y.role) {
			return u"message role differs"_q;
		} else if (x.blocks.size() != y.blocks.size()) {
			return u"block count differs"_q;
		}
		for (auto j = 0, blocks = int(x.blocks.size()); j != blocks; ++j) {
			if (!BlocksEqual(x.blocks[j], y.blocks[j])) {
				return u"block "_q + QString::number(i) + u":"_q
					+ QString::number(j) + u" differs"_q;
			}
		}
	}
	return QString();
}

[[nodiscard]] QString InfosEqual(const SessionInfo &a, const SessionInfo &b) {
	if (a.id != b.id
		|| a.scope != b.scope
		|| a.title != b.title
		|| a.titleFinal != b.titleFinal
		|| a.created != b.created
		|| a.updated != b.updated
		|| a.touched != b.touched) {
		return u"info differs"_q;
	}
	return QString();
}

void WriteHandInfo(QDataStream &stream) {
	stream
		<< QString()
		<< qint32(0)
		<< quint64(0)
		<< QString()
		<< qint32(0)
		<< qint64(0)
		<< qint64(0)
		<< qint32(0);
}

void WriteHandTail(QDataStream &stream, qint32 summaryUntil) {
	stream << summaryUntil << QString();
	stream << qint32(0) << qint32(0) << qint32(0);
	stream << qint32(0);
}

void WriteHandTextBlock(QDataStream &stream) {
	stream << quint8(1) << u"текст"_q << qint32(0);
}

} // namespace

std::vector<Check> RecordChecks() {
	return {
		{ u"record_round_trip"_q, [] {
			const auto record = SampleRecord();
			const auto bytes = SerializeSession(record);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			const auto diff = RecordsEqual(record, *restored);
			if (!diff.isEmpty()) {
				return diff;
			}
			const auto again = SerializeSession(*restored);
			if (again != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"record_round_trip_empty"_q, [] {
			const auto record = SessionRecord();
			const auto bytes = SerializeSession(record);
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"empty record failed to deserialize"_q;
			}
			const auto diff = RecordsEqual(record, *restored);
			if (!diff.isEmpty()) {
				return diff;
			}
			return QString();
		} },
		{ u"record_round_trip_cyrillic"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				Message{ Role::User, {
					TextBlock{ u"Привет"_q, false },
				} },
				Message{ Role::Assistant, {
					TextBlock{ u"Пока"_q, false },
				} },
			};
			const auto bytes = SerializeSession(record);
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"cyrillic record failed to deserialize"_q;
			}
			const auto diff = RecordsEqual(record, *restored);
			if (!diff.isEmpty()) {
				return diff;
			}
			return QString();
		} },
		{ u"record_reject_future_version"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(2);
				WriteHandInfo(stream);
			});
			if (DeserializeSession(bytes)) {
				return u"expected nullopt for a future version"_q;
			}
			return QString();
		} },
		{ u"record_reject_truncated_at_every_byte"_q, [] {
			auto small = SessionRecord();
			small.messages = {
				Message{ Role::User, {
					TextBlock{ u"hi"_q, false },
				} },
			};
			const auto bytes = SerializeSession(small);
			if (!DeserializeSession(bytes)) {
				return u"the full record failed to deserialize"_q;
			}
			for (auto size = 0; size != bytes.size(); ++size) {
				if (DeserializeSession(bytes.mid(0, size))) {
					return u"expected nullopt for truncation at "_q
						+ QString::number(size);
				}
			}
			return QString();
		} },
		{ u"record_reject_unknown_block_tag"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(1);
				WriteHandInfo(stream);
				stream << qint32(1);
				stream << qint32(0) << qint32(1);
				stream << quint8(9);
			});
			if (DeserializeSession(bytes)) {
				return u"expected nullopt for an unknown block tag"_q;
			}
			return QString();
		} },
		{ u"record_reject_invalid_summary_until"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(1);
				WriteHandInfo(stream);
				stream << qint32(1);
				stream << qint32(0) << qint32(1);
				WriteHandTextBlock(stream);
				WriteHandTail(stream, 2);
			});
			if (DeserializeSession(bytes)) {
				return u"expected nullopt for summaryUntil above the end"_q;
			}
			return QString();
		} },
		{ u"record_reject_negative_summary_until"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(1);
				WriteHandInfo(stream);
				stream << qint32(0);
				WriteHandTail(stream, -1);
			});
			if (DeserializeSession(bytes)) {
				return u"expected nullopt for a negative summaryUntil"_q;
			}
			return QString();
		} },
		{ u"record_reject_invalid_json_input"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(1);
				WriteHandInfo(stream);
				stream << qint32(1);
				stream << qint32(1) << qint32(1);
				stream << quint8(4);
				stream << u"call_1"_q;
				stream << u"tool"_q;
				stream << QByteArray("not json");
				stream << QString();
				stream << QByteArray();
			});
			if (DeserializeSession(bytes)) {
				return u"expected nullopt for invalid json input"_q;
			}
			return QString();
		} },
		{ u"record_reject_bad_roles_and_flags"_q, [] {
			const auto bad = std::vector<QByteArray>{
				Written([](QDataStream &stream) {
					stream << qint32(1);
					WriteHandInfo(stream);
					stream << qint32(1);
					stream << qint32(7) << qint32(0);
				}),
				Written([](QDataStream &stream) {
					stream << qint32(1);
					WriteHandInfo(stream);
					stream << qint32(1);
					stream << qint32(0) << qint32(1);
					stream << quint8(1) << u"текст"_q << qint32(2);
				}),
				Written([](QDataStream &stream) {
					stream << qint32(1);
					WriteHandInfo(stream);
					stream << qint32(0);
					stream << qint32(0) << QString();
					stream << qint32(0) << qint32(0) << qint32(0);
					stream << qint32(2);
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeSession(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
		{ u"record_reject_bad_scope_and_title_final"_q, [] {
			const auto bad = std::vector<QByteArray>{
				Written([](QDataStream &stream) {
					stream << qint32(1);
					stream << QString() << qint32(7) << quint64(0);
					stream << QString() << qint32(0);
					stream << qint64(0) << qint64(0) << qint32(0);
					stream << qint32(0);
					WriteHandTail(stream, 0);
				}),
				Written([](QDataStream &stream) {
					stream << qint32(1);
					stream << QString() << qint32(-1) << quint64(0);
					stream << QString() << qint32(0);
					stream << qint64(0) << qint64(0) << qint32(0);
					stream << qint32(0);
					WriteHandTail(stream, 0);
				}),
				Written([](QDataStream &stream) {
					stream << qint32(1);
					stream << QString() << qint32(0) << quint64(0);
					stream << QString() << qint32(2);
					stream << qint64(0) << qint64(0) << qint32(0);
					stream << qint32(0);
					WriteHandTail(stream, 0);
				}),
			};
			for (const auto &bytes : bad) {
				if (DeserializeSession(bytes)) {
					return u"expected nullopt for: "_q + bytes.toBase64();
				}
			}
			return QString();
		} },
		{ u"index_round_trip"_q, [] {
			auto first = SessionInfo();
			first.id = u"1111111111111111"_q;
			first.scope = Scope::Account();
			first.title = u"First"_q;
			first.created = 100;
			first.updated = 200;
			auto second = SessionInfo();
			second.id = u"2222222222222222"_q;
			second.scope = Scope::Chat(777);
			second.title = u"Второй"_q;
			second.titleFinal = true;
			second.created = 300;
			second.updated = 400;
			second.touched = { 5, 6 };
			const auto list = std::vector<SessionInfo>{ first, second };
			const auto bytes = SerializeIndex(list);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeIndex(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			if (restored->size() != 2) {
				return u"unexpected entry count"_q;
			}
			for (auto i = 0; i != 2; ++i) {
				const auto diff = InfosEqual(list[i], (*restored)[i]);
				if (!diff.isEmpty()) {
					return diff;
				}
			}
			return QString();
		} },
		{ u"index_empty_list"_q, [] {
			const auto bytes = SerializeIndex({});
			const auto restored = DeserializeIndex(bytes);
			if (!restored) {
				return u"empty list failed to deserialize"_q;
			}
			if (!restored->empty()) {
				return u"expected an empty list"_q;
			}
			return QString();
		} },
		{ u"index_reject_future_version"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(2) << qint32(0);
			});
			if (DeserializeIndex(bytes)) {
				return u"expected nullopt for a future version"_q;
			}
			return QString();
		} },
		{ u"index_reject_truncated"_q, [] {
			auto info = SessionInfo();
			info.id = u"3333333333333333"_q;
			const auto bytes = SerializeIndex({ info });
			if (!DeserializeIndex(bytes)) {
				return u"the full index failed to deserialize"_q;
			}
			const auto bad = std::vector<QByteArray>{
				bytes.mid(0, bytes.size() - 1),
				bytes.mid(0, bytes.size() / 2),
				bytes + QByteArray("x"),
			};
			for (const auto &truncated : bad) {
				if (DeserializeIndex(truncated)) {
					return u"expected nullopt for: "_q
						+ truncated.toBase64();
				}
			}
			return QString();
		} },
		{ u"record_generate_session_id"_q, [] {
			const auto id = GenerateSessionId();
			if (id.size() != 16) {
				return u"unexpected id length: "_q
					+ QString::number(id.size());
			}
			const auto allowed = QRegularExpression(
				u"^[0-9a-f]{16}$"_q);
			if (!allowed.match(id).hasMatch()) {
				return u"not 16 lowercase hex digits: "_q + id;
			}
			if (GenerateSessionId() == id) {
				return u"two generated ids are equal"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
