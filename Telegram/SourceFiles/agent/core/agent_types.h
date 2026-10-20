/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <crl/crl_time.h>

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <optional>
#include <variant>
#include <vector>

namespace Agent {

enum class Role : uchar {
	User,
	Assistant,
};

struct TextBlock {
	QString text;
	bool hidden = false;
};

enum class MediaKind : uchar {
	Image,
	Audio,
	Video,
	File,
};

struct MediaBlock {
	MediaKind kind = MediaKind::Image;
	QByteArray bytes;
	QString mimeType;
	QString name;
	QString attachment;
	uint64 peer = 0;
	int64 messageId = 0;
	int64 size = 0;
	int duration = 0;
};

struct ThinkingBlock {
	QString text;
	QByteArray signature;
};

struct ToolUseBlock {
	QString id;
	QString name;
	QJsonObject input;
	QString inputError;
	QByteArray rawInput;
};

struct ToolResultBlock {
	QString toolUseId;
	QString content;
	bool error = false;
	std::vector<MediaBlock> media;
};

using Block = std::variant<
	TextBlock,
	MediaBlock,
	ThinkingBlock,
	ToolUseBlock,
	ToolResultBlock>;

struct Message {
	Role role = Role::User;
	std::vector<Block> blocks;
};

struct ToolSpec {
	QString name;
	QString description;
	QJsonObject inputSchema;
};

struct Usage {
	int inputTokens = 0;
	int outputTokens = 0;
	int cachedInputTokens = 0;
	double cost = 0.;
	bool hasCost = false;

	Usage &operator+=(const Usage &other) {
		inputTokens += other.inputTokens;
		outputTokens += other.outputTokens;
		cachedInputTokens += other.cachedInputTokens;
		cost += other.cost;
		hasCost = hasCost || other.hasCost;
		return *this;
	}
};

enum class StopReason : uchar {
	EndTurn,
	ToolUse,
	MaxTokens,
	Refusal,
};

struct Request {
	QString model;
	QString system;
	std::vector<Message> messages;
	std::vector<ToolSpec> tools;
	int maxOutputTokens = 0;
	QString pdfEngine;
};

enum class ErrorKind : uchar {
	Network,
	Auth,
	RateLimited,
	Overloaded,
	BadRequest,
	BadResponse,
};

struct Error {
	ErrorKind kind = ErrorKind::Network;
	int httpStatus = 0;
	QString text;
	crl::time retryAfter = 0;
};

[[nodiscard]] QString ErrorKindName(ErrorKind kind);

} // namespace Agent
