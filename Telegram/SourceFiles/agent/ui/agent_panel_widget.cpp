/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_panel_widget.h"

#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/ui/agent_chat_view.h"
#include "agent/ui/agent_history_view.h"
#include "agent/ui/agent_panel.h"
#include "agent/ui/agent_sessions_list.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "info/info_wrap_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_panel.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Agent::Ui {
namespace {

// WHY: mirrors Info::LayerWidget::countGeometry(), so a short window
// shows the whole layer with its rounded bottom instead of cutting it.
[[nodiscard]] int LayerContentHeight(int windowHeight) {
	const auto margin = std::clamp(
		windowHeight / 24,
		st::infoLayerTopMinimal,
		st::infoLayerTopMaximal);
	return std::min(
		st::agentPanelLayerHeight,
		(windowHeight
			- 2 * margin
			- st::boxRadius
			- st::infoLayerTopBarHeight));
}

class HeightFiller final : public RpWidget {
public:
	HeightFiller(QWidget *parent, not_null<RpWidget*> window);

	rpl::producer<int> desiredHeightValue() const override;

private:
	const not_null<RpWidget*> _window;

};

HeightFiller::HeightFiller(QWidget *parent, not_null<RpWidget*> window)
: RpWidget(parent)
, _window(window) {
}

rpl::producer<int> HeightFiller::desiredHeightValue() const {
	return _window->heightValue() | rpl::map(LayerContentHeight);
}

void MoveToThirdColumn(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Info::ContentMemento> memento) {
	EnableThirdColumn(window);
	window->hideSpecialLayer(anim::type::instant);
	window->showSection(
		std::make_shared<Info::Memento>(
			std::vector<std::shared_ptr<Info::ContentMemento>>{
				std::move(memento),
			}),
		Window::SectionShow(
			Window::SectionShow::Way::ClearStack,
			anim::type::instant,
			anim::activation::background).withThirdColumn());
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

void PanelMemento::setDraft(const QString &text) {
	_draft = text;
}

const QString &PanelMemento::draft() const {
	return _draft;
}

void PanelMemento::setHistoryTab(HistoryTab tab) {
	_historyTab = tab;
}

HistoryTab PanelMemento::historyTab() const {
	return _historyTab;
}

class PanelWidget::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

	void saveState(not_null<PanelMemento*> memento) const;
	void restoreState(not_null<PanelMemento*> memento);
	void setInnerFocus();
	void startNewSession();
	void toggleHistory();
	void showSessionMenu(not_null<QWidget*> button);
	[[nodiscard]] rpl::producer<QString> titleValue() const;
	[[nodiscard]] rpl::producer<bool> listOpenValue() const;
	[[nodiscard]] rpl::producer<bool> hasSessionValue() const;

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
	ChatView *_sessionView = nullptr;
	HistoryView *_history = nullptr;
	Fn<QString()> _hintProvider;
	rpl::variable<bool> _listOpen = false;
	rpl::variable<bool> _hasSession = false;
	rpl::variable<QString> _title;
	base::unique_qptr<PopupMenu> _menu;
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

	_sessionView = Ui::CreateChild<ChatView>(
		this,
		_controller,
		_manager,
		_scope);
	_history = Ui::CreateChild<HistoryView>(
		this,
		_controller,
		_manager,
		_scope);
	_history->openSessionRequests(
	) | rpl::on_next([=](const QString &id) {
		showSession(_manager->open(id));
	}, lifetime());
	_history->openRunRequests(
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
	// WHY: hiding the focused view first lets Qt pass the focus to the
	// next field of the window, the chats search, which then opens.
	const auto focused = Ui::InFocusChain(this);
	_listOpen = shown;
	if (shown) {
		_history->show();
		if (focused) {
			_history->setInnerFocus();
		}
		_sessionView->hide();
	} else {
		_sessionView->show();
		if (focused) {
			_sessionView->setInnerFocus();
		}
		_history->hide();
	}
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

rpl::producer<bool> PanelWidget::Inner::hasSessionValue() const {
	return _hasSession.value();
}

void PanelWidget::Inner::showSessionMenu(not_null<QWidget*> button) {
	if (const auto session = _sessionView->session()) {
		ShowSessionMenu(_controller, _manager, button, session->id(), _menu);
	}
}

void PanelWidget::Inner::showSession(Session *session) {
	_sessionView->showSession(session);
	trackSession(session);
	showHistory(false);
}

void PanelWidget::Inner::trackSession(Session *session) {
	_sessionLifetime = rpl::lifetime();
	_hasSession = (session != nullptr);
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
		_title = tr::lng_agent_history(tr::now);
	} else if (const auto session = _sessionView->session()) {
		const auto title = session->record().info.title;
		_title = title.isEmpty()
			? tr::lng_agent_session_untitled(tr::now)
			: title;
	} else {
		_title = tr::lng_agent_title(tr::now);
	}
}

void PanelWidget::Inner::saveState(not_null<PanelMemento*> memento) const {
	const auto session = _sessionView->session();
	memento->setSessionId(session ? session->id() : QString());
	memento->setListOpen(_listOpen.current());
	memento->setHistoryTab(_history->tab());
}

void PanelWidget::Inner::restoreState(not_null<PanelMemento*> memento) {
	if (!memento->sessionId().isEmpty()) {
		if (const auto session = _manager->open(memento->sessionId())) {
			showSession(session);
		}
	}
	_history->showTab(memento->historyTab());
	showHistory(memento->listOpen());
}

void PanelWidget::Inner::setInnerFocus() {
	if (_listOpen.current()) {
		_history->setInnerFocus();
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
	_history->setGeometry(geometry);
	_sessionView->setGeometry(geometry);
}

PanelWidget::PanelWidget(
	QWidget *parent,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer)
: Info::ContentWidget(parent, controller)
, _peer(peer) {
	const auto window = controller->parentController();
	setInnerWidget(object_ptr<HeightFiller>(
		this,
		window->widget()->bodyWidget()));
	_inner = CreateChild<Inner>(this, window, peer);
	scroll()->geometryValue(
	) | rpl::on_next([=](QRect geometry) {
		_inner->setGeometry(geometry);
	}, _inner->lifetime());
	setupThirdColumnFollow();
	setupCloseOnExclusion();
}

void PanelWidget::setupCloseOnExclusion() {
	const auto window = controller()->parentController();
	PanelAvailabilityChanges(
		&_peer->session()
	) | rpl::filter([=] {
		return !PanelAvailable(_peer);
	}) | rpl::take(1) | rpl::on_next([=] {
		const auto peer = _peer;
		const auto layer = (controller()->wrap() == Info::Wrap::Layer);
		// Both calls destroy this widget, so leave the event first.
		crl::on_main(this, [=] {
			if (layer) {
				window->hideSpecialLayer();
			} else {
				window->showSection(
					std::make_shared<Info::Memento>(peer),
					Window::SectionShow().withThirdColumn());
			}
		});
	}, lifetime());
}

void PanelWidget::setupThirdColumnFollow() {
	const auto window = controller()->parentController();
	rpl::combine(
		controller()->wrapValue(),
		window->widget()->bodyWidget()->widthValue()
	) | rpl::map([=](Info::Wrap wrap, int) {
		return (wrap == Info::Wrap::Layer)
			&& window->canShowThirdSectionWithoutResize();
	}) | rpl::distinct_until_changed(
	) | rpl::skip(1) | rpl::filter(
		rpl::mappers::_1
	) | rpl::on_next([=] {
		// Hiding the layer destroys this widget, so leave the resize first.
		crl::on_main(this, [=] {
			MoveToThirdColumn(window, doCreateMemento());
		});
	}, lifetime());
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
	const auto menu = topBar->addButton(
		base::make_unique_q<IconButton>(
			topBar,
			(layer ? st::infoLayerTopBarMenu : st::infoTopBarMenu)));
	menu->setAccessibleName(tr::lng_agent_session_more(tr::now));
	menu->setClickedCallback([=] {
		_inner->showSessionMenu(menu);
	});
	_inner->hasSessionValue(
	) | rpl::on_next([=](bool has) {
		menu->setDisabled(!has);
		menu->setIconOverride(
			has ? nullptr : &st::agentPanelTopBarMenuDisabled,
			has ? nullptr : &st::agentPanelTopBarMenuDisabled);
	}, menu->lifetime());
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
	if (const auto my = dynamic_cast<PanelMemento*>(memento.get())) {
		_inner->saveState(my);
	}
}

void PanelWidget::restoreState(not_null<Info::ContentMemento*> memento) {
	if (const auto my = dynamic_cast<PanelMemento*>(memento.get())) {
		_inner->restoreState(my);
	}
}

std::shared_ptr<Info::ContentMemento> PanelWidget::doCreateMemento() {
	auto result = std::make_shared<PanelMemento>(_peer);
	saveState(result.get());
	return result;
}

} // namespace Agent::Ui
