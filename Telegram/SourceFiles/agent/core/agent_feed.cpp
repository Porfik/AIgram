/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_feed.h"

#include "base/algorithm.h"

namespace Agent {

std::vector<FeedItem> BuildFeed(
		const std::vector<Message> &messages,
		int summaryUntil) {
	auto result = std::vector<FeedItem>();
	result.reserve(messages.size());
	auto pending = std::vector<int>();
	const auto attach = [&](const ToolResultBlock &block) {
		for (auto i = int(pending.size()) - 1; i >= 0; --i) {
			auto &tool = std::get<FeedTool>(result[pending[i]]);
			if (tool.call.id == block.toolUseId) {
				tool.result = block;
				pending.erase(pending.begin() + i);
				return;
			}
		}
	};
	const auto count = int(messages.size());
	for (auto index = 0; index != count; ++index) {
		if (summaryUntil > 0 && summaryUntil == index) {
			result.push_back(FeedSummary{ summaryUntil });
		}
		const auto &message = messages[index];
		const auto user = (message.role == Role::User);
		auto text = QString();
		const auto flush = [&] {
			if (text.isEmpty()) {
				return;
			}
			if (user) {
				result.push_back(FeedUser{ index, base::take(text) });
			} else {
				result.push_back(FeedAssistant{ index, base::take(text) });
			}
		};
		for (const auto &block : message.blocks) {
			std::visit([&](const auto &value) {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, TextBlock>) {
					if (!value.hidden) {
						if (!text.isEmpty()) {
							text += u"\n\n"_q;
						}
						text += value.text;
					}
				} else if constexpr (std::is_same_v<T, ImageBlock>) {
				} else if constexpr (std::is_same_v<T, ThinkingBlock>) {
					if (!user) {
						flush();
						if (!value.text.isEmpty()) {
							result.push_back(
								FeedThinking{ index, value.text });
						}
					}
				} else if constexpr (std::is_same_v<T, ToolUseBlock>) {
					flush();
					result.push_back(FeedTool{ index, value, std::nullopt });
					pending.push_back(int(result.size()) - 1);
				} else if constexpr (std::is_same_v<T, ToolResultBlock>) {
					attach(value);
				}
			}, block);
		}
		flush();
	}
	if (summaryUntil > 0 && summaryUntil == count) {
		result.push_back(FeedSummary{ summaryUntil });
	}
	return result;
}

} // namespace Agent
