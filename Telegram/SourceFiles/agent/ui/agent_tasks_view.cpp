/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_tasks_view.h"

#include "agent/agent_manager.h"
#include "agent/agent_task_runner.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_task.h"
#include "agent/ui/agent_spending_format.h"
#include "agent/ui/agent_task_format.h"
#include "base/unique_qptr.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_tasks.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QLocale>

namespace Agent::Ui {
namespace {

[[nodiscard]] QString ProviderTitle(const ProviderSettings &provider) {
	return !provider.title.isEmpty()
		? provider.title
		: provider.model;
}

[[nodiscard]] std::optional<double> ParseDouble(QString text) {
	text = text.trimmed();
	if (text.isEmpty()) {
		return 0.;
	}
	auto replaced = text;
	replaced.replace(QChar(','), QChar('.'));
	auto seenDot = false;
	auto digits = 0;
	for (const auto &ch : replaced) {
		if (ch == QChar('.')) {
			if (seenDot) {
				return std::nullopt;
			}
			seenDot = true;
		} else if (ch.isDigit()) {
			++digits;
		} else {
			return std::nullopt;
		}
	}
	if (!digits) {
		return std::nullopt;
	}
	return replaced.toDouble();
}

[[nodiscard]] std::optional<qint64> ParseInteger(
		not_null<Ui::InputField*> field) {
	const auto text = field->getLastText().trimmed();
	if (text.isEmpty()) {
		return 0;
	}
	auto seen = false;
	auto value = qint64(0);
	for (const auto &ch : text) {
		if (ch.isDigit()) {
			seen = true;
			if (value < 100'000'000) {
				value = value * 10 + ch.digitValue();
			}
		} else {
			return std::nullopt;
		}
	}
	return seen ? std::optional(value) : std::nullopt;
}

void ShowTaskInstructionBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &taskId,
		const QString &current) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_agent_task_instruction_title());
		const auto input = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				Ui::InputField::Mode::MultiLine,
				tr::lng_agent_task_instruction(),
				current));
		input->setMaxLength(kTaskMaxInstructionChars);
		box->setFocusCallback([=] {
			input->setFocusFast();
		});
		box->addButton(tr::lng_settings_save(), [=] {
			const auto text = input->getLastText();
			if (text != current) {
				if (const auto task = manager->tasks().find(taskId)) {
					auto copy = *task;
					copy.instruction = text;
					manager->tasks().update(copy);
				}
			}
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowTaskModelBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &taskId,
		const QString &currentId) {
	const auto choices = std::make_shared<std::vector<ProviderSettings>>(
		manager->providers().list);
	auto current = 0;
	for (auto i = 0; i < int(choices->size()); ++i) {
		if ((*choices)[i].id == currentId) {
			current = i + 1;
			break;
		}
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_agent_task_model_title());
		const auto group = std::make_shared<Ui::RadioenumGroup<int>>(current);
		box->addRow(
			object_ptr<Ui::Radioenum<int>>(
				box,
				group,
				0,
				tr::lng_agent_task_model_background(tr::now),
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
		for (auto i = 0; i < int(choices->size()); ++i) {
			box->addRow(
				object_ptr<Ui::Radioenum<int>>(
					box,
					group,
					i + 1,
					ProviderTitle((*choices)[i]),
					st::defaultBoxCheckbox),
				st::settingsSendTypePadding);
		}
		group->setChangedCallback([=](int index) {
			if (const auto task = manager->tasks().find(taskId)) {
				auto copy = *task;
				copy.providerId = index
					? (*choices)[index - 1].id
					: QString();
				manager->tasks().update(copy);
			}
			box->closeBox();
		});
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowTaskLimitsBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &taskId,
		TaskLimits current) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_agent_task_limits_title());
		const auto cost = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_task_limit_cost(),
				(current.dailyCost > 0.)
					? QString::number(current.dailyCost)
					: QString()));
		const auto tokens = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_task_limit_tokens(),
				(current.dailyTokens > 0)
					? QString::number(current.dailyTokens)
					: QString()));
		const auto actions = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_task_limit_actions(),
				(current.actionsPerHour > 0)
					? QString::number(current.actionsPerHour)
					: QString()));
		box->setFocusCallback([=] {
			cost->setFocusFast();
			cost->selectAll();
		});
		box->addButton(tr::lng_settings_save(), [=] {
			const auto newCost = ParseDouble(cost->getLastText());
			const auto newTokens = ParseInteger(tokens);
			const auto newActions = ParseInteger(actions);
			if (!newCost) {
				cost->showError();
			} else if (!newTokens) {
				tokens->showError();
			} else if (!newActions || !*newActions) {
				actions->showError();
			} else {
				if (const auto task = manager->tasks().find(taskId)) {
					auto copy = *task;
					copy.limits.dailyCost = *newCost;
					copy.limits.dailyTokens = *newTokens;
					copy.limits.actionsPerHour = int(*newActions);
					manager->tasks().update(copy);
				}
				box->closeBox();
			}
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowDeleteTaskBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		const QString &taskId,
		Fn<void()> after) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_agent_task_delete_sure(tr::marked),
		.confirmed = [=](Fn<void()> close) {
			manager->removeTask(taskId);
			after();
			close();
		},
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

class InfoRow final : public Ui::AbstractButton {
public:
	InfoRow(
		QWidget *parent,
		const QString &label,
		const QString &value,
		Fn<void()> click = nullptr,
		const QString &linkText = QString(),
		Fn<void()> linkClick = nullptr)
	: AbstractButton(parent)
	, _label(this, label, st::agentTasksLabel)
	, _value(this, value, st::agentTasksValue) {
		_label->setAttribute(Qt::WA_TransparentForMouseEvents);
		_value->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (click) {
			_clickable = true;
			setClickedCallback(std::move(click));
		}
		if (!linkText.isEmpty()) {
			_link = base::make_unique_q<Ui::LinkButton>(this, linkText);
			_link->setClickedCallback(std::move(linkClick));
		}
	}

	int resizeGetHeight(int newWidth) override {
		const auto margins = st::agentTasksRowMargins;
		_label->moveToLeft(margins.left(), margins.top(), newWidth);
		if (_link) {
			_link->moveToRight(
				margins.right() + st::agentTasksRowRight,
				margins.top(),
				newWidth);
		}
		_value->resizeToWidth(std::max(
			newWidth - margins.left() - margins.right(),
			st::agentTasksValue.minWidth));
		const auto valueTop = margins.top()
			+ _label->height()
			+ st::agentTasksRowValueGap;
		_value->moveToLeft(margins.left(), valueTop, newWidth);
		return valueTop + _value->height() + margins.bottom();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		if (isOver() && _clickable) {
			p.fillRect(rect(), st::windowBgOver);
		}
	}

	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

private:
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::FlatLabel> _value;
	base::unique_qptr<Ui::LinkButton> _link;
	bool _clickable = false;

};

class TaskRow final : public Ui::AbstractButton {
public:
	TaskRow(
		QWidget *parent,
		const QString &title,
		const QString &trigger,
		const QString &state,
		bool bright)
	: AbstractButton(parent)
	, _title(title)
	, _trigger(trigger)
	, _state(state)
	, _bright(bright) {
	}

	int resizeGetHeight(int newWidth) override {
		return st::agentTasksListRowHeight;
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

private:
	QString _title;
	QString _trigger;
	QString _state;
	bool _bright = false;

};

void TaskRow::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (isOver()) {
		p.fillRect(rect(), st::windowBgOver);
	}
	const auto padding = st::agentTasksListRowPadding;
	const auto &titleFont = st::agentTasksListTitleStyle.font;
	p.setPen(st::windowFg);
	p.setFont(titleFont);
	const auto textWidth = width() - padding.left() - padding.right();
	p.drawText(
		QRect(
			padding.left(),
			padding.top(),
			textWidth,
			titleFont->height),
		Qt::AlignLeft,
		titleFont->elided(_title, textWidth));
	const auto &infoFont = st::agentTasksListInfoStyle.font;
	const auto infoTop = padding.top() + titleFont->height;
	const auto stateWidth = infoFont->width(_state);
	const auto triggerWidth = std::max(
		textWidth - stateWidth - infoFont->width(u" \u00b7 "_q),
		0);
	p.setPen(st::windowSubTextFg);
	p.setFont(infoFont);
	p.drawText(
		QRect(
			padding.left(),
			infoTop,
			triggerWidth,
			infoFont->height),
		Qt::AlignLeft,
		infoFont->elided(_trigger, triggerWidth));
	p.setPen(_bright ? st::windowFgActive : st::windowSubTextFg);
	p.drawText(
		QPoint(width() - padding.right() - stateWidth, infoTop + infoFont->ascent),
		_state);
}

class RunRow final : public Ui::AbstractButton {
public:
	RunRow(
		QWidget *parent,
		const QString &trigger,
		const QString &time,
		const QString &outcome)
	: AbstractButton(parent)
	, _trigger(trigger)
	, _time(time)
	, _outcome(outcome) {
	}

	int resizeGetHeight(int newWidth) override {
		return st::agentTasksListRowHeight;
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

private:
	QString _trigger;
	QString _time;
	QString _outcome;

};

void RunRow::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (isOver()) {
		p.fillRect(rect(), st::windowBgOver);
	}
	const auto padding = st::agentTasksListRowPadding;
	const auto &triggerFont = st::agentTasksListTitleStyle.font;
	const auto &infoFont = st::agentTasksListInfoStyle.font;
	const auto textWidth = width() - padding.left() - padding.right();
	const auto timeWidth = infoFont->width(_time);
	const auto triggerWidth = std::max(
		textWidth - timeWidth - infoFont->width(u" \u00b7 "_q),
		0);
	p.setPen(st::windowFg);
	p.setFont(triggerFont);
	p.drawText(
		QRect(
			padding.left(),
			padding.top(),
			triggerWidth,
			triggerFont->height),
		Qt::AlignLeft,
		triggerFont->elided(_trigger, triggerWidth));
	p.setPen(st::windowSubTextFg);
	p.setFont(infoFont);
	p.drawText(
		QPoint(
			width() - padding.right() - timeWidth,
			padding.top() + triggerFont->ascent),
		_time);
	p.drawText(
		QRect(
			padding.left(),
			padding.top() + triggerFont->height,
			textWidth,
			infoFont->height),
		Qt::AlignLeft,
		infoFont->elided(_outcome, textWidth));
}

} // namespace
TasksView::TasksView(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope) {
	_manager->tasks().changes(
	) | rpl::on_next([=](const QString &id) {
		if (!_taskId.isEmpty() && !_manager->tasks().find(_taskId)) {
			_taskId = QString();
		}
		rebuild();
	}, lifetime());
	_manager->tasks().runChanges(
	) | rpl::on_next([=](const QString &) {
		if (!_taskId.isEmpty()) {
			rebuild();
		}
	}, lifetime());
	_manager->runner().runningChanges(
	) | rpl::on_next([=](const QString &) {
		rebuild();
	}, lifetime());
	_manager->runner().waitingChanges(
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	rebuild();
}

rpl::producer<QString> TasksView::openRunRequests() const {
	return _openRunRequests.events();
}

void TasksView::showList() {
	if (_taskId.isEmpty()) {
		return;
	}
	_taskId = QString();
	rebuild();
}

void TasksView::showTask(const QString &taskId) {
	if (_taskId == taskId) {
		return;
	}
	_taskId = taskId;
	rebuild();
}

QString TasksView::openedTask() const {
	return _taskId;
}

int TasksView::resizeGetHeight(int newWidth) {
	Expects(_layout != nullptr);

	_layout->resizeToWidth(newWidth);
	return _layout->height();
}

void TasksView::rebuild() {
	delete base::take(_layout);
	_layout = Ui::CreateChild<VerticalLayout>(this);
	_layout->move(0, 0);
	_layout->show();
	if (_taskId.isEmpty()) {
		buildList();
	} else {
		buildTask();
	}
	resizeToWidth(width());
}

bool TasksView::isBusy(const Task &task) const {
	if (_manager->runner().running(task.id)) {
		return true;
	}
	for (const auto &run : _manager->runner().waiting(task.scope)) {
		if (run.taskId == task.id) {
			return true;
		}
	}
	return false;
}

void TasksView::buildList() {
	const auto container = _layout;
	auto tasks = _manager->tasks().list(_scope);
	std::reverse(tasks.begin(), tasks.end());
	if (tasks.empty()) {
		container->add(
			object_ptr<FlatLabel>(
				container,
				tr::lng_agent_tasks_empty(),
				st::agentTasksEmpty),
			st::agentTasksEmptyMargins);
		return;
	}
	for (const auto &task : tasks) {
		container->add(object_ptr<TaskRow>(
			container,
			task.title,
			FormatTaskTrigger(&_manager->session(), task),
			FormatTaskState(_manager, task),
			isBusy(task)
		))->setClickedCallback([=, id = task.id] {
			showTask(id);
		});
	}
}

void TasksView::buildTask() {
	const auto task = _manager->tasks().find(_taskId);
	if (!task) {
		_taskId = QString();
		rebuild();
		return;
	}
	const auto id = _taskId;
	const auto container = _layout;
	const auto session = &_manager->session();

	const auto back = container->add(
		object_ptr<LinkButton>(
			container,
			tr::lng_agent_tasks_back(tr::now),
			st::defaultLinkButton),
		st::agentTasksBackMargins);
	back->setClickedCallback([=] {
		showList();
	});

	container->add(
		object_ptr<FlatLabel>(container, task->title, st::agentTasksTitle),
		st::agentTasksTitleMargins);

	container->add(object_ptr<InfoRow>(
		container,
		tr::lng_agent_task_trigger(tr::now),
		FormatTaskTrigger(session, *task)));

	const auto instruction = task->instruction;
	container->add(object_ptr<InfoRow>(
		container,
		tr::lng_agent_task_instruction(tr::now),
		instruction,
		nullptr,
		tr::lng_agent_task_edit_instruction(tr::now),
		[=] {
			ShowTaskInstructionBox(_controller, _manager, id, instruction);
		}));

	container->add(object_ptr<InfoRow>(
		container,
		tr::lng_agent_task_frame(tr::now),
		FormatTaskFrame(session, *task)));

	const auto providerId = task->providerId;
	container->add(object_ptr<InfoRow>(
		container,
		tr::lng_agent_task_model(tr::now),
		FormatTaskModel(_manager, *task),
		[=] {
			ShowTaskModelBox(_controller, _manager, id, providerId);
		}));

	const auto limits = task->limits;
	const auto costKnown = TaskCostKnown(_manager, *task);
	container->add(object_ptr<InfoRow>(
		container,
		tr::lng_agent_task_limits(tr::now),
		FormatTaskLimits(limits, costKnown),
		[=] {
			ShowTaskLimitsBox(_controller, _manager, id, limits);
		}));

	const auto notify = Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_task_notify(),
		st::settingsButtonNoIcon);
	notify->toggleOn(rpl::single(task->notify == TaskNotify::Actions));
	notify->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		if (const auto task = _manager->tasks().find(id)) {
			const auto next = checked
				? TaskNotify::Actions
				: TaskNotify::LogOnly;
			if (task->notify != next) {
				auto copy = *task;
				copy.notify = next;
				_manager->tasks().update(copy);
			}
		}
	}, container->lifetime());

	if (task->catchUp) {
		container->add(
			object_ptr<FlatLabel>(
				container,
				tr::lng_agent_task_catch_up(),
				st::agentTasksValueGray),
			st::agentTasksNoteMargins);
	}

	const auto spent = task->spentToday;
	container->add(
		object_ptr<FlatLabel>(
			container,
			(spent.hasCost
				? tr::lng_agent_task_spent_today(
					tr::now,
					lt_cost,
					FormatCost(spent.cost))
				: tr::lng_agent_task_spent_today_tokens(
					tr::now,
					lt_amount,
					Lang::FormatCountDecimal(spent.tokens()))),
			st::agentTasksValueGray),
		st::agentTasksNoteMargins);

	if (task->state != TaskState::Finished) {
		const auto paused = (task->state != TaskState::Active);
		Settings::AddButtonWithIcon(
			container,
			paused ? tr::lng_agent_task_resume() : tr::lng_agent_task_pause(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			if (const auto task = _manager->tasks().find(id)) {
				auto copy = *task;
				copy.state = paused ? TaskState::Active : TaskState::Paused;
				copy.pausedUntil = 0;
				_manager->tasks().update(copy);
			}
		});
	}

	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_task_delete(),
		st::settingsAttentionButton
	)->setClickedCallback([=] {
		ShowDeleteTaskBox(_controller, _manager, id, [=] {
			showList();
		});
	});

	Ui::AddDividerText(container, tr::lng_agent_task_change_hint());

	Ui::AddSkip(container, st::agentTasksRunsSkip);
	Ui::AddSubsectionTitle(container, tr::lng_agent_task_runs());
	const auto runs = _manager->tasks().runs(_taskId);
	if (runs.empty()) {
		container->add(
			object_ptr<FlatLabel>(
				container,
				tr::lng_agent_task_runs_empty(),
				st::agentTasksEmpty),
			st::agentTasksEmptyMargins);
	} else {
		for (const auto &run : runs) {
			container->add(object_ptr<RunRow>(
				container,
				FormatTaskRunTrigger(session, run),
				FormatDateTime(QDateTime::fromSecsSinceEpoch(run.started)),
				FormatTaskRunOutcome(run)
			))->setClickedCallback([=, sessionId = run.sessionId] {
				_openRunRequests.fire_copy(sessionId);
			});
		}
	}
	Ui::AddSkip(container);
}

} // namespace Agent::Ui
