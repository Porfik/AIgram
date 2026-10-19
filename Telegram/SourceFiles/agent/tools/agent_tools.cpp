/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tools.h"

#include "agent/core/agent_context.h"
#include "agent/tools/agent_tool_table.h"
#include "crl/crl_on_main.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Agent {
namespace {

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

const std::vector<ToolEntry> &ToolsTable() {
	static const auto table = [] {
		auto result = std::vector<ToolEntry>();
		const auto append = [&](std::vector<ToolEntry> &&entries) {
			for (auto &entry : entries) {
				result.push_back(std::move(entry));
			}
		};
		append(ChatTools::Entries());
		append(GeneralTools::Entries());
		append(ActionTools::Entries());
		append(ChatStateTools::Entries());
		append(FolderTools::Entries());
		return result;
	}();
	return table;
}

} // namespace

const base::flat_set<QString> &ActionToolNames() {
	static const auto result = [] {
		auto names = base::flat_set<QString>();
		for (const auto &entry : ToolsTable()) {
			if (entry.action) {
				names.emplace(entry.name);
			}
		}
		return names;
	}();
	return result;
}

ToolRegistry::ToolRegistry(
	not_null<Main::Session*> session,
	not_null<PolicyGate*> gate,
	std::vector<QString> loadedGroups)
: _session(session)
, _gate(gate)
, _loadedGroups(std::move(loadedGroups)) {
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
	const auto i = ranges::find(table, call.name, &ToolEntry::name);
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

rpl::producer<PeerKey> ToolRegistry::touched() const {
	return _touched.events();
}

rpl::producer<Card> ToolRegistry::cardUpdates() const {
	return _cardUpdates.events();
}

const std::vector<QString> &ToolRegistry::loadedGroups() const {
	return _loadedGroups;
}

rpl::producer<> ToolRegistry::loadedGroupsChanges() const {
	return _loadedGroupsChanges.events();
}

void ToolRegistry::decide(
		const QString &toolUseId,
		CardDecision decision) {
}

bool ToolRegistry::canUndo(const QString &toolUseId) const {
	return false;
}

void ToolRegistry::undo(
		const QString &toolUseId,
		Fn<void(QString error)> done) {
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
