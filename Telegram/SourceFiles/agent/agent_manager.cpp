/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_manager.h"

#include "agent/agent_http_transport.h"
#include "agent/agent_session.h"
#include "agent/agent_session_store.h"
#include "agent/agent_settings.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_openai.h"
#include "base/algorithm.h"
#include "base/unixtime.h"

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
, _transport(std::make_unique<HttpTransport>()) {
	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	_index = _store->readIndex();
	sortIndex();
	refresh();
}

Manager::~Manager() = default;

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

void Manager::learnContextWindow() {
	const auto provider = ActiveProvider(_providers);
	if (!provider
		|| provider->contextWindow > 0
		|| provider->kind != ProviderKind::OpenAiCompatible
		|| provider->baseUrl.isEmpty()
		|| provider->model.isEmpty()) {
		return;
	}
	const auto key = ModelsKey(provider->baseUrl, provider->model);
	if (_learnedContext.contains(key) || _learning.contains(key)) {
		return;
	}
	_learning.insert(key);
	const auto body = std::make_shared<QByteArray>();
	const auto model = provider->model;
	_transport->start(
		OpenAi::ModelsRequest(*provider),
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

void Manager::finishModels(
		const QString &key,
		const QString &model,
		const QByteArray &body,
		const QString &error) {
	_learning.remove(key);
	if (!error.isEmpty()) {
		LOG(("Agent Info: context window request failed."));
		return;
	}
	for (const auto &entry : OpenAi::ParseModels(body)) {
		if (entry.id == model && entry.contextWindow > 0) {
			_learnedContext.emplace(key, entry.contextWindow);
			return;
		}
	}
	LOG(("Agent Info: context window of the model is unknown."));
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
	_store->writeSession(*record);
	upsertIndex(record->info);
	_store->writeIndex(_index);
	_listChanges.fire_copy(record->info.scope);
}

void Manager::remove(const QString &id) {
	auto scope = Scope::Account();
	auto known = false;
	if (const auto session = findOpen(id)) {
		scope = session->scope();
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
	if (known) {
		_listChanges.fire_copy(scope);
	}
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
