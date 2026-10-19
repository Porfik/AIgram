/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_record.h"

#include "base/random.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Agent {
namespace {

constexpr auto kSessionVersion = qint32(3);
constexpr auto kIndexVersion = qint32(3);
constexpr auto kPreviewMaxChars = 120;
constexpr auto kMaxMessages = 1'000'000;
constexpr auto kMaxBlocks = 1'000'000;
constexpr auto kMaxTouched = 10'000;
constexpr auto kMaxSessions = 100'000;
constexpr auto kMaxCards = 10'000;
constexpr auto kMaxTargets = 1'000;
constexpr auto kMaxGroups = 100;
constexpr auto kMaxUndone = 10'000;

enum class BlockTag : quint8 {
	Text = 1,
	Image = 2,
	Thinking = 3,
	ToolUse = 4,
	ToolResult = 5,
};

void WriteBlock(QDataStream &stream, const TextBlock &block) {
	stream << quint8(BlockTag::Text) << block.text << qint32(block.hidden);
}

void WriteBlock(QDataStream &stream, const ImageBlock &block) {
	stream
		<< quint8(BlockTag::Image)
		<< block.bytes
		<< block.mimeType;
}

void WriteBlock(QDataStream &stream, const ThinkingBlock &block) {
	stream << quint8(BlockTag::Thinking) << block.text << block.signature;
}

void WriteBlock(QDataStream &stream, const ToolUseBlock &block) {
	stream
		<< quint8(BlockTag::ToolUse)
		<< block.id
		<< block.name
		<< QJsonDocument(block.input).toJson(QJsonDocument::Compact)
		<< block.inputError
		<< block.rawInput;
}

void WriteBlock(QDataStream &stream, const ToolResultBlock &block) {
	stream
		<< quint8(BlockTag::ToolResult)
		<< block.toolUseId
		<< block.content
		<< qint32(block.error ? 1 : 0);
}

void WriteMessage(QDataStream &stream, const Message &message) {
	stream << qint32(message.role) << qint32(message.blocks.size());
	for (const auto &block : message.blocks) {
		std::visit([&](const auto &value) {
			WriteBlock(stream, value);
		}, block);
	}
}

std::optional<Block> ReadBlock(QDataStream &stream) {
	auto tag = quint8(0);
	stream >> tag;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	switch (BlockTag(tag)) {
	case BlockTag::Text: {
		auto text = QString();
		auto hidden = qint32(0);
		stream >> text >> hidden;
		if (stream.status() != QDataStream::Ok || hidden < 0 || hidden > 1) {
			return std::nullopt;
		}
		return TextBlock{ text, (hidden != 0) };
	}
	case BlockTag::Image: {
		auto bytes = QByteArray();
		auto mimeType = QString();
		stream >> bytes >> mimeType;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		return ImageBlock{ bytes, mimeType };
	}
	case BlockTag::Thinking: {
		auto text = QString();
		auto signature = QByteArray();
		stream >> text >> signature;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		return ThinkingBlock{ text, signature };
	}
	case BlockTag::ToolUse: {
		auto bytes = QByteArray();
		auto block = ToolUseBlock();
		stream
			>> block.id
			>> block.name
			>> bytes
			>> block.inputError
			>> block.rawInput;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		auto document = QJsonDocument::fromJson(bytes);
		if (!document.isObject()) {
			return std::nullopt;
		}
		block.input = document.object();
		return block;
	}
	case BlockTag::ToolResult: {
		auto toolUseId = QString();
		auto content = QString();
		auto error = qint32(0);
		stream >> toolUseId >> content >> error;
		if (stream.status() != QDataStream::Ok || error < 0 || error > 1) {
			return std::nullopt;
		}
		return ToolResultBlock{ toolUseId, content, (error != 0) };
	}
	}
	return std::nullopt;
}

std::optional<Message> ReadMessage(QDataStream &stream) {
	auto role = qint32(0);
	auto count = qint32(0);
	stream >> role >> count;
	if (stream.status() != QDataStream::Ok
		|| role < 0
		|| role > int(Role::Assistant)
		|| count < 0
		|| count > kMaxBlocks) {
		return std::nullopt;
	}
	auto message = Message{ Role(role), {} };
	message.blocks.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto block = ReadBlock(stream);
		if (!block) {
			return std::nullopt;
		}
		message.blocks.push_back(std::move(*block));
	}
	return message;
}

