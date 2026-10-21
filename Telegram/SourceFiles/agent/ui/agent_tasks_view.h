/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "agent/core/agent_task.h"
#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
} // namespace Agent

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Agent::Ui {

using namespace ::Ui;

class TasksView final : public RpWidget {
public:
	TasksView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);

	[[nodiscard]] rpl::producer<QString> openRunRequests() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;

	void setOpenTaskHandler(Fn<void(QString)> open);
	void showList();
	void showTask(const QString &taskId);
	void showSingleTask(const QString &taskId);
	[[nodiscard]] QString openedTask() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void rebuild();
	void buildList();
	void buildTask();
	void requestClose();
	[[nodiscard]] bool isBusy(const Task &task) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	QString _taskId;
	Fn<void(QString)> _openTask;
	bool _single = false;
	VerticalLayout *_layout = nullptr;
	rpl::event_stream<QString> _openRunRequests;
	rpl::event_stream<> _closeRequests;

};

} // namespace Agent::Ui
