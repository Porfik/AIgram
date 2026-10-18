/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_agent.h"

#include "agent/agent_check.h"
#include "agent/agent_settings.h"
#include "lang/lang_keys.h"
#include <rpl/map.h>
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

using namespace Builder;

struct AgentState {
	::Agent::ProvidersState providers;
	rpl::event_stream<> changed;

};

void SaveState(not_null<AgentState*> state) {
	::Agent::SaveProviders(state->providers);
	state->changed.fire({});
}

[[nodiscard]] QString ProviderRowTitle(
		const ::Agent::ProviderSettings &provider) {
	return !provider.title.isEmpty()
		? provider.title
		: !provider.model.isEmpty()
			? provider.model
			: tr::lng_agent_provider_default(tr::now);
}

class Agent final : public Section<Agent> {
public:
	Agent(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

void EditProviderBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<AgentState> state,
	::Agent::ProviderSettings provider);

void RebuildProviderRows(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	while (container->count()) {
		delete container->widgetAt(0);
	}
	const auto &list = state->providers.list;
	auto activeIndex = -1;
	auto index = 0;
	for (const auto &provider : list) {
		if (provider.id == state->providers.activeId) {
			activeIndex = index;
		}
		++index;
	}
	const auto group = (activeIndex >= 0)
		? std::make_shared<Ui::RadiobuttonGroup>(activeIndex)
		: std::make_shared<Ui::RadiobuttonGroup>();
	const auto &st = st::peerListBoxItem;
	for (auto i = 0; i != int(list.size()); ++i) {
		const auto &provider = list[i];
		const auto row = container->add(
			object_ptr<Ui::AbstractButton>(container));
		row->resize(row->width(), st.height);

		const auto name = row->lifetime().make_state<Ui::Text::String>(
			st.nameStyle,
			ProviderRowTitle(provider));
		const auto model = row->lifetime().make_state<Ui::Text::String>(
			st::defaultTextStyle,
			provider.model);
		const auto iconSize = st::menuIconBot.width();
		row->paintOn([=](QPainter &p) {
			const auto iconTop = (row->height() - iconSize) / 2;
			st::menuIconBot.paint(p, st::settingsButton.iconLeft, iconTop, row->width());
			const auto textLeft = st::settingsButton.padding.left();
			const auto textWidth = row->width()
				- textLeft
				- st::settingsButton.padding.right();
			p.setPen(st.nameFg);
			name->draw(p, {
				.position = { textLeft, st.namePosition.y() },
				.outerWidth = row->width(),
				.availableWidth = textWidth,
				.elisionLines = 1,
			});
			p.setPen(st.statusFg);
			model->draw(p, {
				.position = { textLeft, st.statusPosition.y() },
				.outerWidth = row->width(),
				.availableWidth = textWidth,
				.elisionLines = 1,
			});
		});
		const auto radio = Ui::CreateChild<Ui::Radiobutton>(
			row,
			group,
			i,
			QString());
		radio->setAttribute(Qt::WA_TransparentForMouseEvents);
		radio->show();
		row->widthValue(
		) | rpl::on_next([=](int width) {
			radio->moveToRight(
				st::settingsButton.padding.right(),
				(st.height - radio->checkRect().height()) / 2,
				width);
		}, radio->lifetime());

		row->setClickedCallback([=] {
			controller->show(Box(EditProviderBox, state, provider));
		});
	}
	container->resizeToWidth(container->width());
	container->showChildren();
}

void EditProviderBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<AgentState> state,
		::Agent::ProviderSettings provider) {
	const auto creating = provider.id.isEmpty();
	box->setTitle(creating
		? tr::lng_agent_new_provider()
		: tr::lng_agent_edit_provider());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	Ui::AddSkip(container);

	const auto title = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			provider.title),
		st::boxRowPadding);
	Ui::AddSkip(container);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_agent_provider_url(),
			st::boxLabel),
		st::boxRowPadding);
	const auto url = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_url_placeholder(),
			provider.baseUrl),
		st::boxRowPadding);
	Ui::AddSkip(container);
	auto keyWrap = object_ptr<Ui::RpWidget>(container);
	const auto key = Ui::CreateChild<Ui::PasswordInput>(
		keyWrap.data(),
		st::defaultInputField,
		tr::lng_agent_provider_key(),
		provider.apiKey);
	key->move(0, 0);
	key->heightValue(
	) | rpl::on_next([=, wrap = keyWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, key->lifetime());
	keyWrap->widthValue(
	) | rpl::on_next([=](int width) {
		key->resize(width, key->height());
	}, key->lifetime());
	container->add(std::move(keyWrap), st::boxRowPadding);
	Ui::AddSkip(container);
	const auto model = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_model(),
			provider.model),
		st::boxRowPadding);
	Ui::AddSkip(container);

	const auto wasActive = (provider.id == state->providers.activeId);
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			tr::lng_agent_provider_make_active(),
			creating ? state->providers.list.empty() : wasActive),
		st::boxRowPadding);
	Ui::AddSkip(container);

	const auto save = [=]() mutable {
		const auto newUrl = url->getLastText().trimmed();
		if (!newUrl.startsWith(u"http://"_q)
			&& !newUrl.startsWith(u"https://"_q)) {
			url->showError();
			box->uiShow()->showToast(tr::lng_agent_error_url(tr::now));
			return;
		}
		const auto newModel = model->getLastText().trimmed();
		if (newModel.isEmpty()) {
			model->showError();
			box->uiShow()->showToast(tr::lng_agent_error_model(tr::now));
			return;
		}
		provider.title = title->getLastText().trimmed();
		provider.baseUrl = newUrl;
		provider.apiKey = key->getLastText().trimmed();
		provider.model = newModel;
		auto &list = state->providers.list;
		if (creating) {
			provider.id = ::Agent::GenerateProviderId();
			list.push_back(provider);
			if (checkbox->checked() || list.size() == 1) {
				state->providers.activeId = provider.id;
			}
		} else {
			for (auto &item : list) {
				if (item.id == provider.id) {
					item = provider;
					break;
				}
			}
			if (checkbox->checked()) {
				state->providers.activeId = provider.id;
			} else if (wasActive) {
				state->providers.activeId = QString();
			}
		}
		SaveState(state.get());
		box->closeBox();
	};

	if (!creating) {
		box->addLeftButton(tr::lng_box_delete(), [=] {
			auto &list = state->providers.list;
			for (auto i = list.begin(); i != list.end(); ++i) {
				if (i->id == provider.id) {
					list.erase(i);
					break;
				}
			}
			if (state->providers.activeId == provider.id) {
				state->providers.activeId = QString();
			}
			SaveState(state.get());
			box->closeBox();
		});
	}
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void BuildProviders(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSubsectionTitle({
		.id = u"agent/providers"_q,
		.title = tr::lng_agent_providers_title(),
		.keywords = { u"providers"_q, u"api"_q, u"models"_q },
	});

	builder.add([=](const WidgetContext &ctx) {
		const auto container = ctx.container->add(
			object_ptr<Ui::VerticalLayout>(ctx.container));
		RebuildProviderRows(container, state, controller);
		state->changed.events(
		) | rpl::on_next([=] {
			RebuildProviderRows(container, state, controller);
		}, container->lifetime());
		return SectionBuilder::WidgetToAdd{};
	}, [] {
		return SearchEntry{
			.id = u"agent/providers/list"_q,
			.title = tr::lng_agent_providers_title(tr::now),
			.keywords = { u"provider"_q, u"model"_q, u"api key"_q },
		};
	});

	builder.addButton({
		.id = u"agent/add-provider"_q,
		.title = tr::lng_agent_add_provider(),
		.icon = { &st::menuIconBot },
		.onClick = [=] {
			controller->show(
				Box(EditProviderBox, state, ::Agent::ProviderSettings()));
		},
		.keywords = { u"add"_q, u"new"_q, u"create"_q },
	});
}

