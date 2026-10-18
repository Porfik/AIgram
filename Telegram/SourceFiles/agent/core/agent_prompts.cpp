/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_prompts.h"

namespace Agent {

QString SystemPrompt(const PromptContext &context) {
	return QString();
}

QString FormatChatBrief(const ChatBrief &brief) {
	return QString();
}

QString WrapUntrusted(const QString &content) {
	return content;
}

QString HintText(const QString &hint) {
	return hint;
}

} // namespace Agent
