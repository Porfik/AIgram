/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_queue.h"

#include "agent/agent_manager.h"
#include "agent/agent_task_runner.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_task.h"
#include "agent/ui/agent_task_format.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_agent.h"
#include "styles/style_agent_queue.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>

namespace Agent::Ui {
namespace {

[[nodiscard]] QString TaskTitle(
		not_null<Manager*> manager,
		const QString &taskId) {
	if (const auto task = manager->tasks().find(taskId)) {
		return task->title;
	}
	return QString();
}

[[nodiscard]] QString RunSubtitle(
		not_null<Manager*> manager,
		const QString &taskId,
		const QString &sessionId,
		qint64 since) {
	auto trigger = QString();
	for (const auto &run : manager->tasks().runs(taskId)) {
		if (run.sessionId == sessionId) {
			trigger = FormatTaskRunTrigger(&manager->session(), run);
			break;
		}
	}
	const auto time = Ui::FormatDateTime(
		QDateTime::fromSecsSinceEpoch(since));
	return trigger.isEmpty() ? time : trigger + u" · "_q + time;
}

class QueueRow final : public SettingsButton {
public:
	QueueRow(QWidget *parent, QString title, QString subtitle);

	void updateTexts(QString title, QString subtitle);

protected:
	int resizeGetHeight(int newWidth) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<FlatLabel> _title;
	object_ptr<FlatLabel> _subtitle;

};

QueueRow::QueueRow(QWidget *parent, QString title, QString subtitle)
: SettingsButton(parent, nullptr, st::agentQueueRowButton)
, _title(this, std::move(title), st::agentQueueRowTitle)
, _subtitle(this, std::move(subtitle), st::agentQueueRowSubtitle) {
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void QueueRow::updateTexts(QString title, QString subtitle) {
	_title->setText(std::move(title));
	_subtitle->setText(std::move(subtitle));
	resizeToWidth(width());
}

int QueueRow::resizeGetHeight(int newWidth) {
	const auto avail = newWidth
		- st::agentQueueRowPadding.left()
		- st::agentQueueRowPadding.right();
	_title->resizeToWidth(avail);
	_subtitle->resizeToWidth(avail);
	return st::agentQueueRowPadding.top()
		+ _title->height()
		+ st::agentQueueRowGap
		+ _subtitle->height()
		+ st::agentQueueRowPadding.bottom();
}

void QueueRow::resizeEvent(QResizeEvent *e) {
	SettingsButton::resizeEvent(e);
	_title->moveToLeft(
		st::agentQueueRowPadding.left(),
		st::agentQueueRowPadding.top(),
		width());
	_subtitle->moveToLeft(
		st::agentQueueRowPadding.left(),
		st::agentQueueRowPadding.top() + _title->height() + st::agentQueueRowGap,
		width());
}

void FillQueueBox(
		not_null<GenericBox*> box,
		not_null<Manager*> manager,
		Scope scope,
		Fn<void(QString sessionId)> open) {
	box->setTitle(tr::lng_agent_queue_title());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	const auto refresh = [=] {
		container->clear();
		const auto waiting = manager->runner().waiting(scope);
		if (waiting.empty()) {
			container->add(
				object_ptr<FlatLabel>(
					container,
					tr::lng_agent_queue_empty(),
					st::agentFeedCardDetailLabel),
				st::boxRowPadding);
			return;
		}
		for (const auto &run : waiting) {
			const auto row = container->add(
				object_ptr<QueueRow>(
					container,
					TaskTitle(manager, run.taskId),
					RunSubtitle(
						manager,
						run.taskId,
						run.sessionId,
						run.since)));
			row->setClickedCallback([=] {
				open(run.sessionId);
				box->closeBox();
			});
		}
	};
	manager->runner().waitingChanges(
	) | rpl::on_next(refresh, container->lifetime());
	refresh();
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace

QueueStrip::QueueStrip(
	QWidget *parent,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _manager(manager)
, _scope(scope) {
	_title.create(this, QString(), st::agentQueueStripTitle);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_title->show();
	setCursor(style::cur_pointer);
	const auto count = [=] {
		auto result = 0;
		for (const auto &run : _manager->runner().waiting(_scope)) {
			result += run.cards;
		}
		return result;
	};
	_waiting = count();
	_manager->runner().waitingChanges(
	) | rpl::on_next([=] {
		_waiting = count();
		refresh();
	}, lifetime());
	refresh();
}

rpl::producer<> QueueStrip::clicks() const {
	return _clicks.events();
}

void QueueStrip::setSuppressed(bool suppressed) {
	if (_suppressed != suppressed) {
		_suppressed = suppressed;
		refresh();
	}
}

void QueueStrip::refresh() {
	resizeToWidth(width());
	setVisible(_waiting > 0 && !_suppressed);
}

void QueueStrip::mousePressEvent(QMouseEvent *e) {
	_clicks.fire({});
}

int QueueStrip::resizeGetHeight(int newWidth) {
	_title->setText(
		tr::lng_agent_queue_strip(tr::now, lt_count, _waiting));
	_title->resizeToWidth(newWidth
		- st::agentQueueStripTextLeft
		- st::agentQueueStripTextRight);
	_title->moveToLeft(
		st::agentQueueStripTextLeft,
		(st::agentQueueStripHeight - _title->height()) / 2,
		newWidth);
	update();
	return (_waiting && !_suppressed)
		? (st::agentQueueStripHeight + st::lineWidth)
		: 0;
}

void QueueStrip::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(
		QRect(0, 0, width(), st::agentQueueStripHeight),
		st::agentQueueStripBg);
	const auto icon = st::agentQueueStripArrowIcon;
	icon.paintInCenter(
		p,
		QRect(
			width() - st::agentQueueStripArrowRight - st::agentQueueStripArrowSize,
			(st::agentQueueStripHeight - st::agentQueueStripArrowSize) / 2,
			st::agentQueueStripArrowSize,
			st::agentQueueStripArrowSize));
	p.fillRect(
		QRect(0, st::agentQueueStripHeight, width(), st::lineWidth),
		st::shadowFg);
}

void ShowQueueBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope,
		Fn<void(QString sessionId)> open) {
	controller->show(Box(FillQueueBox, manager, scope, open));
}

} // namespace Agent::Ui
