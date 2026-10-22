/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_adapter.h"
#include "agent/core/agent_media.h"
#include "agent/core/agent_providers.h"
#include "agent/core/agent_record.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>
#include <rpl/variable.h>

#include <memory>
#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class AgentChat;
class Drafts;
class Attachments;
class ChatGptTransport;
class ClaudeLauncher;
class ClaudePool;
class Exclusions;
class HttpTransport;
class PendingRequest;
class Memories;
class Session;
class SessionStore;
class SpendingTracker;
class TaskRunner;
class Tasks;
class Transport;
struct RunSetup;
struct Task;

struct ScopeActivity {
	bool working = false;
	int pendingCards = 0;

	friend inline bool operator==(
		const ScopeActivity &,
		const ScopeActivity &) = default;
};

class ActingGuard final {
public:
	explicit ActingGuard(not_null<int*> counter);
	ActingGuard(const ActingGuard &) = delete;
	ActingGuard &operator=(const ActingGuard &) = delete;
	~ActingGuard();

private:
	const not_null<int*> _counter;

};

class Manager final : public base::has_weak_ptr {
public:
	explicit Manager(not_null<Main::Session*> session);
	~Manager();

	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] bool available() const;
	[[nodiscard]] bool enabled() const;
	[[nodiscard]] rpl::producer<bool> enabledValue() const;
	[[nodiscard]] const ProvidersState &providers() const;
	[[nodiscard]] int contextWindow(const ProviderSettings &provider) const;
	[[nodiscard]] MediaAbilities abilities(
		const ProviderSettings &provider) const;
	[[nodiscard]] const ProviderSettings *sessionProvider(
		const QString &profileId) const;
	[[nodiscard]] const ProviderSettings *mediaProvider() const;
	[[nodiscard]] const ProviderSettings *taskProvider(
		const Task &task) const;

	[[nodiscard]] std::vector<SessionInfo> list(const Scope &scope) const;
	[[nodiscard]] const std::vector<SessionInfo> &listAll() const;
	[[nodiscard]] std::optional<SessionInfo> info(const QString &id) const;
	[[nodiscard]] rpl::producer<Scope> listChanges() const;
	[[nodiscard]] ScopeActivity activity(const Scope &scope) const;
	[[nodiscard]] rpl::producer<ScopeActivity> activityValue(
		const Scope &scope) const;

	[[nodiscard]] not_null<Session*> create(const Scope &scope);
	[[nodiscard]] not_null<Session*> startRun(
		SessionRecord record,
		RunSetup setup);
	[[nodiscard]] Session *open(const QString &id);
	[[nodiscard]] Session *loaded(const QString &id) const;
	void rename(const QString &id, const QString &title);
	void remove(const QString &id);
	void unload(const QString &id);
	void clearAllData();

	[[nodiscard]] AgentChat &chat() const;
	[[nodiscard]] Drafts &drafts() const;
	[[nodiscard]] Memories &memories() const;
	[[nodiscard]] Exclusions &exclusions() const;
	[[nodiscard]] SpendingTracker &spending() const;
	[[nodiscard]] Attachments &attachments() const;
	[[nodiscard]] Tasks &tasks() const;
	[[nodiscard]] TaskRunner &runner() const;
	void removeTask(const QString &id);

	[[nodiscard]] ActingGuard actingGuard();
	[[nodiscard]] bool acting() const;

	[[nodiscard]] Transport &transport() const;
	[[nodiscard]] Transport &transportFor(
		const ProviderSettings &provider) const;
	[[nodiscard]] bool canRun(const ProviderSettings &provider) const;
	[[nodiscard]] std::unique_ptr<PendingRequest> single(
		const ProviderSettings &provider,
		Request request,
		Fn<void(Response)> done);
	[[nodiscard]] ClaudeLauncher &claudeLauncher() const;
	[[nodiscard]] ClaudePool &claudePool() const;
	void save(not_null<Session*> session);

private:
	void refresh();
	void updateEnabled();
	void startModels(const ProviderSettings &provider);
	void learnModels();
	void finishModels(
		const QString &baseUrl,
		const QByteArray &body,
		const QString &error);
	[[nodiscard]] Session *findOpen(const QString &id) const;
	void stopExcluded();
	void watchActivity(not_null<Session*> session);
	void updateActivity(const Scope &scope);
	void upsertIndex(const SessionInfo &info);
	void sortIndex();

	const not_null<Main::Session*> _session;
	const std::unique_ptr<SessionStore> _store;
	const std::unique_ptr<HttpTransport> _transport;
	const std::unique_ptr<ChatGptTransport> _chatGptTransport;
	const std::unique_ptr<ClaudeLauncher> _claudeLauncher;
	const std::unique_ptr<ClaudePool> _claudePool;
	const std::unique_ptr<Drafts> _drafts;
	const std::unique_ptr<Memories> _memories;
	const std::unique_ptr<Exclusions> _exclusions;
	const std::unique_ptr<SpendingTracker> _spending;
	const std::unique_ptr<Attachments> _attachments;
	const std::unique_ptr<Tasks> _tasks;
	std::unique_ptr<TaskRunner> _runner;
	std::unique_ptr<AgentChat> _chat;
	ProvidersState _providers;
	int _acting = 0;
	rpl::variable<bool> _enabled = false;
	std::vector<SessionInfo> _index;
	std::vector<std::unique_ptr<Session>> _sessions;
	base::flat_map<QString, int> _learnedContext;
	base::flat_map<QString, MediaAbilities> _learnedAbilities;
	base::flat_set<QString> _learnedUrls;
	base::flat_set<QString> _learning;
	rpl::event_stream<Scope> _listChanges;
	mutable base::flat_map<
		PeerKey,
		std::unique_ptr<rpl::variable<ScopeActivity>>> _activity;
	rpl::lifetime _lifetime;

};

} // namespace Agent
