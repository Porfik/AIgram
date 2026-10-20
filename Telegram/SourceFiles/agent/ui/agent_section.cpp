/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_section.h"

#include "agent/agent_chat.h"
#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/agent_settings.h"
#include "agent/ui/agent_menus.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_chat_view.h"
#include "agent/ui/agent_session_rows.h"
#include "agent/ui/agent_sessions_list.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "settings/settings_common.h"
#include "ui/abstract_button.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_general.h"
#include "styles/style_agent_panel.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Agent::Ui {
namespace {

enum class Mode {
	List,
	Session,
};

class GeneralAgentMemento final : public Window::SectionMemento {
public:
	GeneralAgentMemento() = default;

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	Mode mode = Mode::List;
	QString sessionId;
	std::optional<HandoffDraft> handoff;

};

class HeaderTitle final : public ::Ui::AbstractButton {
public:
	explicit HeaderTitle(QWidget *parent);

	void setTexts(const QString &name, const QString &status, bool active);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	::Ui::Text::String _name;
	QString _status;
	bool _statusActive = false;

};

class GeneralAgentSection final : public Window::SectionWidget {
public:
	GeneralAgentSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	Dialogs::RowDescriptor activeChat() const override;
	bool hasTopBarShadow() const override;
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	void setInternalState(
		const QRect &geometry,
		not_null<GeneralAgentMemento*> memento);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;
	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;

private:
	void setupTopBar();
	void applyModeVisibility();
	void setupList();
	void applyMemento(not_null<GeneralAgentMemento*> memento);
	void showList();
	void showSession(Session *session);
	void trackSession(Session *session);
	void showMenu();
	void updateHeader();
	void updateControlsGeometry();

	const not_null<Manager*> _manager;
	const not_null<History*> _history;
	Mode _mode = Mode::List;
	::Ui::RpWidget *_topBar = nullptr;
	::Ui::IconButton *_back = nullptr;
	::Ui::UserpicButton *_userpic = nullptr;
	HeaderTitle *_title = nullptr;
	::Ui::IconButton *_menuToggle = nullptr;
	::Ui::PlainShadow *_shadow = nullptr;
	::Ui::ScrollArea *_listScroll = nullptr;
	::Ui::RpWidget *_listContent = nullptr;
	::Ui::VerticalLayout *_list = nullptr;
	SessionRows *_rows = nullptr;
	::Ui::FlatLabel *_listAbout = nullptr;
	ChatView *_sessionView = nullptr;
	base::unique_qptr<::Ui::PopupMenu> _menu;
	rpl::lifetime _sessionLifetime;

};

HeaderTitle::HeaderTitle(QWidget *parent)
: AbstractButton(parent) {
}

void HeaderTitle::setTexts(
		const QString &name,
		const QString &status,
		bool active) {
	if (_name.toString() == name
		&& _status == status
		&& _statusActive == active) {
		return;
	}
	_name.setText(st::msgNameStyle, name);
	_status = status;
	_statusActive = active;
	update();
}

void HeaderTitle::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto available = std::max(
		width() - st::topBarNameRightPadding,
		0);
	p.setPen(st::dialogsNameFg);
	_name.draw(p, {
		.position = { 0, st::topBarArrowPadding.top() },
		.availableWidth = available,
		.elisionLines = 1,
	});
	p.setFont(st::dialogsTextFont);
	p.setPen(_statusActive ? st::historyStatusFgTyping : st::historyStatusFg);
	p.drawTextLeft(
		0,
		(st::topBarHeight
			- st::topBarArrowPadding.bottom()
			- st::dialogsTextFont->height),
		width(),
		st::dialogsTextFont->elided(_status, available));
}

} // namespace

object_ptr<Window::SectionWidget> GeneralAgentMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<GeneralAgentSection>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