void WriteInfo(QDataStream &stream, const SessionInfo &info) {
	stream
		<< info.id
		<< qint32(info.scope.kind)
		<< quint64(info.scope.peer)
		<< info.title
		<< qint32(info.titleFinal ? 1 : 0)
		<< info.created
		<< info.updated
		<< qint32(info.touched.size());
	for (const auto &peer : info.touched) {
		stream << quint64(peer);
	}
}

std::optional<SessionInfo> ReadInfo(QDataStream &stream) {
	auto scopeKind = qint32(0);
	auto scopePeer = quint64(0);
	auto titleFinal = qint32(0);
	auto touchedCount = qint32(0);
	auto info = SessionInfo();
	stream
		>> info.id
		>> scopeKind
		>> scopePeer
		>> info.title
		>> titleFinal
		>> info.created
		>> info.updated
		>> touchedCount;
	if (stream.status() != QDataStream::Ok
		|| scopeKind < 0
		|| scopeKind > int(ScopeKind::Chat)
		|| titleFinal < 0
		|| titleFinal > 1
		|| touchedCount < 0
		|| touchedCount > kMaxTouched) {
		return std::nullopt;
	}
	info.scope = Scope{ ScopeKind(scopeKind), scopePeer };
	info.titleFinal = (titleFinal != 0);
	info.touched.reserve(touchedCount);
	for (auto i = 0; i != touchedCount; ++i) {
		auto peer = quint64(0);
		stream >> peer;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		info.touched.push_back(peer);
	}
	return info;
}

void WriteUsage(QDataStream &stream, const Usage &usage) {
	stream
		<< qint32(usage.inputTokens)
		<< qint32(usage.outputTokens)
		<< qint32(usage.cachedInputTokens)
		<< usage.cost
		<< qint32(usage.hasCost ? 1 : 0);
}

std::optional<Usage> ReadUsage(QDataStream &stream, bool withCost) {
	auto input = qint32(0);
	auto output = qint32(0);
	auto cached = qint32(0);
	stream >> input >> output >> cached;
	if (stream.status() != QDataStream::Ok
		|| input < 0
		|| output < 0
		|| cached < 0) {
		return std::nullopt;
	}
	auto usage = Usage{ input, output, cached };
	if (withCost) {
		auto hasCost = qint32(0);
		stream >> usage.cost >> hasCost;
		if (stream.status() != QDataStream::Ok
			|| hasCost < 0
			|| hasCost > 1) {
			return std::nullopt;
		}
		usage.hasCost = (hasCost != 0);
	}
	return usage;
}

void WriteCard(QDataStream &stream, const Card &card) {
	stream
		<< card.toolUseId
		<< qint32(card.kind)
		<< qint32(card.status)
		<< qint32(card.decided)
		<< qint32(card.targets.size());
	for (const auto &target : card.targets) {
		stream
			<< quint64(target.peer)
			<< qint64(target.messageId)
			<< target.title
			<< target.preview
			<< qint32(target.checked ? 1 : 0)
			<< target.error;
	}
	stream
		<< card.text
		<< card.proposedText
		<< card.detail
		<< qint32(card.option ? 1 : 0)
		<< qint32(card.optionAvailable ? 1 : 0)
		<< qint32(card.canAllowForTurn ? 1 : 0)
		<< qint32(card.allowedForTurn ? 1 : 0)
		<< qint32(card.autoApproved ? 1 : 0);
}

[[nodiscard]] bool ReadFlags(
		QDataStream &stream,
		bool &option,
		bool &optionAvailable,
		bool &canAllowForTurn,
		bool &allowedForTurn,
		bool &autoApproved) {
	auto first = qint32(0);
	auto second = qint32(0);
	auto third = qint32(0);
	auto fourth = qint32(0);
	auto fifth = qint32(0);
	stream >> first >> second >> third >> fourth >> fifth;
	if (stream.status() != QDataStream::Ok
		|| first < 0 || first > 1
		|| second < 0 || second > 1
		|| third < 0 || third > 1
		|| fourth < 0 || fourth > 1
		|| fifth < 0 || fifth > 1) {
		return false;
	}
	option = (first != 0);
	optionAvailable = (second != 0);
	canAllowForTurn = (third != 0);
	allowedForTurn = (fourth != 0);
	autoApproved = (fifth != 0);
	return true;
}

