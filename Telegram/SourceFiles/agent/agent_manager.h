/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

class Attachments;
class Exclusions;
class HttpTransport;
class Memories;
class Session;
class SessionStore;
class SpendingTracker;
class Transport;

struct ScopeActivity {
	bool working = false;
	int pendingCards = 0;

	friend inline bool operator==(
		const ScopeActivity &,
		const ScopeActivity &) = default;
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
	[[nodiscard]] int contextWindow() const;
	[[nodiscard]] MediaAbilities abilities(
		const ProviderSettings &provider) const;
	[[nodiscard]] const ProviderSettings *mediaProvider() const;

	[[nodiscard]] std::vector<SessionInfo> list(const Scope &scope) const;
	[[nodiscard]] std::optional<SessionInfo> info(const QString &id) const;
	[[nodiscard]] rpl::producer<Scope> listChanges() const;
	[[nodiscard]] ScopeActivity activity(const Scope &scope) const;
	[[nodiscard]] rpl::producer<ScopeActivity> activityValue(
		const Scope &scope) const;

	[[nodiscard]] not_null<Session*> create(const Scope &scope);
	[[nodiscard]] Session *open(const QString &id);
	void rename(const QString &id, const QString &title);
	void remove(const QString &id);
	void clearAllData();

	[[nodiscard]] Memories &memories() const;
	[[nodiscard]] Exclusions &exclusions() const;
	[[nodiscard]] SpendingTracker &spending() const;
	[[nodiscard]] Attachments &attachments() const;

	[[nodiscard]] Transport &transport() const;
	void save(not_null<Session*> session);

private:
	void refresh();
	void updateEnabled();
	void startModels(const ProviderSettings &provider);
	void learnContextWindow();
	void finishModels(
		const QString &key,
		const QString &model,
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
	const std::unique_ptr<Memories> _memories;
	const std::unique_ptr<Exclusions> _exclusions;
	const std::unique_ptr<SpendingTracker> _spending;
	const std::unique_ptr<Attachments> _attachments;
	ProvidersState _providers;
	rpl::variable<bool> _enabled = false;
	std::vector<SessionInfo> _index;
	std::vector<std::unique_ptr<Session>> _sessions;
	base::flat_map<QString, int> _learnedContext;
	base::flat_map<QString, MediaAbilities> _learnedAbilities;
	base::flat_set<QString> _learning;
	rpl::event_stream<Scope> _listChanges;
	mutable base::flat_map<
		PeerKey,
		std::unique_ptr<rpl::variable<ScopeActivity>>> _activity;
	rpl::lifetime _lifetime;

};

} // namespace Agent
