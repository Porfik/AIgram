/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tools.h"

#include "agent/core/agent_context.h"
#include "agent/tools/agent_tools_chat.h"
#include "crl/crl_on_main.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#include <limits>

namespace Agent {
namespace {

using ChatTools::PendingCall;

struct TableEntry {
	QString name;
	QString description;
	QJsonObject inputSchema;
	ToolPolicy policy;
	Fn<void(not_null<PendingCall*>)> run;
};

QJsonObject Schema(QJsonObject properties, QJsonArray required) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"object"_q));
	result.insert("properties", properties);
	result.insert("required", required);
	result.insert("additionalProperties", QJsonValue(false));
	return result;
}

QJsonObject StringProperty(QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"string"_q));
	result.insert("description", description);
	return result;
}

QJsonObject IntProperty(int minimum, int maximum, QString description) {
	auto result = QJsonObject();
	result.insert("type", QJsonValue(u"integer"_q));
	result.insert("description", description);
	result.insert("minimum", QJsonValue(minimum));
	result.insert("maximum", QJsonValue(maximum));
	return result;
}

QJsonObject MessageIdProperty(QString description) {
	return IntProperty(
		1,
		std::numeric_limits<int>::max(),
		std::move(description));
}

QString DenyText(Decision decision) {
	if (decision.verdict == Verdict::Confirm) {
		return u"This action needs a confirmation, which is not supported"
			" yet."_q;
	}
	switch (decision.reason) {
	case DenyReason::NotInMode:
		return u"This tool is not available here."_q;
	case DenyReason::Excluded:
		return u"This chat is hidden from the agent."_q;
	case DenyReason::OutOfScope:
		return u"This chat is outside of this agent's scope."_q;
	case DenyReason::Forbidden:
		return u"This action is never allowed."_q;
	}
	return u"This action is not allowed."_q;
}

const std::vector<TableEntry> &ToolsTable() {
	static const auto table = [] {
		auto result = std::vector<TableEntry>();
		auto empty = QJsonObject();
		auto historyProperties = QJsonObject();
		historyProperties.insert("around_id", MessageIdProperty(
			u"Read a window centered on this message id."_q));
		historyProperties.insert("before_id", MessageIdProperty(
			u"Read only messages older than this id."_q));
		historyProperties.insert("after_id", MessageIdProperty(
			u"Read only messages newer than this id."_q));
		historyProperties.insert("limit", IntProperty(
			1,
			100,
			u"How many messages to return, 1..100, default 30."_q));
		historyProperties.insert("topic_id", MessageIdProperty(
			u"Forum topic root message id."_q));
		auto searchProperties = QJsonObject();
		searchProperties.insert("query", StringProperty(
			u"Text to search for."_q));
		searchProperties.insert("from", StringProperty(
			u"\"me\", a numeric user id or @username, resolved among"
			" locally known users only."_q));
		searchProperties.insert("min_date", StringProperty(
			u"Only messages sent on or after this local date YYYY-MM-DD."_q));
		searchProperties.insert("max_date", StringProperty(
			u"Only messages sent on or before this local date YYYY-MM-DD."_q));
		searchProperties.insert("limit", IntProperty(
			1,
			50,
			u"How many messages to return, 1..50, default 20."_q));
		searchProperties.insert("offset_id", MessageIdProperty(
			u"Pagination: search older than this message id."_q));
		searchProperties.insert("topic_id", MessageIdProperty(
			u"Forum topic root message id."_q));
		auto membersProperties = QJsonObject();
		membersProperties.insert("query", StringProperty(
			u"Search members by name or username."_q));
		membersProperties.insert("limit", IntProperty(
			1,
			100,
			u"How many members to return, 1..100, default 50."_q));
		membersProperties.insert("offset", IntProperty(
			0,
			std::numeric_limits<int>::max(),
			u"Skip this many members."_q));
		auto push = [&](const QString &name,
				const QString &description,
				QJsonObject schema,
				bool chat,
				auto run) {
			result.push_back(TableEntry{
				.name = name,
				.description = description,
				.inputSchema = std::move(schema),
				.policy = ToolPolicy{ .general = !chat, .chat = true },
				.run = run,
			});
		};
		push(
			u"get_context"_q,
			u"Get the current time and a summary of this chat."_q,
			Schema(std::move(empty), QJsonArray()),
			false,
			ChatTools::RunGetContext);
		push(
			u"read_history"_q,
			u"Read messages of this chat, oldest to newest. Give at most"
			" one of around_id, before_id, after_id. Reading never marks"
			" anything read."_q,
			Schema(std::move(historyProperties), QJsonArray()),
			true,
			ChatTools::RunReadHistory);
		push(
			u"search_messages"_q,
			u"Search messages in this chat, newest matches first. Page"
			" older results with offset_id from next_offset_id."_q,
			Schema(
				std::move(searchProperties),
				QJsonArray{ QJsonValue(u"query"_q) }),
			true,
			ChatTools::RunSearchMessages);
		push(
			u"get_peer_info"_q,
			u"Get details about this chat: title, kind, description,"
			" members, your role and rights."_q,
			Schema(QJsonObject(), QJsonArray()),
			true,
			ChatTools::RunGetPeerInfo);
		push(
			u"list_members"_q,
			u"List members of this chat with names, usernames and roles."_q,
			Schema(std::move(membersProperties), QJsonArray()),
			true,
			ChatTools::RunListMembers);
		return result;
	}();
	return table;
}

} // namespace

