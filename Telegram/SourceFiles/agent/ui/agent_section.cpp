/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_section.h"

#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/ui/agent_chats_row.h"
#include "agent/ui/agent_session_view.h"
#include "agent/ui/agent_sessions_list.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "ui/rp_widget.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_general.h"
#include "styles/style_agent_panel.h"
#include "styles/style_info.h"

namespace Agent::Ui {
namespace {

class GeneralAgentMemento final : public Window::SectionMemento {
public:
	GeneralAgentMemento() = default;

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	void setSessionId(const QString &id);
	const QString &sessionId() const;
	void setListOpen(bool open);
	bool listOpen() const;
	void setDraft(const QString &text);
	const QString &draft() const;
	void setHandoff(std::optional<HandoffDraft> draft);
	const std::optional<HandoffDraft> &handoff() const;

private:
	QString _sessionId;
	bool _listOpen = false;
	QString _draft;
	std::optional<HandoffDraft> _handoff;

};

class GeneralAgentSection final : public Window::SectionWidget {
public:
	GeneralAgentSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~GeneralAgentSection();

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
	void applyHandoff(HandoffDraft draft);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;

private:
	class Inner;

	object_ptr<Inner> _inner;

};

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
	if (const auto handoff = _handoff) {
		result->applyHandoff(*handoff);
	}
	return result;
}

void GeneralAgentMemento::setSessionId(const QString &id) {
	_sessionId = id;
}

const QString &GeneralAgentMemento::sessionId() const {
	return _sessionId;
}

void GeneralAgentMemento::setListOpen(bool open) {
	_listOpen = open;
}

bool GeneralAgentMemento::listOpen() const {
	return _listOpen;
}

void GeneralAgentMemento::setDraft(const QString &text) {
	_draft = text;
}

const QString &GeneralAgentMemento::draft() const {
	return _draft;
}

void GeneralAgentMemento::setHandoff(std::optional<HandoffDraft> draft) {
	_handoff = std::move(draft);
}

const std::optional<HandoffDraft> &GeneralAgentMemento::handoff() const {
	return _handoff;
}

class GeneralAgentSection::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void saveState(not_null<GeneralAgentMemento*> memento) const;
	void restoreState(not_null<GeneralAgentMemento*> memento);
	void applyHandoff(HandoffDraft draft);
	void setInnerFocus();
	[[nodiscard]] rpl::producer<QString> titleValue() const;
	[[nodiscard]] rpl::producer<bool> listOpenValue() const;
	[[nodiscard]] QRect floatPlayerRect() const;
	[[nodiscard]] bool floatPlayerWheel(QEvent *e);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupTopBar();
	void updateControlsGeometry();
	void updateBackVisibility();
	void showHistory(bool shown);
	void showSession(Session *session);
	void trackSession(Session *session);
	void updateTitle();
	[[nodiscard]] int contentTop() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	RpWidget *_topBar = nullptr;
	Ui::IconButton *_back = nullptr;
	Ui::FlatLabel *_title = nullptr;
	Ui::IconButton *_history = nullptr;
	Ui::IconButton *_new = nullptr;
	Ui::PlainShadow *_shadow = nullptr;
	SessionView *_sessionView = nullptr;
	ScrollArea *_scroll = nullptr;
	SessionsList *_sessionsList = nullptr;
	rpl::variable<bool> _listOpen = false;
	rpl::variable<QString> _titleText;
	rpl::lifetime _sessionLifetime;

};

GeneralAgentSection::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _manager(&controller->session().agent())
, _scope(Scope::Account()) {
	setupTopBar();
	_shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	_shadow->show();
	_sessionView = Ui::CreateChild<SessionView>(
		this,
		_controller,
		_manager,
		_scope);
	_sessionView->setContentMaxWidth(st::agentGeneralContentMaxWidth);
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

	const auto list = _manager->list(_scope);
	showSession(list.empty() ? nullptr : _manager->open(list.front().id));
}

