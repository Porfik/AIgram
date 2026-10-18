/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_providers.h"
#include "agent/core/agent_record.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>
#include <rpl/variable.h>

#include <memory>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class HttpTransport;
class Session;
class SessionStore;
class Transport;

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

	[[nodiscard]] std::vector<SessionInfo> list(const Scope &scope) const;
	[[nodiscard]] rpl::producer<Scope> listChanges() const;

	[[nodiscard]] not_null<Session*> create(const Scope &scope);
	[[nodiscard]] Session *open(const QString &id);
	void rename(const QString &id, const QString &title);
	void remove(const QString &id);

	[[nodiscard]] Transport &transport() const;
	void save(not_null<Session*> session);

private:
	const not_null<Main::Session*> _session;
	const std::unique_ptr<SessionStore> _store;
	const std::unique_ptr<HttpTransport> _transport;
	ProvidersState _providers;
	rpl::variable<bool> _enabled = false;
	std::vector<SessionInfo> _index;
	std::vector<std::unique_ptr<Session>> _sessions;
	rpl::event_stream<Scope> _listChanges;
	rpl::lifetime _lifetime;

};

} // namespace Agent
