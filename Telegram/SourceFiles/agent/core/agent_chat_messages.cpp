/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_chat_messages.h"

#include "base/algorithm.h"
#include "base/flat_map.h"
#include "base/variant.h"

namespace Agent {
namespace {

[[nodiscard]] QChar KindLetter(ChatMessageKind kind) {
	switch (kind) {
	case ChatMessageKind::User: return u'u';
	case ChatMessageKind::Agent: return u'a';
	case ChatMessageKind::Card: return u'c';
	case ChatMessageKind::Summary: return u's';
	}
	return u'x';
}

} // namespace

std::vector<ChatMessage> BuildChatMessages(
		const std::vector<FeedItem> &feed) {
	auto result = std::vector<ChatMessage>();
	auto used = base::flat_map<QString, int>();
	auto pending = std::optional<ChatMessage>();
	const auto push = [&](ChatMessage message) {
		const auto base = KindLetter(message.kind)
			+ QString::number(message.index);
		const auto ordinal = used[base]++;
		message.key = ordinal ? (base + u'.' + QString::number(ordinal)) : base;
		result.push_back(std::move(message));
	};
	const auto flush = [&] {
		if (pending) {
			push(base::take(*pending));
			pending.reset();
		}
	};
	const auto agent = [&](int index) -> ChatMessage & {
		if (pending && !pending->text.isEmpty()) {
			flush();
		}
		if (!pending) {
			pending = ChatMessage{
				.kind = ChatMessageKind::Agent,
				.index = index,
			};
		}
		return *pending;
	};
	for (const auto &item : feed) {
		v::match(item, [&](const FeedUser &data) {
			flush();
			push(ChatMessage{
				.kind = ChatMessageKind::User,
				.index = data.index,
				.text = data.text,
				.attachments = data.attachments,
			});
		}, [&](const FeedAssistant &data) {
			auto &message = agent(data.index);
			message.text = data.text;
			flush();
		}, [&](const FeedSteps &data) {
			auto &message = agent(data.index);
			message.steps.insert(
				end(message.steps),
				begin(data.steps),
				end(data.steps));
		}, [&](const FeedActions &data) {
			auto &message = agent(data.index);
			message.actions.insert(
				end(message.actions),
				begin(data.actions),
				end(data.actions));
		}, [&](const FeedCard &data) {
			flush();
			push(ChatMessage{
				.kind = ChatMessageKind::Card,
				.index = data.index,
				.card = data,
			});
		}, [&](const FeedSummary &data) {
			flush();
			push(ChatMessage{
				.kind = ChatMessageKind::Summary,
				.index = data.until,
			});
		});
	}
	flush();
	return result;
}

void LiveTail::apply(const EngineEvent &event) {
	v::match(event, [&](const TextDelta &data) {
		auto &blocks = assistant().blocks;
		if (blocks.empty() || !std::holds_alternative<TextBlock>(blocks.back())) {
			blocks.push_back(TextBlock());
		}
		std::get<TextBlock>(blocks.back()).text += data.text;
	}, [&](const ThinkingDelta &data) {
		auto &blocks = assistant().blocks;
		if (blocks.empty()
			|| !std::holds_alternative<ThinkingBlock>(blocks.back())) {
			blocks.push_back(ThinkingBlock());
		}
		std::get<ThinkingBlock>(blocks.back()).text += data.text;
	}, [&](const ToolUseStarted &data) {
		assistant().blocks.push_back(ToolUseBlock{
			.id = data.id,
			.name = data.name,
		});
	}, [&](const ToolCallRequested &data) {
		auto &blocks = assistant().blocks;
		for (auto &block : blocks) {
			if (const auto call = std::get_if<ToolUseBlock>(&block)) {
				if (call->id == data.call.id) {
					*call = data.call;
					return;
				}
			}
		}
		blocks.push_back(data.call);
	}, [&](const ToolCallFinished &data) {
		if (!_results) {
			_results = Message{ .role = Role::User };
		}
		_results->blocks.push_back(data.result);
	}, [](const auto &) {
	});
}

void LiveTail::clear() {
	_assistant.reset();
	_results.reset();
}

bool LiveTail::empty() const {
	return !_assistant && !_results;
}

std::vector<Message> LiveTail::messages() const {
	auto result = std::vector<Message>();
	if (_assistant) {
		result.push_back(*_assistant);
	}
	if (_results) {
		result.push_back(*_results);
	}
	return result;
}

Message &LiveTail::assistant() {
	if (!_assistant) {
		_assistant = Message{ .role = Role::Assistant };
	}
	return *_assistant;
}

} // namespace Agent
