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

namespace Agent::Ui {

using namespace ::Ui;

enum class HistoryTab : uchar {
	Sessions,
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

	void showTab(HistoryTab tab);
	[[nodiscard]] HistoryTab tab() const;
	void setInnerFocus();

private:
	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	HistoryTab _tab = HistoryTab::Sessions;
	rpl::event_stream<QString> _openSessionRequests;

};

} // namespace Agent::Ui
