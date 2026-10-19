/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "agent/ui/agent_composer.h"
#include "agent/ui/agent_feed_widget.h"
#include "ui/rp_widget.h"

#include <optional>
#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
class Session;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

struct HandoffDraft {
	QString text;
	QString untrusted;
	PeerKey from = 0;
};

class SessionView final : public RpWidget {
public:
	SessionView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);
	~SessionView();

	void showSession(Session *session);
	[[nodiscard]] Session *session() const;
	[[nodiscard]] rpl::producer<Session*> sessionChanges() const;

	void setHintProvider(Fn<QString()> hint);
	void setInnerFocus();
	[[nodiscard]] QString draft() const;
	void setDraft(const QString &text);
	void setContentMaxWidth(int width);
	void startHandoff(HandoffDraft draft);

private:
	enum class Mode {
		Normal,
		NoProvider,
	};

	void setupWidgets();
	void refreshProviderState();
	void applyMode(Mode mode);
	void relayout();
	void relayoutOverlay();
	void rebuildFeed();
	void applyState();
	void refreshSpending();
	void sendCurrentText(const QString &text);
	void handoffRequested(const QString &text);
	void refreshHandoffNote();
	void sessionGone();
	void resizeEvent(QResizeEvent *e) override;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	Session *_session = nullptr;
	Mode _mode = Mode::Normal;
	int _contentMaxWidth = 0;
	std::optional<HandoffDraft> _handoff;
	Fn<QString()> _hint;
	object_ptr<FeedWidget> _feed;
	object_ptr<Composer> _composer;
	object_ptr<RpWidget> _welcome;
	object_ptr<Ui::FlatLabel> _welcomeLabel;
	object_ptr<Ui::FlatLabel> _handoffNote;
	object_ptr<RpWidget> _noProvider;
	object_ptr<Ui::FlatLabel> _noProviderLabel;
	object_ptr<Ui::RoundButton> _openSettings;
	rpl::event_stream<Session*> _sessionChanges;
	rpl::lifetime _sessionLifetime;

};

} // namespace Agent::Ui