std::optional<Card> ReadCard(QDataStream &stream) {
	auto toolUseId = QString();
	auto kind = qint32(0);
	auto status = qint32(0);
	auto decided = qint32(0);
	auto targetCount = qint32(0);
	stream >> toolUseId >> kind >> status >> decided >> targetCount;
	if (stream.status() != QDataStream::Ok
		|| kind < 0
		|| kind > int(CardKind::Handoff)
		|| status < 0
		|| status > int(CardStatus::NotDone)
		|| decided < 0
		|| decided > int(CardAction::Decline)
		|| targetCount < 0
		|| targetCount > kMaxTargets) {
		return std::nullopt;
	}
	auto card = Card();
	card.toolUseId = toolUseId;
	card.kind = CardKind(kind);
	card.status = CardStatus(status);
	card.decided = CardAction(decided);
	card.targets.reserve(targetCount);
	for (auto i = 0; i != targetCount; ++i) {
		auto peer = quint64(0);
		auto messageId = qint64(0);
		auto title = QString();
		auto preview = QString();
		auto checked = qint32(0);
		auto error = QString();
		stream >> peer >> messageId >> title >> preview >> checked >> error;
		if (stream.status() != QDataStream::Ok
			|| checked < 0
			|| checked > 1) {
			return std::nullopt;
		}
		card.targets.push_back(CardTarget{
			peer,
			messageId,
			title,
			preview,
			(checked != 0),
			error });
	}
	stream >> card.text >> card.proposedText >> card.detail;
	if (!ReadFlags(
		stream,
		card.option,
		card.optionAvailable,
		card.canAllowForTurn,
		card.allowedForTurn,
		card.autoApproved)) {
		return std::nullopt;
	}
	return card;
}

void WriteStringList(QDataStream &stream, const std::vector<QString> &list) {
	stream << qint32(list.size());
	for (const auto &value : list) {
		stream << value;
	}
}

std::optional<std::vector<QString>> ReadStringList(
		QDataStream &stream,
		int max) {
	auto count = qint32(0);
	stream >> count;
	if (stream.status() != QDataStream::Ok || count < 0 || count > max) {
		return std::nullopt;
	}
	auto list = std::vector<QString>();
	list.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto value = QString();
		stream >> value;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		list.push_back(std::move(value));
	}
	return list;
}

const Card *FindCard(const std::vector<Card> &cards, const QString &id) {
	for (const auto &card : cards) {
		if (card.toolUseId == id) {
			return &card;
		}
	}
	return nullptr;
}

} // namespace

QString GenerateSessionId() {
	return QString::number(base::RandomValue<uint64>(), 16)
		.rightJustified(16, u'0');
}

QByteArray SerializeSession(const SessionRecord &record) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kSessionVersion;
	WriteInfo(stream, record.info);
	stream << qint32(record.messages.size());
	for (const auto &message : record.messages) {
		WriteMessage(stream, message);
	}
	stream << qint32(record.summaryUntil) << record.summary;
	WriteUsage(stream, record.info.usage);
	stream << qint32(record.tainted ? 1 : 0);
	stream << qint32(record.cards.size());
	for (const auto &card : record.cards) {
		WriteCard(stream, card);
	}
	WriteStringList(stream, record.loadedGroups);
	WriteStringList(stream, record.undone);
	return result;
}

std::optional<SessionRecord> DeserializeSession(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kSessionVersion) {
		return std::nullopt;
	}
	auto info = ReadInfo(stream);
	if (!info) {
		return std::nullopt;
	}
	auto count = qint32(0);
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count < 0
		|| count > kMaxMessages) {
		return std::nullopt;
	}
	auto record = SessionRecord{ .info = std::move(*info) };
	record.messages.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto message = ReadMessage(stream);
		if (!message) {
			return std::nullopt;
		}
		record.messages.push_back(std::move(*message));
	}
	auto summaryUntil = qint32(0);
	stream >> summaryUntil >> record.summary;
	if (stream.status() != QDataStream::Ok
		|| summaryUntil < 0
		|| summaryUntil > count) {
		return std::nullopt;
	}
	record.summaryUntil = summaryUntil;
	auto usage = ReadUsage(stream, version >= 3);
	if (!usage) {
		return std::nullopt;
	}
	record.info.usage = *usage;
	auto tainted = qint32(0);
	stream >> tainted;
	if (stream.status() != QDataStream::Ok
		|| tainted < 0
		|| tainted > 1) {
		return std::nullopt;
	}
	record.tainted = (tainted != 0);
	if (version >= 2) {
		auto cardCount = qint32(0);
		stream >> cardCount;
		if (stream.status() != QDataStream::Ok
			|| cardCount < 0
			|| cardCount > kMaxCards) {
			return std::nullopt;
		}
		record.cards.reserve(cardCount);
		for (auto i = 0; i != cardCount; ++i) {
			auto card = ReadCard(stream);
			if (!card) {
				return std::nullopt;
			}
			record.cards.push_back(std::move(*card));
		}
		auto loadedGroups = ReadStringList(stream, kMaxGroups);
		auto undone = ReadStringList(stream, kMaxUndone);
		if (!loadedGroups || !undone) {
			return std::nullopt;
		}
		record.loadedGroups = std::move(*loadedGroups);
		record.undone = std::move(*undone);
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return record;
}

