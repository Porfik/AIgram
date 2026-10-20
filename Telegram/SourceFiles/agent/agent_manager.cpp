/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_manager.h"

#include "agent/agent_attachments.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_http_transport.h"
#include "agent/agent_memories.h"
#include "agent/agent_session.h"
#include "agent/agent_session_store.h"
#include "agent/agent_settings.h"
#include "agent/agent_spending_tracker.h"
#include "agent/agent_task_runner.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_openai.h"
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <rpl/producer.h>

#include <algorithm>

namespace Agent {
namespace {

[[nodiscard]] QString ModelsKey(
		const QString &baseUrl,
		const QString &model) {
	return baseUrl + '\n' + model;
}

} // namespace

Manager::Manager(not_null<Main::Session*> session)
: _session(session)
, _store(std::make_unique<SessionStore>(session))
, _transport(std::make_unique<HttpTransport>())
, _memories(std::make_unique<Memories>(session))
, _exclusions(std::make_unique<Exclusions>(session))
, _spending(std::make_unique<SpendingTracker>(session))
, _attachments(std::make_unique<Attachments>(session))
, _tasks(std::make_unique<Tasks>(session)) {
	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	_index = _store->readIndex();
	sortIndex();
	_exclusions->changes(
	) | rpl::on_next([=] {
		stopExcluded();
	}, _lifetime);
	refresh();
	_runner = std::make_unique<TaskRunner>(this);
}

Manager::~Manager() {
	_runner = nullptr;
}

Main::Session &Manager::session() const {
	return *_session;
}

void Manager::refresh() {
	_providers = LoadProviders();
	updateEnabled();
	learnContextWindow();
}

void Manager::updateEnabled() {
	_enabled = available() && _providers.showEntryPoints;
}

bool Manager::available() const {
	const auto provider = ActiveProvider(_providers);
	if (!provider) {
		return false;
	}
	return CreateAdapter(*provider) != nullptr;
}

bool Manager::enabled() const {
	return _enabled.current();
}

rpl::producer<bool> Manager::enabledValue() const {
	return _enabled.value();
}

const ProvidersState &Manager::providers() const {
	return _providers;
}

int Manager::contextWindow() const {
	const auto provider = ActiveProvider(_providers);
	if (!provider) {
		return kDefaultContextWindow;
	}
	if (provider->contextWindow > 0) {
		return provider->contextWindow;
	}
	const auto i = _learnedContext.find(
		ModelsKey(provider->baseUrl, provider->model));
	return (i != _learnedContext.cend() && i->second > 0)
		? i->second
		: kDefaultContextWindow;
}

MediaAbilities Manager::abilities(const ProviderSettings &provider) const {
	auto result = MediaAbilities{
		.images = provider.vision,
		.audio = provider.audio,
		.video = provider.video,
		.files = provider.files,
	};
	const auto i = _learnedAbilities.find(
		ModelsKey(provider.baseUrl, provider.model));
	if (i != _learnedAbilities.end()) {
		result.images = result.images || i->second.images;
		result.audio = result.audio || i->second.audio;
		result.video = result.video || i->second.video;
		result.files = result.files || i->second.files;
	}
	return result;
}

const ProviderSettings *Manager::mediaProvider() const {
	const auto result = MediaProvider(_providers);
	return (result && CreateAdapter(*result)) ? result : nullptr;
}

const ProviderSettings *Manager::taskProvider(const Task &task) const {
	const auto result = BackgroundProvider(_providers, task.providerId);
	return (result && CreateAdapter(*result)) ? result : nullptr;
}

void Manager::startModels(const ProviderSettings &provider) {
	if (provider.kind != ProviderKind::OpenAiCompatible
		|| provider.baseUrl.isEmpty()
		|| provider.model.isEmpty()) {
		return;
	}
	const auto key = ModelsKey(provider.baseUrl, provider.model);
	if (_learnedAbilities.contains(key) || _learning.contains(key)) {
		return;
	}
	_learning.insert(key);
	const auto body = std::make_shared<QByteArray>();
	const auto model = provider.model;
	_transport->start(
		OpenAi::ModelsRequest(provider),
		TransportHandlers{
			[](HttpHead) {},
			crl::guard(this, [=](QByteArray chunk) {
				*body += chunk;
			}),
			crl::guard(this, [=](QString error) {
				finishModels(key, model, std::move(*body), std::move(error));
			}),
		});
}

void Manager::learnContextWindow() {
	const auto active = ActiveProvider(_providers);
	if (active) {
		startModels(*active);
	}
	const auto media = MediaProvider(_providers);
	if (media && (!active || media->id != active->id)) {
		startModels(*media);
	}
}

void Manager::finishModels(
		const QString &key,
		const QString &model,
		const QByteArray &body,
		const QString &error) {
	_learning.remove(key);
	if (!error.isEmpty()) {
		LOG(("Agent Info: models request failed."));
		return;
	}
	for (const auto &entry : OpenAi::ParseModels(body)) {
		if (entry.id != model) {
			continue;
		}
		if (entry.contextWindow > 0) {
			_learnedContext.emplace(key, entry.contextWindow);
		}
		if (entry.vision.has_value()
			|| entry.audio.has_value()
			|| entry.video.has_value()
			|| entry.files.has_value()) {
			_learnedAbilities.emplace(key, MediaAbilities{
				.images = entry.vision.value_or(false),
				.audio = entry.audio.value_or(false),
				.video = entry.video.value_or(false),
				.files = entry.files.value_or(false),
			});
		}
		return;
	}
	LOG(("Agent Info: abilities of the model are unknown."));
}

std::vector<SessionInfo> Manager::list(const Scope &scope) const {
	auto result = std::vector<SessionInfo>();
	for (const auto &entry : _index) {
		if (entry.scope == scope) {
			result.push_back(entry);
		}
	}
	return result;
}

std::optional<SessionInfo> Manager::info(const QString &id) const {
	if (const auto session = findOpen(id)) {
		return session->record().info;
	}
	for (const auto &entry : _index) {
		if (entry.id == id) {
			return entry;
		}
	}
	return std::nullopt;
}

rpl::producer<Scope> Manager::listChanges() const {
	return _listChanges.events();
}

ScopeActivity Manager::activity(const Scope &scope) const {
	auto result = ScopeActivity();
	for (const auto &session : _sessions) {
		if (!(session->scope() == scope)) {
			continue;
		}
		const auto &state = session->state();
		if (state.status == SessionStatus::Working
			|| state.status == SessionStatus::Compacting) {
			result.working = true;
		}
		result.pendingCards += state.pendingCards;
	}
	return result;
}

rpl::producer<ScopeActivity> Manager::activityValue(
		const Scope &scope) const {
	auto &variable = _activity[scope.peer];
	if (!variable) {
		variable = std::make_unique<rpl::variable<ScopeActivity>>(
			activity(scope));
	}
	return variable->value();
}

void Manager::watchActivity(not_null<Session*> session) {
	session->stateChanges(
	) | rpl::on_next([=] {
		updateActivity(session->scope());
	}, _lifetime);
}

void Manager::updateActivity(const Scope &scope) {
	const auto i = _activity.find(scope.peer);
	if (i != _activity.end()) {
		*i->second = activity(scope);
	}
}

not_null<Session*> Manager::create(const Scope &scope) {
	auto record = SessionRecord();
	record.info.id = GenerateSessionId();
	record.info.scope = scope;
	record.info.created = record.info.updated = base::unixtime::now();
	_sessions.push_back(std::make_unique<Session>(this, std::move(record)));
	const auto result = _sessions.back().get();
	watchActivity(result);
	return result;
}

Session *Manager::findOpen(const QString &id) const {
	for (const auto &session : _sessions) {
		if (session->id() == id) {
			return session.get();
		}
	}
	return nullptr;
}

Session *Manager::open(const QString &id) {
	if (const auto session = findOpen(id)) {
		return session;
	}
	auto record = _store->readSession(id);
	if (!record) {
		return nullptr;
	}
	_sessions.push_back(std::make_unique<Session>(this, std::move(*record)));
	const auto result = _sessions.back().get();
	result->finishOpen();
	watchActivity(result);
	return result;
}

void Manager::rename(const QString &id, const QString &title) {
	const auto trimmed = title.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	if (const auto session = findOpen(id)) {
		session->applyRename(trimmed);
		return;
	}
	auto record = _store->readSession(id);
	if (!record) {
		return;
	}
	record->info.title = trimmed;
	record->info.titleFinal = true;
	if (const auto known = info(id)) {
		record->info.preview = known->preview;
		record->info.previewOutgoing = known->previewOutgoing;
	}
	_store->writeSession(*record);
	upsertIndex(record->info);
	_store->writeIndex(_index);
	_listChanges.fire_copy(record->info.scope);
}

void Manager::remove(const QString &id) {
	auto scope = Scope::Account();
	auto known = false;
	auto attachments = 0;
	if (const auto session = findOpen(id)) {
		scope = session->scope();
		attachments = session->record().info.attachments;
		known = true;
		session->stop();
	}
	const auto it = std::find_if(
		begin(_sessions),
		end(_sessions),
		[&](const auto &session) { return session->id() == id; });
	if (it != end(_sessions)) {
		_sessions.erase(it);
		updateActivity(scope);
	}
	for (const auto &entry : _index) {
		if (entry.id == id) {
			scope = entry.scope;
			attachments = std::max(attachments, entry.attachments);
			known = true;
			break;
		}
	}
	_store->removeSession(id);
	for (auto i = _index.begin(); i != _index.end();) {
		if (i->id == id) {
			i = _index.erase(i);
		} else {
			++i;
		}
	}
	_store->writeIndex(_index);
	if (attachments > 0) {
		_attachments->remove(id, attachments);
	}
	if (known) {
		_listChanges.fire_copy(scope);
	}
}

void Manager::stopExcluded() {
	auto stopping = std::vector<not_null<Session*>>();
	for (const auto &session : _sessions) {
		const auto &scope = session->scope();
		if (scope.kind == ScopeKind::Chat
			&& _exclusions->excluded(scope.peer)) {
			stopping.push_back(session.get());
		}
	}
	for (const auto session : stopping) {
		session->stop();
	}
}

void Manager::clearAllData() {
	auto scopes = std::vector<Scope>();
	const auto addScope = [&](const Scope &scope) {
		if (!ranges::contains(scopes, scope)) {
			scopes.push_back(scope);
		}
	};
	auto attachments = base::flat_map<QString, int>();
	for (const auto &session : _sessions) {
		addScope(session->scope());
		attachments.emplace(
			session->id(),
			session->record().info.attachments);
	}
	for (const auto &entry : _index) {
		addScope(entry.scope);
		attachments.emplace(entry.id, entry.attachments);
	}
	_runner->stopAll();
	for (const auto &task : _tasks->list()) {
		for (const auto &run : _tasks->runs(task.id)) {
			_store->removeSession(run.sessionId);
		}
	}
	_tasks->clearAll();
	auto sessions = base::take(_sessions);
	for (const auto &session : sessions) {
		session->stop();
	}
	for (const auto &entry : _index) {
		_store->removeSession(entry.id);
	}
	for (const auto &entry : attachments) {
		if (entry.second > 0) {
			_attachments->remove(entry.first, entry.second);
		}
	}
	_index.clear();
	_session->local().removeAgentValue(u"index"_q);
	_memories->clearAll();
	_spending->reset();
	sessions.clear();
	for (const auto &scope : scopes) {
		updateActivity(scope);
		_listChanges.fire_copy(scope);
	}
}

Memories &Manager::memories() const {
	return *_memories;
}

Exclusions &Manager::exclusions() const {
	return *_exclusions;
}

Attachments &Manager::attachments() const {
	return *_attachments;
}

SpendingTracker &Manager::spending() const {
	return *_spending;
}

Tasks &Manager::tasks() const {
	return *_tasks;
}

TaskRunner &Manager::runner() const {
	return *_runner;
}

void Manager::removeTask(const QString &id) {
	_runner->taskRemoved(id);
	for (const auto &run : _tasks->runs(id)) {
		if (const auto session = findOpen(run.sessionId)) {
			session->stop();
		}
		const auto it = std::find_if(
			begin(_sessions),
			end(_sessions),
			[&](const auto &session) {
				return session->id() == run.sessionId;
			});
		if (it != end(_sessions)) {
			_sessions.erase(it);
		}
		_store->removeSession(run.sessionId);
	}
	_tasks->remove(id);
}

void Manager::upsertIndex(const SessionInfo &info) {
	for (auto &entry : _index) {
		if (entry.id == info.id) {
			entry = info;
			sortIndex();
			return;
		}
	}
	_index.push_back(info);
	sortIndex();
}

void Manager::sortIndex() {
	std::stable_sort(
		begin(_index),
		end(_index),
		[](const SessionInfo &a, const SessionInfo &b) {
			return a.updated > b.updated;
		});
}

Transport &Manager::transport() const {
	return *_transport;
}

void Manager::save(not_null<Session*> session) {
	const auto &record = session->record();
	_store->writeSession(record);
	upsertIndex(record.info);
	_store->writeIndex(_index);
	_listChanges.fire_copy(record.info.scope);
}

} // namespace Agent
