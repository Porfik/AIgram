/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_session_view.h"

namespace Agent::Ui {

SessionView::SessionView(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope) {
}

SessionView::~SessionView() = default;

void SessionView::showSession(Session *session) {
	_session = session;
}

Session *SessionView::session() const {
	return _session;
}

rpl::producer<Session*> SessionView::sessionChanges() const {
	return _sessionChanges.events();
}

void SessionView::setHintProvider(Fn<QString()> hint) {
	_hint = std::move(hint);
}

void SessionView::setInnerFocus() {
	setFocus();
}

} // namespace Agent::Ui
