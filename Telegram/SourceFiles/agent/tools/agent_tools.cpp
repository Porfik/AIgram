/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tools.h"

#include "agent/agent_attachments.h"
#include "agent/agent_manager.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_media.h"
#include "agent/core/agent_request.h"
#include "agent/tools/agent_tool_table.h"
#include "agent/ui/agent_task_format.h"
#include "base/algorithm.h"
#include "crl/crl_on_main.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Agent {
namespace {

const auto kFoldersGroup = u"folders"_q;
const auto kFilesGroup = u"files"_q;
const auto kTasksGroup = u"tasks"_q;

struct CallAbilities {
	MediaAbilities main;
	std::optional<MediaAbilities> helper;
};

[[nodiscard]] CallAbilities ComputeAbilities(
		not_null<Main::Session*> session,
		const std::optional<ProviderSettings> &background) {
	const auto &manager = session->agent();
	auto result = CallAbilities();
	const auto provider = background
		? &*background
		: ActiveProvider(manager.providers());
	if (provider) {
		result.main = manager.abilities(*provider);
		result.main.files = result.main.files || IsOpenRouter(*provider);
	}
	if (const auto provider = manager.mediaProvider()) {
		result.helper = manager.abilities(*provider);
	}
	return result;
}

[[nodiscard]] bool NeedsMet(MediaNeed needs, const CallAbilities &abilities) {
	switch (needs) {
	case MediaNeed::None: return true;
	case MediaNeed::Images:
		return ChooseMediaRoute(
			MediaKind::Image,
			abilities.main,
			abilities.helper) != MediaRoute::None;
	case MediaNeed::Audio:
		return ChooseMediaRoute(
			MediaKind::Audio,
			abilities.main,
			abilities.helper) != MediaRoute::None;
	}
	Unexpected("MediaNeed in NeedsMet.");
}

[[nodiscard]] QString GroupToolNames(const QString &group) {
	auto names = QStringList();
	const auto collect = [&](const std::vector<ToolEntry> &entries) {
		for (const auto &entry : entries) {
			if (entry.group == group) {
				names.push_back(entry.name);
			}
		}
	};
	collect(FolderTools::Entries());
	collect(FileTools::Entries());
	collect(TaskTools::Entries());
	return names.join(u", "_q);
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
		return u"The user hid this chat from the agent. Tell the user you"
			" can't see it and don't try to reach it another way."_q;
	case DenyReason::OutOfScope:
		return u"This chat is outside of this agent's scope."_q;
	case DenyReason::Forbidden:
		return u"This action is never allowed."_q;
	}
	return u"This action is not allowed."_q;
}

[[nodiscard]] QString RefName(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString().trimmed().left(64);
	} else if (value.isDouble()) {
		return QString::number(qint64(value.toDouble()));
	}
	return u"value"_q;
}

[[nodiscard]] QJsonObject WithChatParam(ChatParam param, QJsonObject schema) {
	if (param == ChatParam::None) {
		return schema;
	}
	auto properties = schema.value("properties").toObject();
	if (param == ChatParam::Many) {
		auto item = QJsonObject();
		item.insert("type", QJsonValue(u"string"_q));
		auto chats = QJsonObject();
		chats.insert("type", QJsonValue(u"array"_q));
		chats.insert("items", item);
		chats.insert("minItems", QJsonValue(1));
		chats.insert("maxItems", QJsonValue(100));
		chats.insert("description", QJsonValue(
			u"Target chats: 1 to 100 chat ids or @usernames."_q));
		properties.insert("chats", chats);
	} else {
		properties.insert("chat", StringProperty(
			u"Target chat: a chat id or an @username."_q));
		if (param == ChatParam::One) {
			auto required = schema.value("required").toArray();
			if (!required.contains(QJsonValue(u"chat"_q))) {
				required.push_back(u"chat"_q);
				schema.insert("required", required);
			}
		}
	}
	schema.insert("properties", properties);
	return schema;
}

