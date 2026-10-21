/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_feed.h"

#include "base/algorithm.h"

namespace Agent {
namespace {

struct PendingTool {
	int item = -1;
	int step = 0;
	bool local = false;
	bool action = false;
};

struct Run {
	int stepsIndex = -1;
	int actionsIndex = -1;
	std::vector<FeedStep> steps;
	std::vector<FeedTool> actions;
};

const Card *FindCard(const std::vector<Card> &cards, const QString &id) {
	for (const auto &card : cards) {
		if (card.toolUseId == id) {
			return &card;
		}
	}
	return nullptr;
}

} // namespace

std::vector<FeedItem> BuildFeed(
		const std::vector<Message> &messages,
		int summaryUntil,
		const std::vector<Card> &cards,
		const base::flat_set<QString> &actionTools,
		const std::vector<TurnEndMark> &turnEnds) {
	auto result = std::vector<FeedItem>();
	result.reserve(messages.size());
	auto run = std::optional<Run>();
	auto pending = std::vector<PendingTool>();
	const auto attach = [&](const ToolResultBlock &block) {
		for (auto i = int(pending.size()) - 1; i >= 0; --i) {
			const auto &entry = pending[i];
			if (entry.local) {
				auto &tool = entry.action
					? run->actions[entry.step]
					: std::get<FeedTool>(run->steps[entry.step]);
				if (tool.call.id == block.toolUseId) {
					tool.result = block;
					pending.erase(pending.begin() + i);
					return;
				}
				continue;
			}
			auto &item = result[entry.item];
			auto found = false;
			if (const auto card = std::get_if<FeedCard>(&item)) {
				found = (card->call.id == block.toolUseId);
				if (found) {
					card->result = block;
				}
			} else if (const auto group = std::get_if<FeedSteps>(&item)) {
				auto &tool = std::get<FeedTool>(group->steps[entry.step]);
				found = (tool.call.id == block.toolUseId);
				if (found) {
					tool.result = block;
				}
			} else if (const auto group = std::get_if<FeedActions>(&item)) {
				auto &tool = group->actions[entry.step];
				found = (tool.call.id == block.toolUseId);
				if (found) {
					tool.result = block;
				}
			}
			if (found) {
				pending.erase(pending.begin() + i);
				return;
			}
		}
	};
	const auto flushRun = [&] {
		if (!run) {
			return;
		}
		auto stepsItem = -1;
		auto actionsItem = -1;
		if (!run->steps.empty()) {
			stepsItem = int(result.size());
			result.push_back(
				FeedSteps{ run->stepsIndex, std::move(run->steps) });
		}
		if (!run->actions.empty()) {
			actionsItem = int(result.size());
			result.push_back(
				FeedActions{ run->actionsIndex, std::move(run->actions) });
		}
		for (auto &entry : pending) {
			if (!entry.local) {
				continue;
			}
			entry.local = false;
			entry.item = entry.action ? actionsItem : stepsItem;
		}
		run.reset();
	};
	const auto addStep = [&](int index, FeedStep step) {
		if (!run) {
			run = Run();
		}
		if (run->steps.empty()) {
			run->stepsIndex = index;
		}
		const auto tool = std::holds_alternative<FeedTool>(step);
		run->steps.push_back(std::move(step));
		if (tool) {
			pending.push_back(
				{ -1, int(run->steps.size()) - 1, true, false });
		}
	};
	const auto addAction = [&](int index, ToolUseBlock call) {
		if (!run) {
			run = Run();
		}
		if (run->actions.empty()) {
			run->actionsIndex = index;
		}
		run->actions.push_back(FeedTool{ index, std::move(call), std::nullopt });
		pending.push_back(
			{ -1, int(run->actions.size()) - 1, true, true });
	};
	const auto count = int(messages.size());
	auto markIndex = 0;
	const auto flushMarks = [&](int index) {
		while (markIndex < int(turnEnds.size())
			&& turnEnds[markIndex].after == index) {
			const auto &mark = turnEnds[markIndex];
			flushRun();
			result.push_back(FeedTurnEnd{ mark.after - 1, mark.end });
			++markIndex;
		}
	};
	for (auto index = 0; index != count; ++index) {
		flushMarks(index);
		if (summaryUntil > 0 && summaryUntil == index) {
			flushRun();
			result.push_back(FeedSummary{ summaryUntil });
		}
		const auto &message = messages[index];
		const auto user = (message.role == Role::User);
		auto text = QString();
		auto attachments = std::vector<MediaBlock>();
		const auto flush = [&] {
			if (text.isEmpty() && attachments.empty()) {
				return;
			}
			flushRun();
			if (user) {
				result.push_back(FeedUser{
					index,
					base::take(text),
					base::take(attachments),
					message.replyTo,
					message.replyQuote });
			} else {
				result.push_back(FeedAssistant{ index, base::take(text) });
			}
		};
		for (const auto &block : message.blocks) {
			std::visit([&](const auto &value) {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, TextBlock>) {
					if (!value.hidden && !value.text.trimmed().isEmpty()) {
						if (!text.isEmpty()) {
							text += u"\n\n"_q;
						}
						text += value.text;
					}
				} else if constexpr (std::is_same_v<T, MediaBlock>) {
					if (user) {
						attachments.push_back(value);
					}
				} else if constexpr (std::is_same_v<T, ThinkingBlock>) {
					if (!user) {
						flush();
						if (!value.text.isEmpty()) {
							addStep(index, FeedThinking{
								index,
								value.text,
								value.durationMs });
						}
					}
				} else if constexpr (std::is_same_v<T, ToolUseBlock>) {
					flush();
					if (const auto card = FindCard(cards, value.id)) {
						flushRun();
						result.push_back(FeedCard{
							index,
							value,
							std::nullopt,
							*card });
						pending.push_back(
							{ int(result.size()) - 1, 0, false, false });
					} else if (actionTools.contains(value.name)) {
						addAction(index, value);
					} else {
						addStep(index, FeedTool{ index, value, std::nullopt });
					}
				} else if constexpr (std::is_same_v<T, ToolResultBlock>) {
					attach(value);
				}
			}, block);
		}
		flush();
	}
	flushRun();
	flushMarks(count);
	if (summaryUntil > 0 && summaryUntil == count) {
		result.push_back(FeedSummary{ summaryUntil });
	}
	return result;
}

} // namespace Agent
