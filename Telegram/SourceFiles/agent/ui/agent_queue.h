/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "ui/widgets/labels.h"
#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

class QueueStrip final : public RpWidget {
public:
	QueueStrip(QWidget *parent, not_null<Manager*> manager, Scope scope);

	[[nodiscard]] rpl::producer<> clicks() const;
	void setSuppressed(bool suppressed);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	const not_null<Manager*> _manager;
	const Scope _scope;
	object_ptr<FlatLabel> _title = { nullptr };
	int _waiting = 0;
	bool _suppressed = false;
	rpl::event_stream<> _clicks;

	void refresh();
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

};

void ShowQueueBox(
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope,
	Fn<void(QString sessionId)> open);

} // namespace Agent::Ui