GeneralAgentSection::GeneralAgentSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _manager(&controller->session().agent())
, _history(_manager->chat().history()) {
	setupTopBar();
	_shadow = ::Ui::CreateChild<::Ui::PlainShadow>(this);
	_shadow->show();
	setupList();
	_sessionView = ::Ui::CreateChild<ChatView>(
		this,
		controller,
		_manager,
		Scope::Account());
	Window::ChatThemeValueFromPeer(
		controller,
		_manager->chat().user()
	) | rpl::on_next([=](std::shared_ptr<::Ui::ChatTheme> &&theme) {
		controller->setChatStyleTheme(theme);
	}, lifetime());
	_sessionView->sessionChanges(
	) | rpl::on_next([=](Session *session) {
		trackSession(session);
	}, lifetime());
	_manager->activityValue(
		Scope::Account()
	) | rpl::on_next([=] {
		updateHeader();
	}, lifetime());
	rpl::merge(
		_manager->chat().nameValue() | rpl::to_empty,
		Agent::ProvidersChanged()
	) | rpl::on_next([=] {
		updateHeader();
	}, lifetime());
	controller->adaptive().value(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());
	showList();
}

void GeneralAgentSection::setupTopBar() {
	_topBar = ::Ui::CreateChild<::Ui::RpWidget>(this);
	_topBar->show();
	_topBar->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_topBar);
		p.fillRect(clip, st::topBarBg);
	}, _topBar->lifetime());

	_back = ::Ui::CreateChild<::Ui::IconButton>(
		_topBar,
		st::historyTopBarBack);
	_back->setAccessibleName(tr::lng_go_back(tr::now));
	_back->setClickedCallback([=] {
		if (_mode == Mode::Session) {
			showList();
		} else {
			controller()->showBackFromStack();
		}
	});

	const auto showProfile = [=] {
		controller()->showPeerInfo(_manager->chat().user());
	};
	_userpic = ::Ui::CreateChild<::Ui::UserpicButton>(
		_topBar,
		_manager->chat().user(),
		st::topBarInfoButton);
	_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	_userpic->show();
	_title = ::Ui::CreateChild<HeaderTitle>(_topBar);
	_title->setClickedCallback(showProfile);
	_title->show();

	_menuToggle = ::Ui::CreateChild<::Ui::IconButton>(
		_topBar,
		st::topBarMenuToggle);
	_menuToggle->setAccessibleName(tr::lng_agent_session_more(tr::now));
	_menuToggle->setClickedCallback([=] {
		showMenu();
	});
	_menuToggle->show();
}

void GeneralAgentSection::setupList() {
	_listScroll = ::Ui::CreateChild<::Ui::ScrollArea>(
		this,
		st::defaultSolidScroll);
	const auto content = _listScroll->setOwnedWidget(
		object_ptr<::Ui::RpWidget>(_listScroll));
	_listContent = content.data();
	_list = ::Ui::CreateChild<::Ui::VerticalLayout>(content.data());
	content->widthValue(
	) | rpl::on_next([=](int width) {
		const auto inner = std::min(width, st::agentGeneralContentMaxWidth);
		_list->resizeToWidth(inner);
		_list->moveToLeft((width - inner) / 2, 0);
	}, content->lifetime());
	_list->heightValue(
	) | rpl::on_next([=](int height) {
		content->resize(content->width(), height);
	}, content->lifetime());
	content->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(content.data());
		p.fillRect(clip, st::dialogsBg);
	}, content->lifetime());

	const auto add = Settings::AddButtonWithIcon(
		_list,
		tr::lng_agent_new_session(),
		st::settingsButtonActive,
		{ &st::settingsIconAdd, Settings::IconType::Round, &st::windowBgActive });
	add->setClickedCallback([=] {
		showSession(nullptr);
		_sessionView->setInnerFocus();
	});
	_rows = _list->add(object_ptr<SessionRows>(
		_list,
		controller(),
		_manager,
		Scope::Account()));
	_rows->openRequests(
	) | rpl::on_next([=](const QString &id) {
		showSession(_manager->open(id));
	}, _rows->lifetime());
	_listAbout = _list->add(
		object_ptr<::Ui::FlatLabel>(
			_list,
			tr::lng_agent_sessions_empty(),
			st::agentSessionRowsAbout),
		st::agentSessionRowsAboutPadding);
	_rows->heightValue(
	) | rpl::on_next([=] {
		_listAbout->setVisible(_rows->empty());
	}, _listAbout->lifetime());
	_listScroll->show();
}