ToolEntry LoadToolsEntry() {
	auto properties = QJsonObject();
	properties.insert("group", StringProperty(
		u"The tool group to load: \"folders\", \"files\" or"
		" \"tasks\"."_q));
	auto description = u"Load an additional group of tools."
		" \"folders\": %1. \"files\": %2. \"tasks\" (background tasks"
		" that run on new messages or on a schedule): %3."_q.arg(
			GroupToolNames(kFoldersGroup),
			GroupToolNames(kFilesGroup),
			GroupToolNames(kTasksGroup));
	return ToolEntry{
		.name = u"load_tools"_q,
		.description = description,
		.generalDescription = std::move(description),
		.inputSchema = ToolSchema(
			std::move(properties),
			QJsonArray{ QJsonValue(u"group"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Read, .background = false },
		.untrusted = false,
		.run = [](not_null<PendingCall*> call) {
			call->loadGroup(call->input.value("group").toString().trimmed());
		},
	};
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
		append(MemoryTools::Entries());
		append(MediaTools::Entries());
		append(FileTools::Entries());
		append(TaskTools::Entries());
		result.push_back(LoadToolsEntry());
		return result;
	}();
	return table;
}

} // namespace

struct ToolRegistry::Call {
	QString tool;
	ToolPolicy policy;
	bool untrusted = false;
	Verdict verdict = Verdict::Allow;
	bool canAllowForTurn = false;
	Card card;
	Fn<void(Card decided)> execute;
	bool hasCard = false;
	bool approved = false;
};

std::optional<ToolPolicy> ToolPolicyByName(const QString &name) {
	const auto &table = ToolsTable();
	const auto i = ranges::find(table, name, &ToolEntry::name);
	return (i != end(table))
		? std::make_optional(i->policy)
		: std::nullopt;
}

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
	std::vector<QString> loadedGroups,
	QString sessionId)
: _session(session)
, _gate(gate)
, _sessionId(std::move(sessionId))
, _loadedGroups(std::move(loadedGroups)) {
}

ToolRegistry::~ToolRegistry() {
	for (const auto &[id, pending] : _pending) {
		pending->cancel();
	}
}

void ToolRegistry::setBackground(
		ProviderSettings provider,
		Fn<void(QString text)> notify) {
	_backgroundProvider = std::move(provider);
	_notify = std::move(notify);
}

