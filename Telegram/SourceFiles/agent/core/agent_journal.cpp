/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_journal.h"

#include "agent/core/agent_prompts.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <algorithm>

namespace Agent {
namespace {

[[nodiscard]] bool FailedJson(const QString &content) {
	const auto document = QJsonDocument::fromJson(content.toUtf8());
	if (!document.isObject()) {
		return false;
	}
	const auto object = document.object();
	const auto failed = object.value(u"failed"_q).toArray();
	if (failed.isEmpty()) {
		return false;
	}
	const auto done = object.value(u"done"_q).toArray();
	return done.isEmpty();
}

[[nodiscard]] std::optional<ToolResultBlock> FindResult(
		const SessionRecord &record,
		const QString &id) {
	for (const auto &message : record.messages) {
		for (const auto &block : message.blocks) {
			if (const auto result = std::get_if<ToolResultBlock>(&block)) {
				if (result->toolUseId == id) {
					return *result;
				}
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] const Card *FindCard(
		const SessionRecord &record,
		const QString &id) {
	for (const auto &card : record.cards) {
		if (card.toolUseId == id) {
			return &card;
		}
	}
	return nullptr;
}

[[nodiscard]] bool CardListed(const Card &card) {
	return card.status == CardStatus::Done
		|| card.status == CardStatus::Drafted;
}

[[nodiscard]] bool IsUndone(const SessionRecord &record, const QString &id) {
	return std::find(
		begin(record.undone),
		end(record.undone),
		id
	) != end(record.undone);
}

struct Indexed {
	JournalEntry entry;
	int index = 0;
};

} // namespace

std::vector<JournalEntry> BuildJournal(
		const std::vector<SessionRecord> &records,
		const base::flat_set<QString> &actionTools,
		int limit) {
	auto entries = std::vector<Indexed>();
	auto index = 0;
	for (const auto &record : records) {
		const auto &info = record.info;
		for (const auto &message : record.messages) {
			for (const auto &block : message.blocks) {
				const auto call = std::get_if<ToolUseBlock>(&block);
				if (!call) {
					continue;
				}
				const auto result = FindResult(record, call->id);
				const auto card = FindCard(record, call->id);
				if (card) {
					if (!CardListed(*card)) {
						continue;
					}
					auto entry = JournalEntry();
					entry.sessionId = info.id;
					entry.sessionTitle = info.title;
					entry.scope = info.scope;
					entry.task = info.task;
					entry.call = *call;
					entry.result = result;
					entry.card = *card;
					entry.date = message.date > 0
						? message.date
						: info.created;
					entry.undone = IsUndone(record, call->id);
					entries.push_back({ std::move(entry), index++ });
					continue;
				}
				if (!actionTools.contains(call->name)) {
					continue;
				}
				if (!result) {
					continue;
				}
				auto entry = JournalEntry();
				entry.sessionId = info.id;
				entry.sessionTitle = info.title;
				entry.scope = info.scope;
				entry.task = info.task;
				entry.call = *call;
				entry.result = result;
				entry.date = message.date > 0
					? message.date
					: info.created;
				entry.undone = IsUndone(record, call->id);
				entry.failed = result->error
					|| FailedJson(UnwrapUntrusted(result->content));
				entries.push_back({ std::move(entry), index++ });
			}
		}
	}
	std::sort(
		begin(entries),
		end(entries),
		[](const Indexed &a, const Indexed &b) {
			if (a.entry.date != b.entry.date) {
				return a.entry.date > b.entry.date;
			}
			return a.index > b.index;
		});
	if (limit > 0 && entries.size() > std::size_t(limit)) {
		entries.resize(std::size_t(limit));
	}
	auto result = std::vector<JournalEntry>();
	result.reserve(entries.size());
	for (auto &indexed : entries) {
		result.push_back(std::move(indexed.entry));
	}
	return result;
}

} // namespace Agent
