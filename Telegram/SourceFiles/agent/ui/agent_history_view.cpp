/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_history_view.h"

#include "agent/agent_manager.h"
#include "agent/ui/agent_memory_list.h"
#include "agent/ui/agent_session_rows.h"
#include "agent/ui/agent_tasks_view.h"
#include "lang/lang_keys.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {

HistoryView::HistoryView(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope) {
	_tabs = CreateChild<SettingsSlider>(this, st::defaultTabsSlider);
	_tabs->addSection(tr::lng_agent_sessions_tab(tr::now));
	_tabs->addSection(tr::lng_agent_tasks_tab(tr::now));
	_tabs->addSection(tr::lng_agent_memory_tab(tr::now));
	_tabs->setActiveSectionFast(0);
	_tabs->show();
	_shadow = CreateChild<PlainShadow>(this);
	_shadow->show();

	_sessionsScroll = CreateChild<ScrollArea>(this);
	_sessions = _sessionsScroll->setOwnedWidget(
		object_ptr<SessionRows>(
			_sessionsScroll,
			_controller,
			_manager,
			_scope,
			true));
	_sessionsScroll->widthValue(
	) | rpl::on_next([=](int width) {
		_sessions->resizeToWidth(width);
	}, lifetime());
	_sessions->openRequests(
	) | rpl::start_to_stream(_openSessionRequests, lifetime());

	_memoryScroll = CreateChild<ScrollArea>(this);
	_memory = _memoryScroll->setOwnedWidget(
		object_ptr<MemoryList>(
			_memoryScroll,
			_controller,
			_manager,
			_scope.peer));
	_memoryScroll->widthValue(
	) | rpl::on_next([=](int width) {
		_memory->resizeToWidth(width);
	}, lifetime());

	_tasksScroll = CreateChild<ScrollArea>(this);
	_tasks = _tasksScroll->setOwnedWidget(
		object_ptr<TasksView>(
			_tasksScroll,
			_controller,
			_manager,
			_scope));
	_tasksScroll->widthValue(
	) | rpl::on_next([=](int width) {
		_tasks->resizeToWidth(width);
	}, lifetime());
	_tasks->openRunRequests(
	) | rpl::start_to_stream(_openRunRequests, lifetime());

	_tabs->sectionActivated(
	) | rpl::on_next([=](int section) {
		showTab(HistoryTab(section));
	}, lifetime());
	showTab(HistoryTab::Sessions);
}

rpl::producer<QString> HistoryView::openSessionRequests() const {
	return _openSessionRequests.events();
}

rpl::producer<QString> HistoryView::openRunRequests() const {
	return _openRunRequests.events();
}

void HistoryView::showTab(HistoryTab tab) {
	const auto focused = InFocusChain(this);
	_tab = tab;
	const auto index = int(tab);
	if (_tabs->activeSection() != index) {
		_tabs->setActiveSectionFast(index);
	}
	const auto scrolls = {
		std::pair{ _sessionsScroll, HistoryTab::Sessions },
		std::pair{ _tasksScroll, HistoryTab::Tasks },
		std::pair{ _memoryScroll, HistoryTab::Memory },
	};
	for (const auto &[scroll, which] : scrolls) {
		if (which == tab) {
			scroll->show();
		}
	}
	// WHY: hiding the focused tab first lets Qt pass the focus to the
	// next field of the window, the chats search, which then opens.
	if (focused) {
		setInnerFocus();
	}
	for (const auto &[scroll, which] : scrolls) {
		if (which != tab) {
			scroll->hide();
		}
	}
}

HistoryTab HistoryView::tab() const {
	return _tab;
}

void HistoryView::setInnerFocus() {
	if (_tab == HistoryTab::Memory) {
		_memory->setFocus();
	} else if (_tab == HistoryTab::Tasks) {
		_tasks->setFocus();
	} else {
		_sessions->setFocus();
	}
}

void HistoryView::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	updateControlsGeometry();
}

void HistoryView::updateControlsGeometry() {
	_tabs->resizeToWidth(width());
	_tabs->moveToLeft(0, 0, width());
	const auto top = _tabs->height();
	_shadow->setGeometry(0, top, width(), st::lineWidth);
	const auto content = QRect(
		0,
		top + st::lineWidth,
		width(),
		std::max(height() - top - st::lineWidth, 0));
	_sessionsScroll->setGeometry(content);
	_tasksScroll->setGeometry(content);
	_memoryScroll->setGeometry(content);
}

} // namespace Agent::Ui