std::vector<ToolSpec> ToolRegistry::tools() const {
	const auto account = (_gate->scope().kind == ScopeKind::Account);
	const auto background = _gate->background();
	const auto &table = ToolsTable();
	const auto abilities = ComputeAbilities(_session, _backgroundProvider);
	auto result = std::vector<ToolSpec>();
	const auto add = [&](const ToolEntry &entry) {
		if (!_gate->available(entry.policy)
			|| !NeedsMet(entry.needs, abilities)) {
			return;
		}
		result.push_back(ToolSpec{
			.name = entry.name,
			.description = (account && !entry.generalDescription.isEmpty())
				? entry.generalDescription
				: entry.description,
			.inputSchema = account
				? WithChatParam(
					entry.chatParam,
					(entry.generalSchema.isEmpty()
						? entry.inputSchema
						: entry.generalSchema))
				: entry.inputSchema,
		});
	};
	for (const auto &entry : table) {
		if (entry.group.isEmpty() || background) {
			add(entry);
		}
	}
	if (background) {
		return result;
	}
	for (const auto &group : _loadedGroups) {
		for (const auto &entry : table) {
			if (entry.group == group) {
				add(entry);
			}
		}
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
	if (!entry.group.isEmpty()
		&& !_gate->background()
		&& !ranges::contains(_loadedGroups, entry.group)) {
		done(ToolResultBlock{
			.toolUseId = call.id,
			.content = CapToolResult(
				u"Tool %1 is not loaded. Call load_tools with group"
				" \"%2\" first."_q.arg(call.name, entry.group),
				kToolResultMaxChars),
			.error = true,
		});
		return;
	}
	auto pending = std::make_unique<PendingCall>(_session);
	pending->toolUseId = call.id;
	pending->gate = _gate;
	pending->input = call.input;
	pending->result = std::move(done);
	pending->done = [this, id = call.id](QJsonObject json) {
		deliverJson(id, std::move(json));
	};
	pending->fail = [this, id = call.id](QString text) {
		deliver(id, std::move(text), true);
	};
	pending->confirm = [this, id = call.id](
			Card card,
			Fn<void(Card decided)> execute) {
		confirmCall(id, std::move(card), std::move(execute));
	};
	pending->finishCard = [this, id = call.id](Card card) {
		finishCard(id, std::move(card));
	};
	pending->undoable = [this, id = call.id](
			Fn<void(Fn<void(QString error)> finished)> undo) {
		Expects(_pending.contains(id));
		_undo[id] = std::move(undo);
	};
	pending->loadGroup = [this, id = call.id](QString group) {
		loadGroup(id, std::move(group));
	};
	pending->sessionId = _sessionId;
	pending->notify = _notify;
	const auto abilities = ComputeAbilities(_session, _backgroundProvider);
	pending->mainAbilities = abilities.main;
	pending->helperAbilities = abilities.helper;
	pending->doneMedia = [this, id = call.id](
			QJsonObject json,
			std::vector<MediaBlock> media) {
		deliverJson(id, std::move(json), std::move(media));
	};
	pending->readAttachment = [session = _session, sessionId = _sessionId](
			QString id) {
		return session->agent().attachments().read(sessionId, id);
	};
	const auto raw = pending.get();
	pending->askMediaModel = [this, raw](
			MediaBlock media,
			QString question,
			Fn<void(QString text, QString error)> done) {
		askMediaModel(
			raw,
			std::move(media),
			std::move(question),
			std::move(done));
	};
	_pending.emplace(call.id, std::move(pending));
	auto &meta = _calls[call.id];
	meta.tool = entry.name;
	meta.policy = entry.policy;
	meta.untrusted = entry.untrusted;
	const auto scope = _gate->scope();
	auto peer = (PeerData*)nullptr;
	auto peers = std::vector<not_null<PeerData*>>();
	auto resolveError = QString();
	if (scope.kind == ScopeKind::Chat) {
		peer = _session->data().peerLoaded(PeerId(scope.peer));
		if (!peer) {
			resolveError = u"This chat is not loaded yet, try again later."_q;
		} else {
			peers.push_back(peer);
		}
	} else {
		const auto resolve = [&](const QJsonValue &value) {
			const auto resolved = ResolveChatRef(_session, value);
			if (!resolved) {
				resolveError = u"Unknown chat: %1. Use a chat id from"
					" an earlier tool result or an @username."_q
					.arg(RefName(value));
				return false;
			} else if (!_gate->visible(resolved->id.value)) {
				resolveError = u"The user hid this chat from the agent. Tell"
					" the user you can't see it and don't try to reach it"
					" another way."_q;
				return false;
			}
			peers.push_back(resolved);
			return true;
		};
		switch (entry.chatParam) {
		case ChatParam::None:
			break;
		case ChatParam::One:
		case ChatParam::OptionalOne: {
			const auto value = call.input.value("chat");
			if (value.isUndefined() || value.isNull()) {
				if (entry.chatParam == ChatParam::One) {
					resolveError = u"chat is required: a chat id or an"
						" @username."_q;
				}
				break;
			}
			if (resolve(value)) {
				peer = peers.back();
			}
			break;
		}
		case ChatParam::Many: {
			const auto value = call.input.value("chats");
			if (!value.isArray()) {
				resolveError = u"chats must be an array of chat ids or"
					" @usernames."_q;
				break;
			}
			const auto list = value.toArray();
			if (list.isEmpty() || list.size() > 100) {
				resolveError = u"chats must contain 1 to 100 entries."_q;
				break;
			}
			for (const auto &item : list) {
				if (!resolve(item)) {
					break;
				}
			}
			if (resolveError.isEmpty()) {
				auto unique = std::vector<not_null<PeerData*>>();
				for (const auto resolved : peers) {
					if (!ranges::contains(unique, resolved)) {
						unique.push_back(resolved);
					}
				}
				peers = std::move(unique);
			}
			break;
		}
		}
	}
	if (!resolveError.isEmpty()) {
		deliver(call.id, std::move(resolveError), true);
		return;
	}
	auto keys = std::vector<PeerKey>();
	keys.reserve(peers.size());
	for (const auto resolved : peers) {
		keys.push_back(resolved->id.value);
	}
	const auto decision = _gate->check({ entry.name, entry.policy, keys });
	meta.verdict = decision.verdict;
	meta.canAllowForTurn = (decision.verdict == Verdict::Confirm)
		&& decision.canAllowForTurn;
	if (decision.verdict == Verdict::Deny) {
		deliver(call.id, DenyText(decision), true);
		return;
	}
	raw->peer = peer;
	raw->peers = std::move(peers);
	const auto weak = base::make_weak(this);
	if (decision.verdict == Verdict::Confirm
		&& entry.policy.risk == Risk::Reversible) {
		confirmAction(call.id, entry, raw);
		if (!weak) {
			return;
		}
		for (const auto key : keys) {
			_touched.fire_copy(key);
		}
		return;
	}
	{
		const auto acting = _session->agent().actingGuard();
		entry.run(raw);
	}
	if (!weak) {
		return;
	}
	for (const auto key : keys) {
		_touched.fire_copy(key);
	}
}

void ToolRegistry::confirmCall(
		const QString &toolUseId,
		Card card,
		Fn<void(Card decided)> execute) {
	const auto i = _calls.find(toolUseId);
	if (i == _calls.end()) {
		return;
	}
	Expects(!i->second.hasCard);
	Expects(!i->second.approved);
	auto call = i->second;
	call.hasCard = true;
	call.execute = std::move(execute);
	card.toolUseId = toolUseId;
	card.status = CardStatus::Pending;
	if (CardAlwaysAsks(card.kind)) {
		call.canAllowForTurn = false;
	}
	card.canAllowForTurn = call.canAllowForTurn;
	const auto autoApproved = (call.verdict == Verdict::Allow)
		&& !CardAlwaysAsks(card.kind);
	if (autoApproved) {
		card.autoApproved = true;
		card.status = CardStatus::Running;
		call.approved = true;
		call.card = card;
	} else {
		call.card = card;
	}
	i->second = call;
	const auto weak = base::make_weak(this);
	_cardUpdates.fire_copy(card);
	if (autoApproved && weak) {
		auto run = call.execute;
		const auto acting = _session->agent().actingGuard();
		run(card);
	}
}

void ToolRegistry::confirmAction(
		const QString &toolUseId,
		const ToolEntry &entry,
		not_null<PendingCall*> call) {
	auto titles = QStringList();
	for (const auto peer : call->peers) {
		titles.push_back(peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: peer->name());
	}
	auto card = Card();
	card.kind = CardKind::Action;
	card.detail = entry.name;
	card.text = Ui::FormatTaskTool(entry.name)
		+ (titles.isEmpty() ? QString() : (u": "_q + titles.join(u", "_q)));
	const auto run = entry.run;
	confirmCall(toolUseId, std::move(card), [=](Card decided) {
		run(call);
	});
}

void ToolRegistry::finishCard(const QString &toolUseId, Card card) {
	const auto c = _calls.find(toolUseId);
	if (c == _calls.end()) {
		return;
	}
	const auto bad = c->second.hasCard
		? !c->second.approved
		: (c->second.verdict == Verdict::Confirm);
	const auto tool = c->second.tool;
	const auto untrusted = c->second.untrusted;
	if (bad || !CardFinal(card.status)) {
		LOG(("Agent Warning: tool %1 finished the call %2 not through an"
			" approved card.").arg(tool, toolUseId));
		_calls.erase(c);
		deliver(toolUseId, u"This action was not confirmed."_q, true, false);
		return;
	}
	card.toolUseId = toolUseId;
	_calls.erase(c);
	const auto weak = base::make_weak(this);
	_cardUpdates.fire_copy(card);
	if (!weak) {
		return;
	}
	deliver(
		toolUseId,
		CardResultText(card),
		card.status == CardStatus::Failed,
		untrusted);
}

void ToolRegistry::loadGroup(const QString &toolUseId, QString group) {
	if (!_pending.contains(toolUseId)) {
		return;
	}
	if (group != kFoldersGroup
		&& group != kFilesGroup
		&& group != kTasksGroup) {
		deliver(
			toolUseId,
			u"Unknown tool group: %1. The groups are \"folders\","
			" \"files\" and \"tasks\"."_q.arg(
				group.isEmpty() ? u"(empty)"_q : group),
			true);
		return;
	}
	auto json = QJsonObject();
	json.insert("group", group);
	json.insert("loaded", QJsonValue(true));
	auto tools = QJsonArray();
	for (const auto &entry : ToolsTable()) {
		if (entry.group == group) {
			tools.push_back(entry.name);
		}
	}
	json.insert("tools", tools);
	if (ranges::contains(_loadedGroups, group)) {
		deliverJson(toolUseId, std::move(json));
		return;
	}
	_loadedGroups.push_back(group);
	const auto weak = base::make_weak(this);
	_loadedGroupsChanges.fire({});
	if (!weak) {
		return;
	}
	deliverJson(toolUseId, std::move(json));
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

rpl::producer<HelperUsage> ToolRegistry::helperUsage() const {
	return _helperUsage.events();
}

void ToolRegistry::decide(
		const QString &toolUseId,
		CardDecision decision) {
	const auto i = _calls.find(toolUseId);
	if (i == _calls.end()
		|| !i->second.hasCard
		|| i->second.card.status != CardStatus::Pending) {
		return;
	}
	auto call = i->second;
	auto card = ApplyDecision(call.card, decision);
	if (decision.action == CardAction::Decline) {
		_calls.erase(i);
		_cardUpdates.fire_copy(card);
		deliver(toolUseId, CardResultText(card), false, call.untrusted);
		return;
	}
	if (decision.allowForTurn && call.canAllowForTurn) {
		_gate->allowForTurn({ call.tool, call.policy, CheckedPeers(card) });
	}
	call.card = card;
	call.approved = true;
	i->second = call;
	const auto weak = base::make_weak(this);
	_cardUpdates.fire_copy(card);
	if (!weak) {
		return;
	}
	auto run = call.execute;
	const auto acting = _session->agent().actingGuard();
	run(card);
}

void ToolRegistry::expire(const QString &toolUseId) {
	const auto i = _calls.find(toolUseId);
	if (i == _calls.end()
		|| !i->second.hasCard
		|| i->second.card.status != CardStatus::Pending) {
		return;
	}
	const auto untrusted = i->second.untrusted;
	auto card = i->second.card;
	card.status = CardStatus::Expired;
	_calls.erase(i);
	const auto weak = base::make_weak(this);
	_cardUpdates.fire_copy(card);
	if (!weak) {
		return;
	}
	deliver(toolUseId, CardResultText(card), false, untrusted);
}

bool ToolRegistry::canUndo(const QString &toolUseId) const {
	return _undo.contains(toolUseId);
}

void ToolRegistry::undo(
		const QString &toolUseId,
		Fn<void(QString error)> done) {
	const auto i = _undo.find(toolUseId);
	if (i == _undo.end()) {
		done(u"This action cannot be undone."_q);
		return;
	}
	const auto weak = base::make_weak(this);
	const auto run = i->second;
	run([weak, this, id = toolUseId, done = std::move(done)](
			QString error) {
		if (!weak) {
			return;
		} else if (!error.isEmpty()) {
			done(std::move(error));
			return;
		}
		_undo.erase(id);
		const auto finished = _finished.find(id);
		if (finished != _finished.end()) {
			_finished.erase(finished);
		}
		done(QString());
	});
}

void ToolRegistry::cancel(const QString &toolUseId) {
	const auto i = _pending.find(toolUseId);
	if (i == _pending.end()) {
		return;
	}
	auto entry = std::move(i->second);
	_pending.erase(i);
	const auto c = _calls.find(toolUseId);
	if (c != _calls.end()) {
		auto card = c->second.card;
		const auto running = (card.status == CardStatus::Running);
		_calls.erase(c);
		if (running) {
			card.status = CardStatus::NotDone;
		} else {
			card.status = CardStatus::Declined;
			card.decided = CardAction::Decline;
		}
		_cardUpdates.fire_copy(std::move(card));
	}
	entry->cancel();
	if (_undo.contains(toolUseId)) {
		_finished.emplace(toolUseId, std::move(entry));
	} else {
		crl::on_main([holder = std::move(entry)] {});
	}
}

void ToolRegistry::deliver(
		const QString &toolUseId,
		QString content,
		bool error,
		std::vector<MediaBlock> media) {
	if (!_pending.contains(toolUseId)) {
		return;
	}
	const auto c = _calls.find(toolUseId);
	const auto call = (c != _calls.end()) ? c->second : Call();
	if (c != _calls.end()) {
		_calls.erase(c);
	}
	if (!error && !call.approved
		&& (call.verdict == Verdict::Confirm || call.hasCard)) {
		LOG(("Agent Warning: tool %1 finished the call %2 not through an"
			" approved card.").arg(call.tool, toolUseId));
		error = true;
	}
	if (call.hasCard && !CardFinal(call.card.status)) {
		auto card = call.card;
		card.status = (call.approved && !error)
			? CardStatus::Done
			: CardStatus::Failed;
		const auto weak = base::make_weak(this);
		_cardUpdates.fire_copy(card);
		if (!weak) {
			return;
		}
	}
	deliver(
		toolUseId,
		std::move(content),
		error,
		call.untrusted,
		std::move(media));
}

void ToolRegistry::deliver(
		const QString &toolUseId,
		QString content,
		bool error,
		bool untrusted,
		std::vector<MediaBlock> media) {
	const auto i = _pending.find(toolUseId);
	if (i == _pending.end()) {
		return;
	}
	auto entry = std::move(i->second);
	_pending.erase(i);
	content = CapToolResult(std::move(content), kToolResultMaxChars);
	if (!error
		&& (_gate->scope().kind == ScopeKind::Chat || untrusted)) {
		content = WrapUntrusted(content);
		_gate->markTainted();
	}
	entry->result(ToolResultBlock{
		.toolUseId = toolUseId,
		.content = std::move(content),
		.error = error,
		.media = error ? std::vector<MediaBlock>() : std::move(media),
	});
	if (_undo.contains(toolUseId)) {
		_finished.emplace(toolUseId, std::move(entry));
	} else {
		crl::on_main([holder = std::move(entry)] {});
	}
}

void ToolRegistry::deliverJson(
		const QString &toolUseId,
		QJsonObject json,
		std::vector<MediaBlock> media) {
	deliver(
		toolUseId,
		QString::fromUtf8(
			QJsonDocument(std::move(json)).toJson(QJsonDocument::Compact)),
		false,
		std::move(media));
}

void ToolRegistry::askMediaModel(
		not_null<PendingCall*> call,
		MediaBlock media,
		QString question,
		Fn<void(QString text, QString error)> done) {
	const auto provider = _session->agent().mediaProvider();
	auto adapter = provider ? CreateAdapter(*provider) : nullptr;
	if (!adapter) {
		done(QString(), u"No media model is set up."_q);
		return;
	}
	const auto settings = *provider;
	const auto weak = base::make_weak(this);
	const auto holder = call->lifetime.make_state<
		std::unique_ptr<SingleRequest>>();
	*holder = std::make_unique<SingleRequest>(
		std::move(adapter),
		&_session->agent().transport(),
		HelperRequest(settings.model, std::move(media), question),
		[=, done = std::move(done)](Response response) {
			if (!weak) {
				return;
			}
			_helperUsage.fire({ response.usage, settings });
			if (!weak) {
				return;
			} else if (response.error) {
				done(
					QString(),
					u"The media model failed: %1"_q.arg(
						response.error->text.isEmpty()
							? ErrorKindName(response.error->kind)
							: response.error->text));
				return;
			}
			const auto text = ResponseText(response.message).trimmed();
			if (text.isEmpty()) {
				done(QString(), u"The media model gave no answer."_q);
			} else {
				done(text, QString());
			}
		});
}

} // namespace Agent