Dialogs::RowDescriptor GeneralAgentSection::activeChat() const {
	return { _history, FullMsgId() };
}

void GeneralAgentSection::applyModeVisibility() {
	const auto list = (_mode == Mode::List);
	_listScroll->setVisible(list);
	_sessionView->setVisible(!list);
}

void GeneralAgentSection::showFinishedHook() {
	applyModeVisibility();
	updateControlsGeometry();
	_sessionView->showFinished();
}

void GeneralAgentSection::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_sessionView->showStarted();
}

void GeneralAgentSection::showList() {
	const auto focused = ::Ui::InFocusChain(this);
	_mode = Mode::List;
	_listScroll->show();
	if (focused) {
		_listScroll->setFocus();
	}
	_sessionView->hide();
	_sessionLifetime.destroy();
	updateHeader();
	updateControlsGeometry();
}

void GeneralAgentSection::showSession(Session *session) {
	_mode = Mode::Session;
	_sessionView->showSession(session);
	_sessionView->show();
	_listScroll->hide();
	trackSession(session);
	updateControlsGeometry();
}

void GeneralAgentSection::trackSession(Session *session) {
	_sessionLifetime.destroy();
	if (session) {
		rpl::merge(
			session->recordChanges(),
			session->stateChanges()
		) | rpl::on_next([=] {
			updateHeader();
		}, _sessionLifetime);
	}
	updateHeader();
}

void GeneralAgentSection::showMenu() {
	const auto session = (_mode == Mode::Session)
		? _sessionView->session()
		: nullptr;
	if (session) {
		ShowSessionMenu(
			controller(),
			_manager,
			_menuToggle,
			session->id(),
			_menu);
		return;
	}
	_menu = base::make_unique_q<::Ui::PopupMenu>(
		_menuToggle,
		st::popupMenuWithIcons);
	const auto addAction = ::Ui::Menu::CreateAddActionCallback(_menu);
	const auto filled = FillAgentChatMenu(
		controller(),
		Dialogs::EntryState{ .key = _history },
		addAction);
	if (!filled || _menu->empty()) {
		_menu = nullptr;
		return;
	}
	_menu->setForcedOrigin(::Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(_menuToggle->mapToGlobal(
		QPoint(_menuToggle->width(), _menuToggle->height())));
}

void GeneralAgentSection::updateHeader() {
	const auto agent = _manager->chat().name();
	if (_mode == Mode::List) {
		const auto activity = _manager->activity(Scope::Account());
		const auto provider = DefaultProvider(_manager->providers());
		const auto status = activity.pendingCards
			? tr::lng_agent_waiting_card(tr::now)
			: activity.working
			? (tr::lng_typing(tr::now) + QString::fromUtf8("\xe2\x80\xa6"))
			: provider
			? ModelTitle(*provider)
			: QString();
		_title->setTexts(
			agent,
			status,
			activity.pendingCards || activity.working);
		return;
	}
	const auto session = _sessionView->session();
	const auto title = !session
		? tr::lng_agent_new_session(tr::now)
		: session->record().info.title.isEmpty()
		? tr::lng_agent_session_untitled(tr::now)
		: session->record().info.title;
	const auto &state = session ? session->state() : SessionState();
	const auto working = (state.status == SessionStatus::Working)
		|| (state.status == SessionStatus::Compacting);
	const auto status = state.pendingCards
		? tr::lng_agent_waiting_card(tr::now)
		: working
		? (tr::lng_typing(tr::now) + QString::fromUtf8("\xe2\x80\xa6"))
		: agent;
	_title->setTexts(title, status, state.pendingCards || working);
}

bool GeneralAgentSection::hasTopBarShadow() const {
	return true;
}

QRect GeneralAgentSection::floatPlayerAvailableRect() {
	return mapToGlobal((_mode == Mode::List)
		? _listScroll->geometry()
		: _sessionView->geometry());
}

bool GeneralAgentSection::floatPlayerHandleWheelEvent(QEvent *e) {
	return false;
}

void GeneralAgentSection::applyMemento(
		not_null<GeneralAgentMemento*> memento) {
	if (memento->handoff) {
		showSession(nullptr);
		_sessionView->startHandoff(*memento->handoff);
		return;
	}
	if (memento->mode == Mode::Session) {
		const auto session = memento->sessionId.isEmpty()
			? nullptr
			: _manager->open(memento->sessionId);
		showSession(session);
	} else {
		showList();
	}
}

bool GeneralAgentSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto my = dynamic_cast<GeneralAgentMemento*>(memento.get());
	if (!my) {
		return false;
	}
	applyMemento(my);
	return true;
}

