/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"

#include <QtCore/QString>

#include <optional>

namespace Agent {

struct ChatBrief {
	PeerKey peer = 0;
	QString title;
	QString kind;
	QString username;
	int members = 0;
	QString role;
	int unread = 0;
	bool canWrite = false;
};

enum class PromptFamily : uchar {
	Generic,
	Glm,
};

[[nodiscard]] PromptFamily PromptFamilyForModel(const QString &model);

struct PromptContext {
	Scope scope;
	QString account;
	QString instructions;
	QString memory;
	std::optional<ChatBrief> chat;
	QString catalog;
	PromptFamily family = PromptFamily::Generic;
};

struct TaskPromptContext {
	Scope scope;
	QString account;
	QString instructions;
	QString title;
	QString task;
	QString frame;
	std::optional<ChatBrief> chat;
	QString catalog;
	PromptFamily family = PromptFamily::Generic;
};

[[nodiscard]] QString SystemPrompt(const PromptContext &context);
[[nodiscard]] QString TaskSystemPrompt(const TaskPromptContext &context);
[[nodiscard]] QString FormatChatBrief(const ChatBrief &brief);
[[nodiscard]] QString FormatChatCounters(const ChatBrief &brief);
[[nodiscard]] QString WrapUntrusted(const QString &content);
[[nodiscard]] QString UnwrapUntrusted(const QString &content);
[[nodiscard]] QString HintText(const QString &hint);

} // namespace Agent
