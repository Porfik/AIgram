/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "ui/rp_widget.h"

#include <rpl/event_stream.h>

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
class Session;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

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

private:
	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	Session *_session = nullptr;
	Fn<QString()> _hint;
	rpl::event_stream<Session*> _sessionChanges;

};

} // namespace Agent::Ui
