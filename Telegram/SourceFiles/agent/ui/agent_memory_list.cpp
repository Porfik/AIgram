/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_memory_list.h"

#include "agent/agent_manager.h"
#include "agent/agent_memories.h"
#include "agent/core/agent_memory.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_panel.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Agent::Ui {
namespace {

class FactRow final : public RpWidget {
public:
	FactRow(
		QWidget *parent,
		const QString &text,
		Fn<void()> edit,
		Fn<void()> remove);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	object_ptr<FlatLabel> _text;
	object_ptr<IconButton> _edit;
	object_ptr<IconButton> _remove;

};

FactRow::FactRow(
	QWidget *parent,
	const QString &text,
	Fn<void()> edit,
	Fn<void()> remove)
: RpWidget(parent)
, _text(this, text, st::agentMemoryFact)
, _edit(this, st::agentMemoryRowEdit)
, _remove(this, st::agentMemoryRowDelete) {
	_edit->setClickedCallback(std::move(edit));
	_remove->setClickedCallback(std::move(remove));
}

int FactRow::resizeGetHeight(int newWidth) {
	const auto &margins = st::agentMemoryFactMargins;
	_text->resizeToWidth(std::max(
		newWidth - margins.left() - margins.right(),
		st::agentMemoryFact.minWidth));
	_text->moveToLeft(margins.left(), margins.top(), newWidth);
	const auto line = st::agentMemoryFact.style.font->height;
	const auto top = margins.top() + (line - _edit->height()) / 2;
	const auto right = st::agentMemoryRowRight;
	_remove->moveToRight(right, top, newWidth);
	_edit->moveToRight(right + _remove->width(), top, newWidth);
	return margins.top() + _text->height() + margins.bottom();
}

} // namespace

void ShowMemoryResultToast(
		not_null<Window::SessionController*> controller,
		const MemoryResult &result) {
	if (result.error == MemoryError::None) {
		return;
	}
	const auto text = (result.error == MemoryError::Full)
		? tr::lng_agent_memory_full(tr::now)
		: (result.error == MemoryError::TooLong)
		? tr::lng_agent_memory_too_long(
			tr::now,
			lt_count,
			kMemoryMaxChars)
		: QString();
	if (!text.isEmpty()) {
		controller->showToast(text);
	}
}

void ShowMemoryEditBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		PeerKey owner,
		const QString &id,
		const QString &current) {
	controller->show(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_agent_memory_edit_title());
		const auto input = box->addRow(
			object_ptr<InputField>(
				box,
				st::defaultInputField,
				InputField::Mode::NoNewlines,
				tr::lng_agent_memory_placeholder(),
				current));
		input->setMaxLength(kMemoryMaxChars);
		box->setFocusCallback([=] {
			input->setFocusFast();
			input->selectAll();
		});
		const auto save = [=] {
			const auto text = NormalizeMemoryText(input->getLastText());
			if (text.isEmpty() || text == current) {
				box->closeBox();
				return;
			}
			const auto result = manager->memories().update(owner, id, text);
			if (result.error == MemoryError::None) {
				box->closeBox();
			} else {
				ShowMemoryResultToast(controller, result);
			}
		};
		input->submits() | rpl::on_next(save, box->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowMemoryDeleteBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		PeerKey owner,
		const QString &id) {
	controller->show(MakeConfirmBox({
		.text = tr::lng_agent_memory_delete_sure(tr::marked),
		.confirmed = [=](Fn<void()> close) {
			manager->memories().remove(owner, id);
			close();
		},
		.confirmText = tr::lng_box_delete(),
	}));
}

void ShowMemoryClearBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		PeerKey owner) {
	controller->show(MakeConfirmBox({
		.text = tr::lng_agent_memory_clear_sure(tr::marked),
		.confirmed = [=](Fn<void()> close) {
			manager->memories().clear(owner);
			close();
		},
		.confirmText = tr::lng_box_delete(),
	}));
}

void ShowMemoryAddBox(
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		PeerKey owner) {
	controller->show(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_agent_memory_add_title());
		const auto input = box->addRow(
			object_ptr<InputField>(
				box,
				st::defaultInputField,
				InputField::Mode::NoNewlines,
				tr::lng_agent_memory_placeholder()));
		input->setMaxLength(kMemoryMaxChars);
		box->setFocusCallback([=] {
			input->setFocusFast();
		});
		const auto save = [=] {
			const auto text = NormalizeMemoryText(input->getLastText());
			if (text.isEmpty()) {
				box->closeBox();
				return;
			}
			const auto result = manager->memories().add(owner, text);
			if (result.error == MemoryError::None) {
				box->closeBox();
			} else {
				ShowMemoryResultToast(controller, result);
			}
		};
		input->submits() | rpl::on_next(save, box->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

MemoryList::MemoryList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	PeerKey owner)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _owner(owner) {
	rebuild();

	_manager->memories().changes(
	) | rpl::filter([=](PeerKey owner) {
		return (owner == _owner);
	}) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
}

int MemoryList::resizeGetHeight(int newWidth) {
	Expects(_layout != nullptr);

	_layout->resizeToWidth(newWidth);
	return _layout->height();
}

void MemoryList::rebuild() {
	delete _layout;
	_layout = Ui::CreateChild<VerticalLayout>(this);
	_layout->move(0, 0);
	_layout->show();
	const auto container = _layout;

	const auto memory = _manager->memories().get(_owner);
	const auto &facts = memory.facts;
	if (facts.empty()) {
		container->add(
			object_ptr<FlatLabel>(
				container,
				(_owner == 0
					? tr::lng_agent_memory_empty_general()
					: tr::lng_agent_memory_empty_chat()),
				st::agentMemoryEmpty),
			st::agentMemoryEmptyMargins);
	} else {
		for (const auto &fact : facts) {
			container->add(object_ptr<FactRow>(
				container,
				fact.text,
				[=] { showEditBox(fact.id, fact.text); },
				[=] { showDeleteBox(fact.id); }));
		}
	}
	Ui::AddSkip(container, st::agentMemoryButtonSkip);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_memory_add(),
		st::settingsButtonActive,
		{ &st::agentMemoryAddIcon }
	)->setClickedCallback([=] {
		showAddBox();
	});
	if (!facts.empty()) {
		Settings::AddButtonWithIcon(
			container,
			tr::lng_agent_memory_clear(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			showClearBox();
		});
	}
	Ui::AddSkip(container, st::agentMemoryButtonSkip);
	Ui::AddDividerText(container, tr::lng_agent_memory_about());

	resizeToWidth(width());
}

void MemoryList::showAddBox() {
	ShowMemoryAddBox(_controller, _manager, _owner);
}

void MemoryList::showEditBox(const QString &id, const QString &text) {
	ShowMemoryEditBox(_controller, _manager, _owner, id, text);
}

void MemoryList::showDeleteBox(const QString &id) {
	ShowMemoryDeleteBox(_controller, _manager, _owner, id);
}

void MemoryList::showClearBox() {
	ShowMemoryClearBox(_controller, _manager, _owner);
}

} // namespace Agent::Ui
