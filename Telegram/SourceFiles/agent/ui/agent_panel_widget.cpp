/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_panel_widget.h"

#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/ui/agent_session_view.h"
#include "agent/ui/agent_sessions_list.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "info/info_controller.h"
#include "info/info_top_bar.h"
#include "info/info_wrap_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_panel.h"

namespace Agent::Ui {
namespace {

class HeightFiller final : public RpWidget {
public:
	using RpWidget::RpWidget;

	rpl::producer<int> desiredHeightValue() const override;

};

rpl::producer<int> HeightFiller::desiredHeightValue() const {
	return rpl::single(st::agentPanelLayerHeight);
}

} // namespace

PanelMemento::PanelMemento(not_null<PeerData*> peer)
: Info::ContentMemento(peer, nullptr, nullptr, PeerId()) {
}

object_ptr<Info::ContentWidget> PanelMemento::createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<PanelWidget>(parent, controller, peer());
	result->setInternalState(geometry, this);
	return result;
}

Info::Section PanelMemento::section() const {
	return Info::Section(Info::Section::Type::Agent);
}

void PanelMemento::setSessionId(const QString &id) {
	_sessionId = id;
}

const QString &PanelMemento::sessionId() const {
	return _sessionId;
}

void PanelMemento::setListOpen(bool open) {
	_listOpen = open;
}

bool PanelMemento::listOpen() const {
	return _listOpen;
}

class PanelWidget::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

	void restoreState(const QString &sessionId, bool listOpen);
	[[nodiscard]] std::pair<QString, bool> state() const;
	void setInnerFocus();
	void startNewSession();
	void toggleHistory();
	[[nodiscard]] rpl::producer<QString> titleValue() const;
	[[nodiscard]] rpl::producer<bool> listOpenValue() const;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateControlsGeometry();
	void showHistory(bool shown);
	void showSession(Session *session);
	void trackSession(Session *session);
	void updateTitle();

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const not_null<Manager*> _manager;
	const Scope _scope;
	SessionView *_sessionView = nullptr;
	ScrollArea *_scroll = nullptr;
	SessionsList *_sessionsList = nullptr;
	Fn<QString()> _hintProvider;
	rpl::variable<bool> _listOpen = false;
	rpl::variable<QString> _title;
	rpl::lifetime _sessionLifetime;

};

PanelWidget::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _manager(&peer->session().agent())
, _scope(Scope::Chat(peer->id.value)) {
	const auto forumPeer = _peer->asChannel();
	if (forumPeer && forumPeer->isForum()) {
		const auto weak = base::make_weak(_controller.get());
		_hintProvider = [=] {
			if (const auto strong = weak.get()) {
				const auto thread = strong->activeChatCurrent().thread();
				if (const auto topic = thread ? thread->asTopic() : nullptr) {
					if (topic->peer() == _peer) {
						return (u"The user has topic \"%1\" "
							"(topic_id %2) open."_q
							.arg(topic->title())
							.arg(topic->rootId().bare));
					}
				}
			}
			return QString();
		};
	}

	_sessionView = Ui::CreateChild<SessionView>(
		this,
		_controller,
		_manager,
		_scope);
	_scroll = Ui::CreateChild<ScrollArea>(this);
	_sessionsList = _scroll->setOwnedWidget(
		object_ptr<SessionsList>(
			_scroll,
			_controller,
			_manager,
			_scope));
	_scroll->widthValue(
	) | rpl::on_next([=](int width) {
		_sessionsList->resizeToWidth(width);
	}, lifetime());
	_sessionsList->openSessionRequests(
	) | rpl::on_next([=](const QString &id) {
		showSession(_manager->open(id));
	}, lifetime());
	_sessionView->sessionChanges(
	) | rpl::on_next([=](Session *session) {
		trackSession(session);
	}, lifetime());

	if (_hintProvider) {
		_sessionView->setHintProvider(_hintProvider);
	}

	const auto list = _manager->list(_scope);
	showSession(list.empty() ? nullptr : _manager->open(list.front().id));
}

void PanelWidget::Inner::showHistory(bool shown) {
	_listOpen = shown;
	_sessionView->setVisible(!shown);
	_scroll->setVisible(shown);
	updateTitle();
	updateControlsGeometry();
}

void PanelWidget::Inner::startNewSession() {
	showSession(nullptr);
	_sessionView->setInnerFocus();
}

void PanelWidget::Inner::toggleHistory() {
	showHistory(!_listOpen.current());
	if (!_listOpen.current()) {
		_sessionView->setInnerFocus();
	}
}

rpl::producer<QString> PanelWidget::Inner::titleValue() const {
	return _title.value();
}

rpl::producer<bool> PanelWidget::Inner::listOpenValue() const {
	return _listOpen.value();
}

void PanelWidget::Inner::showSession(Session *session) {
	_sessionView->showSession(session);
	trackSession(session);
	showHistory(false);
}

