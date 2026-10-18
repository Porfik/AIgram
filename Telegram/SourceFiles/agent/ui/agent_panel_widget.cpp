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
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_panel.h"

namespace Agent::Ui {

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

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupHeader();
	void updateControlsGeometry();
	void showHistory(bool shown);
	void showSession(Session *session);

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const not_null<Manager*> _manager;
	const Scope _scope;
	RpWidget *_header = nullptr;
	IconButton *_newSessionButton = nullptr;
	IconButton *_historyButton = nullptr;
	SessionView *_sessionView = nullptr;
	SessionsList *_sessionsList = nullptr;
	Fn<QString()> _hintProvider;
	bool _listOpen = false;

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
	setupHeader();

	const auto forumPeer = _peer->asChannel();
	if (forumPeer && forumPeer->isForum()) {
		const auto weak = base::make_weak(_controller.get());
		_hintProvider = [=] {
			if (const auto strong = weak.get()) {
				const auto thread = strong->activeChatCurrent().thread();
				if (const auto topic = thread ? thread->asTopic() : nullptr) {
					if (topic->peer() == _peer) {
						return (u"The user has topic \"%1\" (topic_id %2) open.\n"_q
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
	_sessionsList = Ui::CreateChild<SessionsList>(
		this,
		_manager,
		_scope);
	_sessionsList->openSessionRequests(
	) | rpl::on_next([=](const QString &id) {
		showSession(_manager->open(id));
	}, lifetime());

	if (_hintProvider) {
		_sessionView->setHintProvider(_hintProvider);
	}

	const auto list = _manager->list(_scope);
	showSession(list.empty() ? nullptr : _manager->open(list.front().id));
}

void PanelWidget::Inner::setupHeader() {
	_header = Ui::CreateChild<RpWidget>(this);
	_header->show();

	_newSessionButton = Ui::CreateChild<IconButton>(
		_header,
		st::agentPanelHeaderButton);
	_newSessionButton->setIconOverride(&st::agentPanelIconNewSession);
	_newSessionButton->setToolTip(tr::lng_agent_new_session(tr::now));
	_newSessionButton->setClickedCallback([=] {
		showSession(nullptr);
	});
	_historyButton = Ui::CreateChild<IconButton>(
		_header,
		st::agentPanelHeaderButton);
	_historyButton->setToolTip(tr::lng_agent_history(tr::now));
	_historyButton->setClickedCallback([=] {
		showHistory(!_listOpen);
	});

	_header->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_header);
		p.fillRect(_header->rect(), st::windowBg);
		p.setPen(st::windowFg);
		p.setFont(st::agentPanelHeaderTitleStyle.font);
		p.drawText(
			QRect(
				st::agentPanelHeaderPadding.left(),
				0,
				_header->width()
					- st::agentPanelHeaderPadding.left()
					- st::agentPanelHeaderPadding.right(),
				st::agentPanelHeaderHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			tr::lng_agent_title(tr::now));
	}, _header->lifetime());
}

void PanelWidget::Inner::showHistory(bool shown) {
	_listOpen = shown;
	_historyButton->setIconOverride(shown
		? &st::agentPanelIconSession
		: &st::agentPanelIconHistory);
	_sessionView->setVisible(!shown);
	_sessionsList->setVisible(shown);
	updateControlsGeometry();
}

void PanelWidget::Inner::showSession(Session *session) {
	_sessionView->showSession(session);
	showHistory(false);
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
		_listOpen,
	};
}

void PanelWidget::Inner::setInnerFocus() {
	if (_listOpen) {
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
	const auto width = this->width();
	const auto height = this->height();
	if (_header) {
		_header->setGeometry(0, 0, width, st::agentPanelHeaderHeight);
		const auto buttonSize = st::agentPanelHeaderButton.width;
		_historyButton->setGeometry(
			width
				- buttonSize
				- st::agentPanelHeaderPadding.right(),
			(st::agentPanelHeaderHeight - buttonSize) / 2,
			buttonSize,
			st::agentPanelHeaderButton.height);
		_newSessionButton->setGeometry(
			_historyButton->x() - buttonSize,
			(st::agentPanelHeaderHeight - buttonSize) / 2,
			buttonSize,
			st::agentPanelHeaderButton.height);
	}
	const auto top = st::agentPanelHeaderHeight;
	const auto contentHeight = height - top;
	if (_sessionView) {
		_sessionView->setGeometry(0, top, width, contentHeight);
	}
	if (_sessionsList) {
		_sessionsList->setGeometry(0, top, width, contentHeight);
	}
}

PanelWidget::PanelWidget(
	QWidget *parent,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer)
: Info::ContentWidget(parent, controller)
, _peer(peer) {
	_inner = setInnerWidget(object_ptr<Inner>(
		this,
		controller->parentController(),
		peer));

	_inner->resizeToWidth(width());
	scrollHeightValue(
	) | rpl::on_next([=](int height) {
		_inner->resize(width(), height);
	}, _inner->lifetime());
}

not_null<PeerData*> PanelWidget::peer() const {
	return _peer;
}

rpl::producer<QString> PanelWidget::title() {
	return tr::lng_agent_title();
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
