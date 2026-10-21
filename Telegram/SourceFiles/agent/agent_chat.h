/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_chat_state.h"
#include "base/timer.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>
#include <rpl/variable.h>

#include <QtGui/QImage>

class History;
class PeerData;
class UserData;

namespace Dialogs {
class Key;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Agent {

class Manager;
struct ScopeActivity;

[[nodiscard]] PeerId AgentPeerId();
[[nodiscard]] bool IsAgentPeer(PeerId id);
[[nodiscard]] bool IsAgentPeer(const PeerData *peer);
[[nodiscard]] bool IsAgentHistory(const History *history);
[[nodiscard]] bool ShowBotIcon(not_null<UserData*> user);
[[nodiscard]] int CountPinnedForLimit(const std::vector<Dialogs::Key> &order);

[[nodiscard]] QImage DefaultAgentPhoto(int size);
[[nodiscard]] QString AgentName(const QString &custom);

class AgentChat final {
public:
	explicit AgentChat(not_null<Manager*> manager);
	~AgentChat();

	[[nodiscard]] not_null<UserData*> user() const;
	[[nodiscard]] not_null<History*> history() const;

	[[nodiscard]] bool shown() const;
	[[nodiscard]] rpl::producer<bool> shownValue() const;
	[[nodiscard]] bool pinned() const;
	void setPinned(bool pinned);

	[[nodiscard]] QString name() const;
	[[nodiscard]] rpl::producer<QString> nameValue() const;
	[[nodiscard]] QImage photo() const;

private:
	void refreshProfile();
	void refreshShown();
	void refreshPreview();
	void refreshActivity(const ScopeActivity &activity);
	void sendTyping();
	void applyPinned();
	void pinnedOrderChanged();
	void loadState();
	void saveState();
	void removePreview();

	const not_null<Manager*> _manager;
	const not_null<Main::Session*> _session;
	const not_null<UserData*> _user;
	const not_null<History*> _history;
	ChatState _state;
	FullMsgId _previewId;
	QString _previewKey;
	rpl::variable<QString> _name;
	QByteArray _photoBytes;
	bool _photoKnown = false;
	bool _showBotIcon = true;
	rpl::variable<bool> _shown = false;
	bool _applyingPin = false;
	bool _working = false;
	base::Timer _typingTimer;
	rpl::lifetime _lifetime;

};

} // namespace Agent
