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
	record.info.usage = Usage{ 100, 200, 30 };
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
	} else if (a.info.usage.inputTokens != b.info.usage.inputTokens
		|| a.info.usage.outputTokens != b.info.usage.outputTokens
		|| a.info.usage.cachedInputTokens
			!= b.info.usage.cachedInputTokens) {
		return u"usage differs"_q;
	} else if (a.info.usage.cost != b.info.usage.cost) {
		return u"usage cost differs"_q;
	} else if (a.info.usage.hasCost != b.info.usage.hasCost) {
		return u"usage hasCost differs"_q;
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
		|| a.touched != b.touched
		|| a.usage.inputTokens != b.usage.inputTokens
		|| a.usage.outputTokens != b.usage.outputTokens
		|| a.usage.cachedInputTokens != b.usage.cachedInputTokens
		|| a.usage.cost != b.usage.cost
		|| a.usage.hasCost != b.usage.hasCost
		|| a.preview != b.preview
		|| a.previewOutgoing != b.previewOutgoing) {
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

[[nodiscard]] Card SampleCard() {
	auto card = Card();
	card.toolUseId = u"call_9"_q;
	card.kind = CardKind::Delete;
	card.status = CardStatus::Failed;
	card.decided = CardAction::Execute;
	card.targets = {
		CardTarget{
			123456789,
			42,
			u"Чат \"тест\""_q,
			u"превью"_q,
			true,
			u"Flood wait 3s"_q },
		CardTarget{ 987654321, 0, u"Second"_q, QString(), false, QString() },
	};
	card.text = u"текст"_q;
	card.proposedText = u"было"_q;
	card.detail = u"детали"_q;
	card.option = true;
	card.optionAvailable = true;
	card.canAllowForTurn = true;
	card.allowedForTurn = true;
	card.autoApproved = true;
	return card;
}

[[nodiscard]] QString CardsEqual(const Card &a, const Card &b) {
	if (a.toolUseId != b.toolUseId) {
		return u"toolUseId differs"_q;
	} else if (a.kind != b.kind) {
		return u"kind differs"_q;
	} else if (a.status != b.status) {
		return u"status differs"_q;
	} else if (a.decided != b.decided) {
		return u"decided differs"_q;
	} else if (a.targets.size() != b.targets.size()) {
		return u"target count differs"_q;
	}
	for (auto i = 0, count = int(a.targets.size()); i != count; ++i) {
		const auto &x = a.targets[i];
		const auto &y = b.targets[i];
		if (x.peer != y.peer) {
			return u"target peer differs"_q;
		} else if (x.messageId != y.messageId) {
			return u"target messageId differs"_q;
		} else if (x.title != y.title) {
			return u"target title differs"_q;
		} else if (x.preview != y.preview) {
			return u"target preview differs"_q;
		} else if (x.checked != y.checked) {
			return u"target checked differs"_q;
		} else if (x.error != y.error) {
			return u"target error differs"_q;
		}
	}
	if (a.text != b.text) {
		return u"text differs"_q;
	} else if (a.proposedText != b.proposedText) {
		return u"proposedText differs"_q;
	} else if (a.detail != b.detail) {
		return u"detail differs"_q;
	} else if (a.option != b.option) {
		return u"option differs"_q;
	} else if (a.optionAvailable != b.optionAvailable) {
		return u"optionAvailable differs"_q;
	} else if (a.canAllowForTurn != b.canAllowForTurn) {
		return u"canAllowForTurn differs"_q;
	} else if (a.allowedForTurn != b.allowedForTurn) {
		return u"allowedForTurn differs"_q;
	} else if (a.autoApproved != b.autoApproved) {
		return u"autoApproved differs"_q;
	}
	return QString();
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
				stream << qint32(4);
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
		{ u"record_round_trip_with_cost"_q, [] {
			auto record = SampleRecord();
			record.info.usage = Usage{ 100, 200, 30, 1.25, true };
			const auto bytes = SerializeSession(record);
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			const auto diff = RecordsEqual(record, *restored);
			if (!diff.isEmpty()) {
				return diff;
			}
			if (SerializeSession(*restored) != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"record_loads_version2"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(2);
				WriteHandInfo(stream);
				stream << qint32(1);
				stream << qint32(0) << qint32(1);
				WriteHandTextBlock(stream);
				stream << qint32(0) << QString();
				stream << qint32(10) << qint32(20) << qint32(5);
				stream << qint32(0);
				stream << qint32(0);
				stream << qint32(0);
				stream << qint32(0);
			});
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"version 2 failed to load"_q;
			}
			if (restored->info.usage.inputTokens != 10
				|| restored->info.usage.outputTokens != 20
				|| restored->info.usage.cachedInputTokens != 5) {
				return u"version 2 usage lost"_q;
			} else if (restored->info.usage.hasCost
				|| restored->info.usage.cost != 0.) {
				return u"version 2 usage must have no cost"_q;
			}
			return QString();
		} },
		{ u"index_round_trip_with_usage"_q, [] {
			auto info = SessionInfo();
			info.id = u"4444444444444444"_q;
			info.scope = Scope::Account();
			info.title = u"With usage"_q;
			info.usage = Usage{ 10, 20, 5, 0.5, true };
			const auto bytes = SerializeIndex({ info });
			const auto restored = DeserializeIndex(bytes);
			if (!restored || restored->size() != 1) {
				return u"deserialization failed"_q;
			}
			const auto diff = InfosEqual(info, (*restored)[0]);
			if (!diff.isEmpty()) {
				return diff;
			}
			if (SerializeIndex(*restored) != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"index_loads_version1"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(1) << qint32(1);
				WriteHandInfo(stream);
			});
			const auto restored = DeserializeIndex(bytes);
			if (!restored || restored->size() != 1) {
				return u"version 1 failed to load"_q;
			}
			if ((*restored)[0].usage.hasCost
				|| (*restored)[0].usage.inputTokens != 0) {
				return u"version 1 usage must stay zero"_q;
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
		{ u"index_round_trip_with_preview"_q, [] {
			auto info = SessionInfo();
			info.id = u"5555555555555555"_q;
			info.title = u"Preview"_q;
			info.preview = u"Перескажи чат"_q;
			info.previewOutgoing = true;
			const auto bytes = SerializeIndex({ info });
			const auto restored = DeserializeIndex(bytes);
			if (!restored || restored->size() != 1) {
				return u"deserialization failed"_q;
			}
			return InfosEqual(info, (*restored)[0]);
		} },
		{ u"index_loads_version2_without_preview"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(2) << qint32(1);
				WriteHandInfo(stream);
				stream << qint32(1) << qint32(2) << qint32(0)
					<< double(0.) << qint32(0);
			});
			const auto restored = DeserializeIndex(bytes);
			if (!restored || restored->size() != 1) {
				return u"version 2 failed to load"_q;
			} else if (!(*restored)[0].preview.isEmpty()
				|| (*restored)[0].previewOutgoing) {
				return u"version 2 preview must stay empty"_q;
			}
			return QString();
		} },
		{ u"preview_takes_last_visible_text"_q, [] {
			auto record = SessionRecord();
			record.messages.push_back(Message{
				.role = Role::User,
				.blocks = { TextBlock{ u"first"_q } },
			});
			record.messages.push_back(Message{
				.role = Role::Assistant,
				.blocks = {
					TextBlock{ u"**Готово**:\nчат `пересказан`"_q },
					ToolUseBlock{ .id = u"a"_q, .name = u"x"_q },
				},
			});
			record.messages.push_back(Message{
				.role = Role::User,
				.blocks = {
					ToolResultBlock{ .toolUseId = u"a"_q },
					TextBlock{ u"hint"_q, true },
				},
			});
			const auto preview = MakeSessionPreview(record);
			if (preview.text != u"Готово: чат пересказан"_q) {
				return u"wrong text: "_q + preview.text;
			} else if (preview.outgoing) {
				return u"an answer is not outgoing"_q;
			}
			record.messages.push_back(Message{
				.role = Role::User,
				.blocks = { TextBlock{ u"# Ещё раз"_q } },
			});
			const auto mine = MakeSessionPreview(record);
			if (mine.text != u"Ещё раз"_q || !mine.outgoing) {
				return u"the user's message must be outgoing"_q;
			}
			return QString();
		} },
		{ u"preview_caps_and_handles_empty"_q, [] {
			if (!MakeSessionPreview(SessionRecord()).text.isEmpty()) {
				return u"an empty record has no preview"_q;
			}
			auto record = SessionRecord();
			record.messages.push_back(Message{
				.role = Role::Assistant,
				.blocks = { TextBlock{ QString(300, QChar('a')) } },
			});
			if (MakeSessionPreview(record).text.size() != 120) {
				return u"the preview must be capped at 120"_q;
			}
			return QString();
		} },
		{ u"index_reject_future_version"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(4) << qint32(0);
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
		{ u"record_cards_round_trip"_q, [] {
			auto record = SessionRecord();
			for (auto kind = 0; kind <= int(CardKind::Handoff); ++kind) {
				for (auto status = 0; status <= int(CardStatus::NotDone); ++status) {
					for (auto decided = 0; decided <= int(CardAction::Decline); ++decided) {
						auto card = SampleCard();
						card.kind = CardKind(kind);
						card.status = CardStatus(status);
						card.decided = CardAction(decided);
						record.cards.push_back(card);
					}
				}
			}
			record.loadedGroups = { u"folders"_q };
			record.undone = { u"call_8"_q, u"call_7"_q };
			const auto bytes = SerializeSession(record);
			if (bytes.isEmpty()) {
				return u"serialization is empty"_q;
			}
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"deserialization failed"_q;
			}
			if (restored->cards.size() != record.cards.size()) {
				return u"unexpected card count"_q;
			}
			for (auto i = 0, count = int(record.cards.size()); i != count; ++i) {
				const auto diff = CardsEqual(record.cards[i], restored->cards[i]);
				if (!diff.isEmpty()) {
					return u"card "_q + QString::number(i) + u": "_q + diff;
				}
			}
			if (restored->loadedGroups != record.loadedGroups) {
				return u"loadedGroups differ"_q;
			}
			if (restored->undone != record.undone) {
				return u"undone differ"_q;
			}
			if (SerializeSession(*restored) != bytes) {
				return u"re-serialization is not byte-identical"_q;
			}
			return QString();
		} },
		{ u"record_reject_truncated_v2"_q, [] {
			auto record = SessionRecord();
			record.cards = { SampleCard() };
			record.loadedGroups = { u"folders"_q };
			record.undone = { u"call_8"_q };
			const auto bytes = SerializeSession(record);
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
		{ u"record_loads_version1"_q, [] {
			const auto bytes = Written([](QDataStream &stream) {
				stream << qint32(1);
				WriteHandInfo(stream);
				stream << qint32(1);
				stream << qint32(0) << qint32(1);
				WriteHandTextBlock(stream);
				WriteHandTail(stream, 0);
			});
			const auto restored = DeserializeSession(bytes);
			if (!restored) {
				return u"version 1 failed to load"_q;
			}
			if (restored->messages.size() != 1
				|| restored->messages[0].blocks.size() != 1) {
				return u"version 1 messages lost"_q;
			}
			if (!restored->cards.empty()) {
				return u"version 1 must have no cards"_q;
			}
			if (!restored->loadedGroups.empty()) {
				return u"version 1 must have no loaded groups"_q;
			}
			if (!restored->undone.empty()) {
				return u"version 1 must have no undone"_q;
			}
			return QString();
		} },
		{ u"record_close_interrupted_turn_with_cards"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				Message{ Role::User, { TextBlock{ u"go"_q, false } } },
				Message{ Role::Assistant, {
					ToolUseBlock{
						u"call1"_q,
						u"send_message"_q,
						{},
						QString(),
						{} },
					ToolUseBlock{
						u"call2"_q,
						u"set_mute"_q,
						{},
						QString(),
						{} },
				} },
			};
			auto waiting = Card();
			waiting.toolUseId = u"call1"_q;
			waiting.kind = CardKind::Send;
			waiting.status = CardStatus::Pending;
			auto running = Card();
			running.toolUseId = u"call2"_q;
			running.kind = CardKind::Send;
			running.status = CardStatus::Running;
			running.targets = {
				CardTarget{ 111, 0, u"Alpha"_q, QString(), true, QString() },
			};
			record.cards = { waiting, running };
			if (!CloseInterruptedTurn(record)) {
				return u"expected a change"_q;
			}
			if (record.cards[0].status != CardStatus::NotDone
				|| record.cards[1].status != CardStatus::NotDone) {
				return u"cards must become NotDone"_q;
			}
			if (record.messages.size() != 3) {
				return u"expected one appended message"_q;
			}
			const auto &last = record.messages.back();
			if (last.role != Role::User || last.blocks.size() != 2) {
				return u"unexpected appended message"_q;
			}
			const auto first = std::get_if<ToolResultBlock>(&last.blocks[0]);
			const auto second = std::get_if<ToolResultBlock>(&last.blocks[1]);
			if (!first || !second) {
				return u"expected tool results"_q;
			}
			if (first->toolUseId != u"call1"_q || second->toolUseId != u"call2"_q) {
				return u"results out of order"_q;
			}
			if (!first->content.contains(u"was not performed"_q)
				|| first->error) {
				return u"unexpected first result"_q;
			}
			if (second->error
				|| !second->content.contains(u"was not performed"_q)) {
				return u"unexpected second result"_q;
			}
			if (CloseInterruptedTurn(record)) {
				return u"a repaired record must not change again"_q;
			}
			return QString();
		} },
		{ u"record_close_interrupted_turn_without_cards"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				Message{ Role::Assistant, {
					ToolUseBlock{
						u"call1"_q,
						u"read_history"_q,
						{},
						QString(),
						{} },
				} },
			};
			if (!CloseInterruptedTurn(record)) {
				return u"expected a change"_q;
			}
			if (record.messages.size() != 2) {
				return u"expected an appended result"_q;
			}
			const auto block = std::get_if<ToolResultBlock>(
				&record.messages.back().blocks[0]);
			if (!block
				|| block->toolUseId != u"call1"_q
				|| !block->error
				|| !block->content.contains(u"closed"_q)) {
				return u"unexpected error result"_q;
			}
			return QString();
		} },
		{ u"record_close_interrupted_turn_nothing_open"_q, [] {
			auto record = SessionRecord();
			record.messages = {
				Message{ Role::User, { TextBlock{ u"hi"_q, false } } },
				Message{ Role::Assistant, {
					TextBlock{ u"answer"_q, false },
				} },
			};
			auto card = Card();
			card.toolUseId = u"call1"_q;
			card.status = CardStatus::Done;
			record.cards = { card };
			if (CloseInterruptedTurn(record)) {
				return u"nothing open must not change the record"_q;
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