void GeneralAgentSection::setInternalState(
		const QRect &geometry,
		not_null<GeneralAgentMemento*> memento) {
	setGeometry(geometry);
	::Ui::SendPendingMoveResizeEvents(this);
	applyMemento(memento);
}

std::shared_ptr<Window::SectionMemento> GeneralAgentSection::createMemento() {
	auto result = std::make_shared<GeneralAgentMemento>();
	result->mode = _mode;
	if (_mode == Mode::Session) {
		const auto session = _sessionView->session();
		result->sessionId = session ? session->id() : QString();
	}
	return result;
}

void GeneralAgentSection::resizeEvent(QResizeEvent *e) {
	SectionWidget::resizeEvent(e);
	updateControlsGeometry();
}

void GeneralAgentSection::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	}
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::windowBg);
}

void GeneralAgentSection::doSetInnerFocus() {
	if (_mode == Mode::Session) {
		_sessionView->setInnerFocus();
	} else {
		_listScroll->setFocus();
	}
}

void GeneralAgentSection::updateControlsGeometry() {
	if (!_topBar) {
		return;
	}
	const auto oneColumn = controller()->adaptive().isOneColumn();
	const auto backShown = (_mode == Mode::Session)
		|| oneColumn
		|| !controller()->content()->stackIsEmpty();
	_back->setVisible(backShown);

	_topBar->setGeometry(0, 0, width(), st::topBarHeight);
	_shadow->setGeometry(0, st::topBarHeight, width(), st::lineWidth);
	auto left = 0;
	if (backShown) {
		_back->moveToLeft(0, 0);
		left += _back->width();
	} else {
		left += st::topBarArrowPadding.right() - st::topBarInfoButtonInnerPosition.x();
	}
	_userpic->moveToLeft(left, 0);
	left += _userpic->width();
	_menuToggle->moveToRight(0, 0);
	const auto right = _menuToggle->width();
	_title->setGeometry(
		left,
		0,
		std::max(width() - left - right, 0),
		st::topBarHeight);

	const auto top = st::topBarHeight + st::lineWidth;
	const auto content = QRect(0, top, width(), std::max(height() - top, 0));
	_listScroll->setGeometry(content);
	_listContent->resizeToWidth(content.width());
	_sessionView->setGeometry(content);
}

void ShowGeneralAgent(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<GeneralAgentMemento>());
}

void ShowGeneralAgent(
		not_null<Window::SessionController*> controller,
		HandoffDraft draft) {
	auto memento = std::make_shared<GeneralAgentMemento>();
	memento->mode = Mode::Session;
	memento->handoff = std::move(draft);
	controller->showSection(std::move(memento));
}

void ShowGeneralAgentNewSession(
		not_null<Window::SessionController*> controller) {
	auto memento = std::make_shared<GeneralAgentMemento>();
	memento->mode = Mode::Session;
	controller->showSection(std::move(memento));
}

void ShowGeneralAgentSession(
		not_null<Window::SessionController*> controller,
		const QString &sessionId) {
	auto memento = std::make_shared<GeneralAgentMemento>();
	memento->mode = Mode::Session;
	memento->sessionId = sessionId;
	controller->showSection(std::move(memento));
}

} // namespace Agent::Ui