void GeneralAgentSection::Inner::setupTopBar() {
	_topBar = Ui::CreateChild<RpWidget>(this);
	_topBar->show();
	_topBar->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_topBar);
		p.fillRect(clip, st::windowBg);
	}, _topBar->lifetime());

	_title = Ui::CreateChild<Ui::FlatLabel>(
		_topBar,
		_titleText.value(),
		st::infoTopBarTitle);
	_title->show();
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_history = Ui::CreateChild<Ui::IconButton>(
		_topBar,
		st::agentPanelTopBarHistory);
	_history->setAccessibleName(tr::lng_agent_history(tr::now));
	_history->setToolTip(tr::lng_agent_history(tr::now));
	_history->show();
	_history->setClickedCallback([=] {
		showHistory(!_listOpen.current());
		if (!_listOpen.current()) {
			_sessionView->setInnerFocus();
		}
	});
	listOpenValue(
	) | rpl::on_next([=](bool open) {
		_history->setIconOverride(
			open ? &st::agentPanelTopBarHistoryActive : nullptr);
	}, _history->lifetime());
	_new = Ui::CreateChild<Ui::IconButton>(
		_topBar,
		st::agentPanelTopBarNew);
	_new->setAccessibleName(tr::lng_agent_new_session(tr::now));
	_new->setToolTip(tr::lng_agent_new_session(tr::now));
	_new->show();
	_new->setClickedCallback([=] {
		showSession(nullptr);
		_sessionView->setInnerFocus();
	});

	const auto stackEmpty = _controller->content()->stackIsEmpty();
	const auto oneColumn = _controller->adaptive().isOneColumn();
	if (stackEmpty && !oneColumn) {
		return;
	}
	_back = Ui::CreateChild<Ui::IconButton>(
		_topBar,
		st::infoTopBarBack);
	_back->setAccessibleName(tr::lng_go_back(tr::now));
	_back->show();
	_back->setClickedCallback([=] {
		_controller->showBackFromStack();
	});
}

void GeneralAgentSection::Inner::updateBackVisibility() {
	if (!_back) {
		return;
	}
	const auto stackEmpty = _controller->content()->stackIsEmpty();
	const auto oneColumn = _controller->adaptive().isOneColumn();
	_back->setVisible(!stackEmpty || oneColumn);
}

void GeneralAgentSection::Inner::showHistory(bool shown) {
	_listOpen = shown;
	_sessionView->setVisible(!shown);
	_scroll->setVisible(shown);
	updateTitle();
	updateControlsGeometry();
}

void GeneralAgentSection::Inner::showSession(Session *session) {
	_sessionView->showSession(session);
	trackSession(session);
	showHistory(false);
}

void GeneralAgentSection::Inner::trackSession(Session *session) {
	_sessionLifetime = rpl::lifetime();
	if (session) {
		session->recordChanges(
		) | rpl::on_next([=] {
			updateTitle();
		}, _sessionLifetime);
	}
	updateTitle();
}

void GeneralAgentSection::Inner::updateTitle() {
	if (_listOpen.current()) {
		_titleText = tr::lng_agent_sessions_tab(tr::now);
	} else if (const auto session = _sessionView->session()) {
		const auto title = session->record().info.title;
		_titleText = title.isEmpty()
			? tr::lng_agent_session_untitled(tr::now)
			: title;
	} else {
		_titleText = tr::lng_agent_title(tr::now);
	}
}

void GeneralAgentSection::Inner::applyHandoff(HandoffDraft draft) {
	showSession(nullptr);
	_sessionView->startHandoff(std::move(draft));
}

void GeneralAgentSection::Inner::saveState(
		not_null<GeneralAgentMemento*> memento) const {
	const auto session = _sessionView->session();
	memento->setSessionId(session ? session->id() : QString());
	memento->setListOpen(_listOpen.current());
	memento->setDraft(_sessionView->draft());
}

void GeneralAgentSection::Inner::restoreState(
		not_null<GeneralAgentMemento*> memento) {
	if (!memento->sessionId().isEmpty()) {
		if (const auto session = _manager->open(memento->sessionId())) {
			showSession(session);
		}
	}
	if (!memento->draft().isEmpty()) {
		_sessionView->setDraft(memento->draft());
	}
	showHistory(memento->listOpen());
}

