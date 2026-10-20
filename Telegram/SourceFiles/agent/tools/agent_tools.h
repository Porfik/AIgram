/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_engine.h"
#include "agent/core/agent_policy.h"
#include "agent/core/agent_prompts.h"
#include "agent/core/agent_providers.h"
#include "base/flat_set.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>

#include <QtCore/QJsonObject>

#include <map>
#include <memory>
#include <vector>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Agent {

struct PendingCall;

struct HelperUsage {
	Usage usage;
	ProviderSettings provider;
};

class ToolRegistry final
	: public ToolExecutor
	, public base::has_weak_ptr {
public:
	ToolRegistry(
		not_null<Main::Session*> session,
		not_null<PolicyGate*> gate,
		std::vector<QString> loadedGroups = {},
		QString sessionId = QString());
	~ToolRegistry();

	[[nodiscard]] std::vector<ToolSpec> tools() const override;
	void execute(
		ToolUseBlock call,
		Fn<void(ToolResultBlock)> done) override;
	void cancel(const QString &toolUseId) override;

	[[nodiscard]] rpl::producer<PeerKey> touched() const;
	[[nodiscard]] rpl::producer<Card> cardUpdates() const;
	[[nodiscard]] const std::vector<QString> &loadedGroups() const;
	[[nodiscard]] rpl::producer<> loadedGroupsChanges() const;
	[[nodiscard]] rpl::producer<HelperUsage> helperUsage() const;

	void decide(const QString &toolUseId, CardDecision decision);
	[[nodiscard]] bool canUndo(const QString &toolUseId) const;
	void undo(
		const QString &toolUseId,
		Fn<void(QString error)> done);

private:
	struct Call;

	void confirmCall(
		const QString &toolUseId,
		Card card,
		Fn<void(Card decided)> execute);
	void finishCard(const QString &toolUseId, Card card);
	void loadGroup(const QString &toolUseId, QString group);
	void deliver(
		const QString &toolUseId,
		QString content,
		bool error);
	void deliver(
		const QString &toolUseId,
		QString content,
		bool error,
		bool untrusted);
	void deliverJson(
		const QString &toolUseId,
		QJsonObject json);

	const not_null<Main::Session*> _session;
	const not_null<PolicyGate*> _gate;
	const QString _sessionId;
	rpl::event_stream<PeerKey> _touched;
	rpl::event_stream<HelperUsage> _helperUsage;
	std::map<QString, std::unique_ptr<PendingCall>, std::less<>> _pending;
	std::vector<QString> _loadedGroups;
	rpl::event_stream<Card> _cardUpdates;
	rpl::event_stream<> _loadedGroupsChanges;
	std::map<QString, Call, std::less<>> _calls;
	std::map<QString, std::unique_ptr<PendingCall>, std::less<>> _finished;
	std::map<
		QString,
		Fn<void(Fn<void(QString error)> finished)>,
		std::less<>> _undo;

};

[[nodiscard]] const base::flat_set<QString> &ActionToolNames();

[[nodiscard]] ChatBrief MakeChatBrief(not_null<PeerData*> peer);
[[nodiscard]] std::vector<PeerName> NameCandidates(
	not_null<Main::Session*> session);
[[nodiscard]] QString AccountLine(not_null<Main::Session*> session);

} // namespace Agent