ToolRegistry::ToolRegistry(
	not_null<Main::Session*> session,
	not_null<PolicyGate*> gate)
: _session(session)
, _gate(gate) {
}

ToolRegistry::~ToolRegistry() {
	for (const auto &[id, pending] : _pending) {
		pending->cancel();
	}
}

std::vector<ToolSpec> ToolRegistry::tools() const {
	auto result = std::vector<ToolSpec>();
	for (const auto &entry : ToolsTable()) {
		if (!_gate->available(entry.policy)) {
			continue;
		}
		result.push_back(ToolSpec{
			.name = entry.name,
			.description = entry.description,
			.inputSchema = entry.inputSchema,
		});
	}
	return result;
}

void ToolRegistry::execute(
		ToolUseBlock call,
		Fn<void(ToolResultBlock)> done) {
	const auto &table = ToolsTable();
	const auto i = ranges::find(table, call.name, &TableEntry::name);
	if (i == end(table)) {
		done(ToolResultBlock{
			.toolUseId = call.id,
			.content = CapToolResult(
				u"Unknown tool: %1."_q.arg(call.name),
				kToolResultMaxChars),
			.error = true,
		});
		return;
	}
	const auto &entry = *i;
	const auto scope = _gate->scope();
	const auto peer = (scope.kind == ScopeKind::Chat)
		? _session->data().peerLoaded(PeerId(scope.peer))
		: nullptr;
	if (scope.kind == ScopeKind::Chat && !peer) {
		done(ToolResultBlock{
			.toolUseId = call.id,
			.content = u"This chat is not loaded yet, try again later."_q,
			.error = true,
		});
		return;
	}
	const auto peers = (scope.kind == ScopeKind::Chat)
		? std::vector<PeerKey>{ scope.peer }
		: std::vector<PeerKey>();
	const auto request = ActionRequest{
		.tool = entry.name,
		.policy = entry.policy,
		.peers = peers,
	};
	const auto decision = _gate->check(request);
	if (decision.verdict != Verdict::Allow) {
		done(ToolResultBlock{
			.toolUseId = call.id,
			.content = CapToolResult(DenyText(decision), kToolResultMaxChars),
			.error = true,
		});
		return;
	}
	auto pending = std::make_unique<PendingCall>(_session);
	pending->peer = peer;
	pending->input = call.input;
	pending->result = std::move(done);
	pending->done = [this, id = call.id](QJsonObject json) {
		deliverJson(id, std::move(json));
	};
	pending->fail = [this, id = call.id](QString text) {
		deliver(id, std::move(text), true);
	};
	const auto raw = pending.get();
	_pending.emplace(call.id, std::move(pending));
	entry.run(raw);
	if (scope.kind == ScopeKind::Chat) {
		_touched.fire_copy(scope.peer);
	}
}

void ToolRegistry::cancel(const QString &toolUseId) {
	const auto i = _pending.find(toolUseId);
	if (i == end(_pending)) {
		return;
	}
	auto entry = std::move(i->second);
	_pending.erase(i);
	entry->cancel();
}

void ToolRegistry::deliver(
		const QString &toolUseId,
		QString content,
		bool error) {
	const auto i = _pending.find(toolUseId);
	if (i == end(_pending)) {
		return;
	}
	auto entry = std::move(i->second);
	_pending.erase(i);
	content = CapToolResult(std::move(content), kToolResultMaxChars);
	if (!error && _gate->scope().kind == ScopeKind::Chat) {
		content = WrapUntrusted(content);
		_gate->markTainted();
	}
	entry->result(ToolResultBlock{
		.toolUseId = toolUseId,
		.content = std::move(content),
		.error = error,
	});
	crl::on_main([holder = std::move(entry)] {});
}

void ToolRegistry::deliverJson(
		const QString &toolUseId,
		QJsonObject json) {
	deliver(
		toolUseId,
		QString::fromUtf8(
			QJsonDocument(std::move(json)).toJson(QJsonDocument::Compact)),
		false);
}

} // namespace Agent
