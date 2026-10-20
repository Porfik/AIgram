/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_context.h"

#include <QtCore/QJsonDocument>

#include <utility>

namespace Agent {
namespace {

constexpr auto kCompactionThreshold = 0.7;
constexpr auto kCompactionTarget = 0.4;
constexpr auto kSummaryTokenBudget = 2000;
constexpr auto kTranscriptToolResultMaxChars = 2000;
constexpr auto kTitleUserMaxChars = 1500;
constexpr auto kTitleAssistantMaxChars = 500;
constexpr auto kCompactionMaxOutputTokens = 2048;
constexpr auto kTitleMaxOutputTokens = 256;

[[nodiscard]] QString CompactJson(const QJsonObject &object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] bool IsTurnStart(const Message &message) {
	if (message.role != Role::User) {
		return false;
	}
	for (const auto &block : message.blocks) {
		if (std::get_if<TextBlock>(&block)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString CutText(const QString &text, int maxChars) {
	Expects(maxChars > 0);

	if (text.size() <= maxChars) {
		return text;
	}
	auto cut = maxChars;
	if (text[cut - 1].isHighSurrogate() && text[cut].isLowSurrogate()) {
		--cut;
	}
	return text.left(cut);
}

[[nodiscard]] QString FirstVisibleText(const Message &message) {
	for (const auto &block : message.blocks) {
		if (const auto text = std::get_if<TextBlock>(&block)) {
			if (!text->hidden) {
				return text->text;
			}
		}
	}
	return QString();
}

[[nodiscard]] QString TranscriptText(const std::vector<Message> &messages) {
	auto result = QString();
	const auto line = [&](const QString &value) {
		if (!result.isEmpty()) {
			result += '\n';
		}
		result += value;
	};
	for (const auto &message : messages) {
		auto pending = QString();
		const auto flush = [&] {
			if (pending.isEmpty()) {
				return;
			}
			line((message.role == Role::User
				? u"User: %1"_q
				: u"Assistant: %1"_q).arg(pending));
			pending = QString();
		};
		for (const auto &block : message.blocks) {
			if (const auto text = std::get_if<TextBlock>(&block)) {
				if (text->hidden) {
					continue;
				}
				if (!pending.isEmpty()) {
					pending += '\n';
				}
				pending += text->text;
			} else if (const auto use = std::get_if<ToolUseBlock>(&block)) {
				flush();
				line(u"Tool call %1 %2"_q.arg(
					use->name,
					use->inputError.isEmpty()
						? CompactJson(use->input)
						: QString::fromUtf8(use->rawInput)));
			} else if (const auto tool = std::get_if<ToolResultBlock>(&block)) {
				flush();
				line((tool->error
					? u"Tool error: %1"_q
					: u"Tool result: %1"_q).arg(CapToolResult(
						tool->content,
						kTranscriptToolResultMaxChars)));
			}
		}
		flush();
	}
	return result;
}

} // namespace

int EstimateTokens(const QString &text) {
	return (text.size() + 2) / 3;
}

int EstimateTokens(const Message &message) {
	auto result = 4;
	for (const auto &block : message.blocks) {
		if (const auto text = std::get_if<TextBlock>(&block)) {
			result += EstimateTokens(text->text);
		} else if (std::get_if<MediaBlock>(&block)) {
			result += 1000;
		} else if (const auto thinking = std::get_if<ThinkingBlock>(&block)) {
			result += EstimateTokens(thinking->text);
		} else if (const auto use = std::get_if<ToolUseBlock>(&block)) {
			result += EstimateTokens(use->name);
			result += EstimateTokens(use->inputError.isEmpty()
				? CompactJson(use->input)
				: QString::fromUtf8(use->rawInput));
		} else if (const auto tool = std::get_if<ToolResultBlock>(&block)) {
			result += EstimateTokens(tool->content);
		}
	}
	return result;
}

int EstimateTokens(const Request &request) {
	auto result = EstimateTokens(request.system);
	for (const auto &tool : request.tools) {
		result += EstimateTokens(tool.name + tool.description);
		result += EstimateTokens(CompactJson(tool.inputSchema));
	}
	for (const auto &message : request.messages) {
		result += EstimateTokens(message);
	}
	return result;
}

QString CapToolResult(QString content, int maxChars) {
	Expects(maxChars > 0);

	if (content.size() <= maxChars) {
		return content;
	}
	auto cut = maxChars;
	if (content[cut - 1].isHighSurrogate()
		&& content[cut].isLowSurrogate()) {
		--cut;
	}
	return content.left(cut)
		+ u"\n[%1 characters dropped. Request a smaller page.]"_q.arg(
			content.size() - cut);
}

bool DropMediaBytes(SessionRecord &record) {
	return false;
}

std::vector<Message> ContextMessages(const SessionRecord &record) {
	auto result = std::vector<Message>(
		record.messages.begin() + record.summaryUntil,
		record.messages.end());
	if (record.summaryUntil <= 0 || record.summary.isEmpty()) {
		return result;
	}
	const auto block = TextBlock{
		u"Summary of the earlier conversation:\n"_q + record.summary,
		true,
	};
	if (!result.empty() && result.front().role == Role::User) {
		auto &first = result.front();
		first.blocks.insert(first.blocks.begin(), block);
	} else {
		result.insert(result.begin(), Message{ Role::User, { block } });
	}
	return result;
}

int PlanCompaction(
		const SessionRecord &record,
		int fixedTokens,
		int contextWindow) {
	const auto window = (contextWindow > 0)
		? contextWindow
		: kDefaultContextWindow;
	auto contextTokens = 0;
	for (const auto &message : ContextMessages(record)) {
		contextTokens += EstimateTokens(message);
	}
	if (fixedTokens + contextTokens <= int(window * kCompactionThreshold)) {
		return record.summaryUntil;
	}
	const auto count = int(record.messages.size());
	auto suffix = std::vector<int>(count + 1, 0);
	for (auto i = count; i != 0; --i) {
		suffix[i - 1] = suffix[i] + EstimateTokens(record.messages[i - 1]);
	}
	const auto budget = int(window * kCompactionTarget);
	for (auto i = record.summaryUntil + 1; i <= count; ++i) {
		if (i < count && !IsTurnStart(record.messages[i])) {
			continue;
		} else if (fixedTokens + kSummaryTokenBudget + suffix[i] <= budget) {
			return i;
		}
	}
	return count;
}

Request CompactionRequest(
		const SessionRecord &record,
		int until,
		const QString &model) {
	Expects(until >= record.summaryUntil
		&& until <= int(record.messages.size()));

	auto text = QString();
	if (!record.summary.isEmpty()) {
		text += u"Previous summary:\n"_q + record.summary + u"\n\n"_q;
	}
	text += u"Transcript:\n"_q;
	text += TranscriptText(std::vector<Message>(
		record.messages.begin() + record.summaryUntil,
		record.messages.begin() + until));
	return Request{
		model,
		u"Summarize the conversation below for the assistant's own later "
		u"use. Keep the user's goals and preferences, decisions, facts "
		u"about chats and people with their ids, actions taken with "
		u"their results, and open questions. Write in the same language "
		u"as the conversation and use fewer than 400 words. Reply with "
		u"the summary only."_q,
		{ Message{ Role::User, { TextBlock{ std::move(text) } } } },
		{},
		kCompactionMaxOutputTokens,
	};
}

Request TitleRequest(const SessionRecord &record, const QString &model) {
	auto userText = QString();
	auto assistantText = QString();
	for (const auto &message : record.messages) {
		if (userText.isEmpty() && message.role == Role::User) {
			userText = FirstVisibleText(message);
		} else if (assistantText.isEmpty()
			&& message.role == Role::Assistant) {
			assistantText = FirstVisibleText(message);
		}
		if (!userText.isEmpty() && !assistantText.isEmpty()) {
			break;
		}
	}
	auto text = u"User message:\n"_q + CutText(userText, kTitleUserMaxChars);
	if (!assistantText.isEmpty()) {
		text += u"\n\nAssistant reply:\n"_q
			+ CutText(assistantText, kTitleAssistantMaxChars);
	}
	return Request{
		model,
		u"Write a 3 to 6 word title for the conversation in the language "
		u"of the user's message. Reply with the title only: no quotes, "
		u"no final period."_q,
		{ Message{ Role::User, { TextBlock{ std::move(text) } } } },
		{},
		kTitleMaxOutputTokens,
	};
}

QString ResponseText(const Message &message) {
	auto result = QString();
	for (const auto &block : message.blocks) {
		if (const auto text = std::get_if<TextBlock>(&block)) {
			if (!result.isEmpty()) {
				result += '\n';
			}
			result += text->text;
		}
	}
	return result;
}

QString CleanTitle(const QString &text) {
	auto line = QString();
	for (const auto &part : text.split('\n')) {
		const auto trimmed = part.trimmed();
		if (!trimmed.isEmpty()) {
			line = trimmed;
			break;
		}
	}
	const auto isQuote = [](QChar character) {
		return character == '"'
			|| character == '\''
			|| character == u'\u201C'
			|| character == u'\u201D'
			|| character == u'\u2018'
			|| character == u'\u2019'
			|| character == u'\u00AB'
			|| character == u'\u00BB';
	};
	auto start = 0;
	auto end = line.size();
	while (start < end && isQuote(line[start])) {
		++start;
	}
	while (end > start && isQuote(line[end - 1])) {
		--end;
	}
	auto result = line.mid(start, end - start).trimmed();
	if (result.endsWith('.')) {
		result.chop(1);
		result = result.trimmed();
	}
	return CutText(result, 60);
}

bool IsContextOverflow(const Error &error) {
	if (error.kind != ErrorKind::BadRequest
		&& error.httpStatus != 400
		&& error.httpStatus != 413) {
		return false;
	}
	const auto text = error.text.toLower();
	for (const auto &phrase : {
		u"context length"_q,
		u"context_length"_q,
		u"context window"_q,
		u"maximum context"_q,
		u"too many tokens"_q,
		u"too long"_q,
	}) {
		if (text.contains(phrase)) {
			return true;
		}
	}
	return false;
}

QString HandoffText(
		const SessionRecord &record,
		const QString &chatTitle) {
	auto text = u"Handoff from the chat \u201C%1\u201D."_q.arg(chatTitle);
	if (!record.summary.isEmpty()) {
		text += u"\n\nSummary of the earlier conversation:\n"_q
			+ record.summary;
	}
	text += u"\n\nTranscript:\n"_q;
	text += TranscriptText(std::vector<Message>(
		record.messages.begin() + record.summaryUntil,
		record.messages.end()));
	if (text.size() <= kHandoffMaxChars) {
		return text;
	}
	const auto note = u"[The beginning of the handoff was cut off.]"_q;
	const auto excess = text.size() + note.size() + 1 - kHandoffMaxChars;
	if (excess >= text.size()) {
		return note;
	}
	auto cut = excess;
	while (cut < text.size() && text[cut - 1] != '\n') {
		++cut;
	}
	return note + u"\n"_q + text.mid(cut);
}

} // namespace Agent