QByteArray SerializeIndex(const std::vector<SessionInfo> &list) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kIndexVersion << qint32(list.size());
	for (const auto &info : list) {
		WriteInfo(stream, info);
		WriteUsage(stream, info.usage);
		stream << info.preview << qint32(info.previewOutgoing ? 1 : 0);
	}
	return result;
}

std::optional<std::vector<SessionInfo>> DeserializeIndex(
		const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kIndexVersion) {
		return std::nullopt;
	}
	auto count = qint32(0);
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count < 0
		|| count > kMaxSessions) {
		return std::nullopt;
	}
	auto list = std::vector<SessionInfo>();
	list.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto info = ReadInfo(stream);
		if (!info) {
			return std::nullopt;
		}
		if (version >= 2) {
			auto usage = ReadUsage(stream, true);
			if (!usage) {
				return std::nullopt;
			}
			info->usage = *usage;
		}
		if (version >= 3) {
			auto outgoing = qint32(0);
			stream >> info->preview >> outgoing;
			if (stream.status() != QDataStream::Ok
				|| outgoing < 0
				|| outgoing > 1) {
				return std::nullopt;
			}
			info->previewOutgoing = (outgoing != 0);
		}
		list.push_back(std::move(*info));
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return list;
}

SessionPreview MakeSessionPreview(const SessionRecord &record) {
	const auto visibleText = [](const Message &message) {
		auto result = QString();
		for (const auto &block : message.blocks) {
			const auto text = std::get_if<TextBlock>(&block);
			if (text && !text->hidden) {
				result += text->text + ' ';
			}
		}
		return result;
	};
	for (auto i = record.messages.rbegin(); i != record.messages.rend(); ++i) {
		auto text = visibleText(*i);
		for (const auto &marker : { u"**"_q, u"__"_q, u"~~"_q, u"`"_q }) {
			text.remove(marker);
		}
		text = text.simplified();
		while (text.startsWith('#') || text.startsWith('>')) {
			text = text.mid(1).trimmed();
		}
		if (text.isEmpty()) {
			continue;
		}
		if (text.size() > kPreviewMaxChars) {
			auto cut = kPreviewMaxChars;
			if (text[cut - 1].isHighSurrogate()) {
				--cut;
			}
			text = text.left(cut);
		}
		return {
			.text = std::move(text),
			.outgoing = (i->role == Role::User),
		};
	}
	return {};
}

bool CloseInterruptedTurn(SessionRecord &record) {
	auto changed = false;
	for (auto &card : record.cards) {
		if (card.status == CardStatus::Pending
			|| card.status == CardStatus::Running) {
			card.status = CardStatus::NotDone;
			changed = true;
		}
	}
	if (record.messages.empty()
		|| record.messages.back().role != Role::Assistant) {
		return changed;
	}
	auto results = std::vector<ToolResultBlock>();
	for (const auto &block : record.messages.back().blocks) {
		const auto use = std::get_if<ToolUseBlock>(&block);
		if (!use) {
			continue;
		}
		const auto card = FindCard(record.cards, use->id);
		if (card) {
			results.push_back(ToolResultBlock{
				use->id,
				CardResultText(*card),
				(card->status == CardStatus::Failed) });
		} else {
			results.push_back(ToolResultBlock{
				use->id,
				u"The app was closed while the tool was running."_q,
				true });
		}
	}
	if (results.empty()) {
		return changed;
	}
	auto blocks = std::vector<Block>();
	blocks.reserve(results.size());
	for (auto &result : results) {
		blocks.push_back(std::move(result));
	}
	record.messages.push_back(Message{ Role::User, std::move(blocks) });
	return true;
}

} // namespace Agent