void PanelWidget::Inner::trackSession(Session *session) {
	_sessionLifetime = rpl::lifetime();
	if (session) {
		session->recordChanges(
		) | rpl::on_next([=] {
			updateTitle();
		}, _sessionLifetime);
	}
	updateTitle();
}

void PanelWidget::Inner::updateTitle() {
	if (_listOpen.current()) {
		_title = tr::lng_agent_sessions_tab(tr::now);
	} else if (const auto session = _sessionView->session()) {
		const auto title = session->record().info.title;
		_title = title.isEmpty()
			? tr::lng_agent_session_untitled(tr::now)
			: title;
	} else {
		_title = tr::lng_agent_title(tr::now);
	}
}

void PanelWidget::Inner::restoreState(
		const QString &sessionId,
		bool listOpen) {
	if (!sessionId.isEmpty()) {
		if (const auto session = _manager->open(sessionId)) {
			showSession(session);
		}
	}
	showHistory(listOpen);
}

std::pair<QString, bool> PanelWidget::Inner::state() const {
	return {
		_sessionView->session() ? _sessionView->session()->id() : QString(),
		_listOpen.current(),
	};
}

void PanelWidget::Inner::setInnerFocus() {
	if (_listOpen.current()) {
		_sessionsList->setFocus();
	} else {
		_sessionView->setInnerFocus();
	}
}

void PanelWidget::Inner::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	updateControlsGeometry();
}

void PanelWidget::Inner::updateControlsGeometry() {
	const auto geometry = rect();
	_scroll->setGeometry(geometry);
	_sessionView->setGeometry(geometry);
}

PanelWidget::PanelWidget(
	QWidget *parent,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer)
: Info::ContentWidget(parent, controller)
, _peer(peer) {
	setInnerWidget(object_ptr<HeightFiller>(this));
	_inner = CreateChild<Inner>(
		this,
		controller->parentController(),
		peer);
	scroll()->geometryValue(
	) | rpl::on_next([=](QRect geometry) {
		_inner->setGeometry(geometry);
	}, _inner->lifetime());
}

not_null<PeerData*> PanelWidget::peer() const {
	return _peer;
}

rpl::producer<QString> PanelWidget::title() {
	return _inner->titleValue() | rpl::after_next([=] {
		if (const auto topBar = _topBar.data()) {
			topBar->resizeToWidth(topBar->width());
		}
	});
}

void PanelWidget::setInnerFocus() {
	_inner->setInnerFocus();
}

void PanelWidget::setupTopBar(
		not_null<Info::TopBar*> topBar,
		Info::Wrap wrap) {
	_topBar = topBar.get();
	const auto layer = (wrap == Info::Wrap::Layer);
	const auto history = topBar->addButton(
		base::make_unique_q<IconButton>(
			topBar,
			(layer
				? st::agentPanelLayerTopBarHistory
				: st::agentPanelTopBarHistory)));
	history->setAccessibleName(tr::lng_agent_history(tr::now));
	history->setToolTip(tr::lng_agent_history(tr::now));
	history->setClickedCallback([=] {
		_inner->toggleHistory();
	});
	_inner->listOpenValue(
	) | rpl::on_next([=](bool open) {
		history->setIconOverride(
			open ? &st::agentPanelTopBarHistoryActive : nullptr,
			open ? &st::agentPanelTopBarHistoryActive : nullptr);
	}, history->lifetime());
	const auto add = topBar->addButton(
		base::make_unique_q<IconButton>(
			topBar,
			(layer
				? st::agentPanelLayerTopBarNew
				: st::agentPanelTopBarNew)));
	add->setAccessibleName(tr::lng_agent_new_session(tr::now));
	add->setToolTip(tr::lng_agent_new_session(tr::now));
	add->setClickedCallback([=] {
		_inner->startNewSession();
	});
}

bool PanelWidget::showInternal(
		not_null<Info::ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (dynamic_cast<PanelMemento*>(memento.get())) {
		restoreState(memento);
		return true;
	}
	return false;
}

void PanelWidget::setInternalState(
		const QRect &geometry,
		not_null<Info::ContentMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

void PanelWidget::saveState(not_null<Info::ContentMemento*> memento) {
	if (auto my = dynamic_cast<PanelMemento*>(memento.get())) {
		const auto state = _inner->state();
		my->setSessionId(state.first);
		my->setListOpen(state.second);
	}
}

void PanelWidget::restoreState(not_null<Info::ContentMemento*> memento) {
	if (auto my = dynamic_cast<PanelMemento*>(memento.get())) {
		_inner->restoreState(my->sessionId(), my->listOpen());
	}
}

std::shared_ptr<Info::ContentMemento> PanelWidget::doCreateMemento() {
	auto result = std::make_shared<PanelMemento>(_peer);
	saveState(result.get());
	return result;
}

} // namespace Agent::Ui
