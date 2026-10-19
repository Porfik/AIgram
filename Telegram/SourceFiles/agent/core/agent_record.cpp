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

constexpr auto kFormatVersion = qint32(1);
constexpr auto kMaxMessages = 1'000'000;
constexpr auto kMaxBlocks = 1'000'000;
constexpr auto kMaxTouched = 10'000;
constexpr auto kMaxSessions = 100'000;

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
		<< qint32(usage.cachedInputTokens);
}

std::optional<Usage> ReadUsage(QDataStream &stream) {
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
	return Usage{ input, output, cached };
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
	stream << kFormatVersion;
	WriteInfo(stream, record.info);
	stream << qint32(record.messages.size());
	for (const auto &message : record.messages) {
		WriteMessage(stream, message);
	}
	stream << qint32(record.summaryUntil) << record.summary;
	WriteUsage(stream, record.usage);
	stream << qint32(record.tainted ? 1 : 0);
	return result;
}

std::optional<SessionRecord> DeserializeSession(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok || version != kFormatVersion) {
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
	auto usage = ReadUsage(stream);
	if (!usage) {
		return std::nullopt;
	}
	record.usage = *usage;
	auto tainted = qint32(0);
	stream >> tainted;
	if (stream.status() != QDataStream::Ok
		|| tainted < 0
		|| tainted > 1
		|| !stream.atEnd()) {
		return std::nullopt;
	}
	record.tainted = (tainted != 0);
	return record;
}

QByteArray SerializeIndex(const std::vector<SessionInfo> &list) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kFormatVersion << qint32(list.size());
	for (const auto &info : list) {
		WriteInfo(stream, info);
	}
	return result;
}

std::optional<std::vector<SessionInfo>> DeserializeIndex(
		const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok || version != kFormatVersion) {
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
		list.push_back(std::move(*info));
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return list;
}

bool CloseInterruptedTurn(SessionRecord &record) {
	return false;
}

} // namespace Agent
