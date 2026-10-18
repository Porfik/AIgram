/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_session_view.h"

#include "agent/core/agent_feed.h"
#include "agent/ui/agent_composer.h"
#include "agent/ui/agent_feed_widget.h"
#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/agent_settings.h"
#include "iv/markdown/iv_markdown_math_renderer.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_agent.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"

#include "styles/style_agent.h"

namespace Agent::Ui {

SessionView::SessionView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope)
, _feed(this, FeedContext{
	.controller = controller,
	.renderer = std::make_shared<Iv::Markdown::MathRenderer>(),
})
, _composer(this)
, _welcome(this)
, _welcomeLabel(
	_welcome,
	(_scope.kind == ScopeKind::Chat
		? tr::lng_agent_chat_welcome(tr::now)
		: tr::lng_agent_title(tr::now)),
	st::agentWelcomeLabel)
, _noProvider(this)
, _noProviderLabel(
	_noProvider,
	tr::lng_agent_no_provider(tr::now),
	st::agentFeedStatusLabel)
, _openSettings(
	_noProvider,
	tr::lng_agent_open_settings(),
	st::agentOverlayOpenSettings) {
	_composer->sends(
	) | rpl::on_next([=](const QString &text) {
		sendCurrentText(text);
	}, lifetime());

	_composer->stops(
	) | rpl::on_next([=] {
		if (_session) {
			_session->stop();
		}
	}, lifetime());

	_composer->heightChanges(
	) | rpl::on_next([=] {
		relayout();
	}, lifetime());

	_feed->retryRequests(
	) | rpl::on_next([=] {
		if (_session) {
			_session->retry();
		}
	}, lifetime());

	_manager->enabledValue(
	) | rpl::on_next([=](bool) {
		refreshProviderState();
	}, lifetime());

	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		refreshProviderState();
	}, lifetime());

	_openSettings->setClickedCallback([=] {
		_controller->showSettings(Settings::AgentId());
	});

	refreshProviderState();
}

SessionView::~SessionView() {
	_sessionLifetime.destroy();
}

void SessionView::showSession(Session *session) {
	if (_session == session) {
		return;
	}
	_sessionLifetime.destroy();
	_session = session;
	if (session) {
		std::move(session->recordChanges()
		) | rpl::on_next_error_done(
			[=] {
				rebuildFeed();
			},
			[](rpl::no_error) {
			},
			[=] {
				sessionGone();
			},
			_sessionLifetime);
		std::move(session->stateChanges()
		) | rpl::on_next([=] {
			applyState();
		}, _sessionLifetime);
		std::move(session->live()
		) | rpl::on_next([=](const EngineEvent &event) {
			_feed->handleLiveEvent(event);
		}, _sessionLifetime);
		rebuildFeed();
		_feed->scrollToBottom();
		relayout();
	} else {
		_feed->rebuild({});
		_feed->setStatus(SessionState{});
		_composer->setWorking(false);
		_composer->setQueued({});
		relayout();
	}
	_sessionChanges.fire_copy(_session);
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
	if (_mode == Mode::Normal) {
		_composer->focusField();
	} else {
		setFocus();
	}
}

void SessionView::refreshProviderState() {
	_mode = _manager->available()
		? Mode::Normal
		: Mode::NoProvider;
	relayout();
}

void SessionView::rebuildFeed() {
	const auto session = _session;
	if (!session) {
		return;
	}
	_feed->rebuild(BuildFeed(
		session->record().messages,
		session->record().summaryUntil));
	applyState();
}

void SessionView::applyState() {
	const auto session = _session;
	if (!session) {
		return;
	}
	const auto &state = session->state();
	_composer->setWorking(
		state.status == SessionStatus::Working
		|| state.status == SessionStatus::Compacting);
	_composer->setQueued(state.queued);
	_feed->setStatus(state);
}

void SessionView::sendCurrentText(const QString &text) {
	auto session = _session;
	if (!session) {
		session = _manager->create(_scope);
		showSession(session);
	}
	session->send({
		.text = text,
		.mentions = {},
		.hint = _hint ? _hint() : QString(),
	});
}

void SessionView::sessionGone() {
	showSession(nullptr);
}

void SessionView::relayout() {
	const auto w = width();
	const auto h = height();
	if (_mode == Mode::NoProvider) {
		_feed->hide();
		_composer->hide();
		_welcome->hide();
		_noProvider->setGeometry(rect());
		_noProvider->show();
		relayoutOverlay();
		return;
	}
	_noProvider->hide();
	_composer->show();
	_composer->resizeToWidth(w);
	const auto composerHeight = _composer->height();
	_composer->setGeometry(0, h - composerHeight, w, composerHeight);
	_feed->setGeometry(0, 0, w, h - composerHeight);
	_feed->show();
	relayoutOverlay();
}

void SessionView::relayoutOverlay() {
	const auto inset = st::agentOverlayMargin.left()
		+ st::agentOverlayMargin.right();
	if (_mode == Mode::NoProvider) {
		const auto w = _noProvider->width();
		const auto avail = std::max(w - inset, 0);
		_noProviderLabel->resizeToNaturalWidth(avail);
		const auto total = _noProviderLabel->height()
			+ st::agentOverlayGap
			+ _openSettings->height();
		const auto top = std::max((_noProvider->height() - total) / 2, 0);
		_noProviderLabel->move((w - _noProviderLabel->width()) / 2, top);
		_openSettings->move(
			(w - _openSettings->width()) / 2,
			top + _noProviderLabel->height() + st::agentOverlayGap);
		return;
	}
	if (_session) {
		_welcome->hide();
		return;
	}
	_welcome->setGeometry(_feed->geometry());
	_welcome->show();
	const auto w = _welcome->width();
	const auto avail = std::max(w - inset, 0);
	_welcomeLabel->resizeToNaturalWidth(avail);
	_welcomeLabel->move(
		(w - _welcomeLabel->width()) / 2,
		std::max((_welcome->height() - _welcomeLabel->height()) / 2, 0));
}

void SessionView::resizeEvent(QResizeEvent *e) {
	relayout();
}

} // namespace Agent::Ui