void GeneralAgentSection::Inner::setInnerFocus() {
	if (_listOpen.current()) {
		_sessionsList->setFocus();
	} else {
		_sessionView->setInnerFocus();
	}
}

rpl::producer<QString> GeneralAgentSection::Inner::titleValue() const {
	return _titleText.value();
}

rpl::producer<bool> GeneralAgentSection::Inner::listOpenValue() const {
	return _listOpen.value();
}

int GeneralAgentSection::Inner::contentTop() const {
	return st::infoTopBarHeight + st::lineWidth;
}

void GeneralAgentSection::Inner::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	updateControlsGeometry();
}

QRect GeneralAgentSection::Inner::floatPlayerRect() const {
	return _listOpen.current()
		? _scroll->geometry()
		: _sessionView->geometry();
}

bool GeneralAgentSection::Inner::floatPlayerWheel(QEvent *e) {
	return _listOpen.current() ? _scroll->viewportEvent(e) : false;
}

void GeneralAgentSection::Inner::updateControlsGeometry() {
	updateBackVisibility();
	_topBar->setGeometry(0, 0, width(), st::infoTopBarHeight);
	_shadow->setGeometry(
		0,
		st::infoTopBarHeight,
		width(),
		st::lineWidth);

	const auto historyRight = _history->width();
	_history->moveToRight(0, 0, width());
	_new->moveToRight(historyRight, 0, width());
	const auto left = (_back && !_back->isHidden())
		? st::infoTopBar.back.width
		: st::infoTopBar.titlePosition.x();
	const auto available = std::max(
		width() - historyRight - _new->width() - left,
		0);
	_title->resizeToWidth(available);
	_title->moveToLeft(
		left,
		st::infoTopBar.titlePosition.y(),
		width());

	const auto content = QRect(
		0,
		contentTop(),
		width(),
		height() - contentTop());
	_scroll->setGeometry(content);
	_sessionView->setGeometry(content);
}

GeneralAgentSection::GeneralAgentSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _inner(this, controller) {
	SetGeneralAgentShown(controller, true);
}

GeneralAgentSection::~GeneralAgentSection() {
	SetGeneralAgentShown(controller(), false);
}

bool GeneralAgentSection::hasTopBarShadow() const {
	return true;
}

QRect GeneralAgentSection::floatPlayerAvailableRect() {
	return mapToGlobal(_inner->floatPlayerRect());
}

bool GeneralAgentSection::floatPlayerHandleWheelEvent(QEvent *e) {
	return _inner->floatPlayerWheel(e);
}

void GeneralAgentSection::applyHandoff(HandoffDraft draft) {
	_inner->applyHandoff(std::move(draft));
}

bool GeneralAgentSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto my = dynamic_cast<GeneralAgentMemento*>(memento.get());
	if (!my) {
		return false;
	}
	_inner->restoreState(my);
	if (const auto handoff = my->handoff()) {
		_inner->applyHandoff(*handoff);
	}
	return true;
}

void GeneralAgentSection::setInternalState(
		const QRect &geometry,
		not_null<GeneralAgentMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	_inner->restoreState(memento);
}

std::shared_ptr<Window::SectionMemento> GeneralAgentSection::createMemento() {
	auto result = std::make_shared<GeneralAgentMemento>();
	_inner->saveState(result.get());
	return result;
}

void GeneralAgentSection::resizeEvent(QResizeEvent *e) {
	SectionWidget::resizeEvent(e);
	_inner->setGeometry(rect());
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
	_inner->setInnerFocus();
}

void ShowGeneralAgent(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<GeneralAgentMemento>());
}

void ShowGeneralAgent(
		not_null<Window::SessionController*> controller,
		HandoffDraft draft) {
	auto memento = std::make_shared<GeneralAgentMemento>();
	memento->setHandoff(std::move(draft));
	controller->showSection(std::move(memento));
}

} // namespace Agent::Ui