void BuildInstructions(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/instructions"_q,
		.title = tr::lng_agent_instructions_title(),
		.keywords = { u"instructions"_q, u"prompt"_q, u"system"_q },
	});

	builder.add([=](const WidgetContext &ctx) {
		const auto container = ctx.container;
		Ui::AddSkip(container);
		const auto field = container->add(
			object_ptr<Ui::InputField>(
				container,
				st::settingsBio,
				Ui::InputField::Mode::MultiLine,
				tr::lng_agent_instructions_placeholder(),
				state->providers.instructions),
			st::settingsBioMargins);
		const auto save = [=] {
			if (state->providers.instructions != field->getLastText()) {
				state->providers.instructions = field->getLastText();
				SaveState(state.get());
			}
		};
		field->focusedChanges(
		) | rpl::filter([=](bool focused) {
			return !focused;
		}) | rpl::on_next(save, field->lifetime());
		container->lifetime().add(save);
		return SectionBuilder::WidgetToAdd{};
	}, [] {
		return SearchEntry{
			.id = u"agent/instructions/text"_q,
			.title = tr::lng_agent_instructions_title(tr::now),
			.keywords = { u"instructions"_q, u"prompt"_q },
		};
	});
}

void BuildCheckButton(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	const auto button = builder.addButton({
		.id = u"agent/check"_q,
		.title = tr::lng_agent_check_button(),
		.icon = { &st::menuIconNetwork },
		.onClick = [=] {
			if (const auto active
					= ::Agent::ActiveProvider(state->providers)) {
				::Agent::ShowCheckBox(controller, *active);
			}
		},
		.keywords = { u"check"_q, u"test"_q, u"connection"_q },
	});
	const auto update = [=] {
		button->setDisabled(!::Agent::ActiveProvider(state->providers));
	};
	update();
	state->changed.events(
	) | rpl::on_next(update, builder.container()->lifetime());
}

