/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
} // namespace Agent

namespace Ui {
class PlainShadow;
class ScrollArea;
class SettingsSlider;
} // namespace Ui

namespace Agent::Ui {

using namespace ::Ui;

class MemoryList;
class SessionRows;
class TasksView;

enum class HistoryTab : uchar {
	Sessions,
	Tasks,
	Memory,
};

class HistoryView final : public RpWidget {
public:
	HistoryView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);

	[[nodiscard]] rpl::producer<QString> openSessionRequests() const;
	[[nodiscard]] rpl::producer<QString> openRunRequests() const;

	void showTab(HistoryTab tab);
	[[nodiscard]] HistoryTab tab() const;
	void setInnerFocus();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateControlsGeometry();

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	HistoryTab _tab = HistoryTab::Sessions;
	SettingsSlider *_tabs = nullptr;
	PlainShadow *_shadow = nullptr;
	ScrollArea *_sessionsScroll = nullptr;
	SessionRows *_sessions = nullptr;
	ScrollArea *_tasksScroll = nullptr;
	TasksView *_tasks = nullptr;
	ScrollArea *_memoryScroll = nullptr;
	MemoryList *_memory = nullptr;
	rpl::event_stream<QString> _openSessionRequests;
	rpl::event_stream<QString> _openRunRequests;

};

} // namespace Agent::Ui
