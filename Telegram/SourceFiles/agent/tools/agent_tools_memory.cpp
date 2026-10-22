/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_manager.h"
#include "agent/agent_memories.h"
#include "agent/core/agent_memory.h"
#include "agent/core/agent_scope.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Agent {
namespace {

[[nodiscard]] PeerKey MemoryOwner(not_null<PendingCall*> call) {
	return call->gate->scope().peer;
}

[[nodiscard]] QJsonObject FactJson(const MemoryFact &fact) {
	auto result = QJsonObject();
	result.insert("id", fact.id);
	result.insert("text", fact.text);
	return result;
}

void DeliverFact(not_null<PendingCall*> call, const MemoryFact &fact) {
	const auto owner = MemoryOwner(call);
	auto json = FactJson(fact);
	json.insert(
		"count",
		QJsonValue(int(call->session->agent().memories().get(owner)
			.facts.size())));
	json.insert("limit", QJsonValue(kMemoryMaxFacts));
	call->done(std::move(json));
}

[[nodiscard]] QString MemoryId(
		not_null<PendingCall*> call,
		QString &error) {
	const auto id = call->input.value("id").toString().trimmed();
	if (id.isEmpty()) {
		error = u"id is required, like \"m3\"."_q;
	}
	return id;
}

[[nodiscard]] Card MemoryCard(
		const QString &title,
		const QString &tool,
		const QString &text,
		const QString &previous) {
	auto card = Card{ .kind = CardKind::Change };
	card.text = title;
	card.detail = tool;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_fact(tr::now),
		.value = text,
		.previous = previous,
	});
	return card;
}

void AddFact(not_null<PendingCall*> call, const QString &text) {
	const auto owner = MemoryOwner(call);
	const auto result = call->session->agent().memories().add(owner, text);
	if (result.error != MemoryError::None) {
		call->fail(MemoryErrorText(result.error));
		return;
	}
	const auto session = call->session;
	call->undoable([=](Fn<void(QString error)> finished) {
		const auto removed = session->agent().memories().remove(
			owner,
			result.fact.id);
		finished((removed.error == MemoryError::None
			|| removed.error == MemoryError::UnknownId)
			? QString()
			: MemoryErrorText(removed.error));
	});
	DeliverFact(call, result.fact);
}

void RunMemoryAdd(not_null<PendingCall*> call) {
	const auto text = call->input.value("text").toString();
	if (!call->gate->tainted()) {
		AddFact(call, text);
		return;
	}
	auto card = MemoryCard(
		tr::lng_agent_card_title_memory_add(tr::now),
		u"memory_add"_q,
		NormalizeMemoryText(text),
		QString());
	call->confirm(std::move(card), [=](Card) {
		AddFact(call, text);
	});
}

void UpdateFact(
		not_null<PendingCall*> call,
		const QString &id,
		const QString &text) {
	const auto owner = MemoryOwner(call);
	const auto result = call->session->agent().memories().update(
		owner,
		id,
		text);
	if (result.error != MemoryError::None) {
		call->fail(MemoryErrorText(result.error));
		return;
	}
	const auto session = call->session;
	call->undoable([=](Fn<void(QString error)> finished) {
		const auto back = session->agent().memories().update(
			owner,
			id,
			result.previousText);
		finished(back.error == MemoryError::None
			? QString()
			: MemoryErrorText(back.error));
	});
	DeliverFact(call, result.fact);
}