void BuildEntryPoints(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state) {
	builder.addSkip();
	builder.addButton({
		.id = u"agent/show-entry-points"_q,
		.title = tr::lng_agent_show_entry_points(),
		.toggled = state->changed.events_starting_with(
			rpl::empty_value()
		) | rpl::map([=](rpl::empty_value) {
			return state->providers.showEntryPoints;
		}),
		.onClick = [=] {
			state->providers.showEntryPoints
				= !state->providers.showEntryPoints;
			SaveState(state.get());
		},
		.keywords = { u"show"_q, u"agent"_q, u"chats"_q },
	});
	builder.add([=](const WidgetContext &ctx) {
		Ui::AddDividerText(
			ctx.container,
			tr::lng_agent_show_entry_points_about());
		return SectionBuilder::WidgetToAdd{};
	}, nullptr);
}

Agent::Agent(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Agent::title() {
	return tr::lng_agent_title();
}

void Agent::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto state = std::make_shared<AgentState>();
	state->providers = ::Agent::LoadProviders();

	const SectionBuildMethod buildMethod = [state](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
		});

		BuildProviders(builder, state, controller);
		BuildInstructions(builder, state);
		BuildCheckButton(builder, state, controller);
		BuildEntryPoints(builder, state);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

const auto kMeta = BuildHelper({
	.id = Agent::Id(),
	.parentId = MainId(),
	.title = &tr::lng_agent_title,
	.icon = &st::menuIconBot,
}, [](SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"agent/providers"_q,
			.title = tr::lng_agent_providers_title(tr::now),
			.keywords = { u"providers"_q, u"api"_q, u"models"_q },
		};
	});

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"agent/instructions"_q,
			.title = tr::lng_agent_instructions_title(tr::now),
			.keywords = { u"instructions"_q, u"prompt"_q },
		};
	});

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"agent/check"_q,
			.title = tr::lng_agent_check_button(tr::now),
			.keywords = { u"check"_q, u"test"_q, u"connection"_q },
		};
	});
});

} // namespace

Type AgentId() {
	return Agent::Id();
}

} // namespace Settings
