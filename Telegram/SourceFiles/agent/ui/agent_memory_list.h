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
class VerticalLayout;
} // namespace Ui

namespace Agent::Ui {

using namespace ::Ui;

class MemoryList final : public RpWidget {
public:
	MemoryList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		PeerKey owner);

private:
	void rebuild();
	void showAddBox();
	void showEditBox(const QString &id, const QString &text);
	void showDeleteBox(const QString &id);
	void showClearBox();

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const PeerKey _owner = 0;
	VerticalLayout *_layout = nullptr;

protected:
	int resizeGetHeight(int newWidth) override;

};

} // namespace Agent::Ui