void RunMemoryUpdate(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = MemoryId(call, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto text = call->input.value("text").toString();
	if (!call->gate->tainted()) {
		UpdateFact(call, id, text);
		return;
	}
	const auto &memory = call->session->agent().memories().get(
		MemoryOwner(call));
	const auto i = ranges::find(memory.facts, id, &MemoryFact::id);
	if (i == end(memory.facts)) {
		call->fail(MemoryErrorText(MemoryError::UnknownId));
		return;
	}
	auto card = MemoryCard(
		tr::lng_agent_card_title_memory_update(tr::now),
		u"memory_update"_q,
		NormalizeMemoryText(text),
		i->text);
	call->confirm(std::move(card), [=](Card) {
		UpdateFact(call, id, text);
	});
}

void RunMemoryRemove(not_null<PendingCall*> call) {
	auto error = QString();
	const auto id = MemoryId(call, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	const auto owner = MemoryOwner(call);
	const auto result = call->session->agent().memories().remove(owner, id);
	if (result.error != MemoryError::None) {
		call->fail(MemoryErrorText(result.error));
		return;
	}
	const auto session = call->session;
	call->undoable([=](Fn<void(QString error)> finished) {
		const auto restored = session->agent().memories().restore(
			owner,
			result.fact,
			result.index);
		finished(restored.error == MemoryError::None
			? QString()
			: MemoryErrorText(restored.error));
	});
	DeliverFact(call, result.fact);
}

void RunReadChatMemory(not_null<PendingCall*> call) {
	const auto peer = call->peer;
	if (!peer) {
		call->fail(u"chat is required."_q);
		return;
	}
	const auto &memory
		= call->session->agent().memories().get(peer->id.value);
	auto facts = QJsonArray();
	for (const auto &fact : memory.facts) {
		facts.push_back(FactJson(fact));
	}
	auto json = QJsonObject();
	json.insert("chat", QJsonValue(qint64(peer->id.value)));
	json.insert("title", peer->name());
	json.insert("facts", facts);
	call->done(std::move(json));
}

} // namespace

namespace MemoryTools {

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto addProperties = QJsonObject();
	addProperties.insert("text", StringProperty(
		u"The fact to remember, one line, at most 300 characters."_q));
	auto idProperties = [] {
		auto properties = QJsonObject();
		properties.insert("id", StringProperty(
			u"Memory fact id, like \"m3\"."_q));
		return properties;
	}();
	auto updateProperties = [] {
		auto properties = QJsonObject();
		properties.insert("id", StringProperty(
			u"Memory fact id, like \"m3\"."_q));
		properties.insert("text", StringProperty(
			u"The new text of the fact, at most 300 characters."_q));
		return properties;
	}();
	const auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	push(ToolEntry{
		.name = u"memory_add"_q,
		.description = u"Save a short fact to this chat's memory. Save"
			" only what the user asks to remember or clearly states as"
			" a lasting preference or fact, never one-off task details."
			" Keep facts short; fix them with memory_update and delete"
			" them with memory_remove, also when asked to forget."_q,
		.generalDescription = u"Save a short fact to your own memory."
			" Save only what the user asks to remember or clearly states"
			" as a lasting preference or fact, never one-off task"
			" details. Keep facts short; fix them with memory_update"
			" and delete them with memory_remove, also when asked to"
			" forget."_q,
		.inputSchema = ToolSchema(
			std::move(addProperties),
			QJsonArray{ QJsonValue(u"text"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = true,
			.background = false,
			.taintedAsks = true,
		},
		.untrusted = false,
		.action = true,
		.run = RunMemoryAdd,
	});
	push(ToolEntry{
		.name = u"memory_update"_q,
		.description = u"Replace the text of a memory fact of this chat"
			" by its id."_q,
		.generalDescription = u"Replace the text of one of your memory"
			" facts by its id."_q,
		.inputSchema = ToolSchema(
			std::move(updateProperties),
			QJsonArray{ QJsonValue(u"id"_q), QJsonValue(u"text"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = true,
			.background = false,
			.taintedAsks = true,
		},
		.untrusted = false,
		.action = true,
		.run = RunMemoryUpdate,
	});
	push(ToolEntry{
		.name = u"memory_remove"_q,
		.description = u"Remove a memory fact of this chat by its id,"
			" also when the user asks to forget something."_q,
		.generalDescription = u"Remove one of your memory facts by its"
			" id, also when the user asks to forget something."_q,
		.inputSchema = ToolSchema(
			std::move(idProperties),
			QJsonArray{ QJsonValue(u"id"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = true,
			.background = false,
		},
		.untrusted = false,
		.action = true,
		.run = RunMemoryRemove,
	});
	push(ToolEntry{
		.name = u"read_chat_memory"_q,
		.description = u"Read the memory facts of a chat with their ids"
			" and texts."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = true,
			.chat = false,
			.background = false,
		},
		.chatParam = ChatParam::One,
		.run = RunReadChatMemory,
	});
	return result;
}

} // namespace MemoryTools
} // namespace Agent
