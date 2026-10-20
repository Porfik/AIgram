/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_profile.h"

#include "agent/agent_chat.h"
#include "agent/agent_manager.h"
#include "agent/agent_memories.h"
#include "agent/agent_settings.h"
#include "agent/agent_tasks.h"
#include "agent/ui/agent_memory_list.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_section.h"
#include "agent/ui/agent_tasks_view.h"
#include "data/data_peer.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/sections/settings_agent.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_general.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QBuffer>

namespace Agent::Ui {
namespace {

constexpr auto kPhotoSize = 320;

[[nodiscard]] QByteArray PhotoToBytes(const QImage &image) {
	const auto scaled = image.scaled(
		kPhotoSize,
		kPhotoSize,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	scaled.save(&buffer, "JPG", 90);
	return result;
}

[[nodiscard]] QString DefaultModelLabel() {
	const auto providers = LoadProviders();
	const auto provider = DefaultProvider(providers);
	return provider
		? ModelTitle(*provider)
		: tr::lng_agent_media_model_none(tr::now);
}

void DefaultModelChoiceBox(not_null<::Ui::GenericBox*> box) {
	box->setTitle(tr::lng_agent_default_model_title());
	const auto providers = LoadProviders();
	const auto choices = providers.list;
	const auto current = DefaultProvider(providers);
	auto index = -1;
	for (auto i = 0; i != int(choices.size()); ++i) {
		if (current && choices[i].id == current->id) {
			index = i;
		}
	}
	const auto group = std::make_shared<::Ui::RadioenumGroup<int>>(index);
	for (auto i = 0; i != int(choices.size()); ++i) {
		box->addRow(
			object_ptr<::Ui::Radioenum<int>>(
				box,
				group,
				i,
				ModelTitle(choices[i]),
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int index) {
		const auto id = choices[index].id;
		UpdateProviders([&](ProvidersState &state) {
			state.defaultId = id;
		});
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace

ProfileMemento::ProfileMemento(not_null<PeerData*> peer, ProfileMode mode)
: Info::ContentMemento(peer, nullptr, nullptr, PeerId())
, _mode(mode) {
}

object_ptr<Info::ContentWidget> ProfileMemento::createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<ProfileWidget>(
		parent,
		controller,
		peer(),
		_mode);
	result->setInternalState(geometry, this);
	return result;
}

Info::Section ProfileMemento::section() const {
	return Info::Section(Info::Section::Type::Agent);
}

ProfileMode ProfileMemento::mode() const {
	return _mode;
}

ProfileWidget::ProfileWidget(
	QWidget *parent,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer,
	ProfileMode mode)
: Info::ContentWidget(parent, controller)
, _peer(peer)
, _mode(mode) {
	const auto window = controller->parentController();
	const auto manager = &peer->session().agent();
	auto owned = object_ptr<::Ui::VerticalLayout>(this);
	const auto container = owned.data();
	switch (_mode) {
	case ProfileMode::Main:
		setupMain(container);
		break;
	case ProfileMode::Tasks: {
		const auto tasks = container->add(object_ptr<TasksView>(
			container,
			window,
			manager,
			Scope::Account()));
		tasks->openRunRequests(
		) | rpl::on_next([=](const QString &id) {
			ShowGeneralAgentSession(window, id);
		}, tasks->lifetime());
	} break;
	case ProfileMode::Memory:
		container->add(object_ptr<MemoryList>(
			container,
			window,
			manager,
			PeerKey(0)));
		break;
	}
	setInnerWidget(std::move(owned));
}

void ProfileWidget::setupMain(not_null<::Ui::VerticalLayout*> container) {
	const auto window = controller()->parentController();
	const auto manager = &_peer->session().agent();
	const auto chat = &manager->chat();

	::Ui::AddSkip(container, st::agentProfileCoverSkip);
	container->add(
		object_ptr<::Ui::UserpicButton>(
			container,
			_peer,
			st::agentProfileUserpic),
		style::al_top);
	::Ui::AddSkip(container, st::agentProfileNameSkip);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			chat->nameValue(),
			st::agentProfileName),
		style::al_top);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_profile_status(),
			st::agentProfileStatus),
		style::al_top);
	::Ui::AddSkip(container, st::agentProfileCoverSkip);
	::Ui::AddDivider(container);
	::Ui::AddSkip(container);

	const auto modelLabel = rpl::single(
		rpl::empty
	) | rpl::then(
		ProvidersChanged()
	) | rpl::map(DefaultModelLabel);
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_default_model(),
		modelLabel,
		st::settingsButton,
		{ &st::agentMenuIconPass }
	)->setClickedCallback([=] {
		if (!LoadProviders().list.empty()) {
			window->show(Box(DefaultModelChoiceBox));
		}
	});

	const auto tasksLabel = rpl::single(
		rpl::empty
	) | rpl::then(
		manager->tasks().changes() | rpl::to_empty
	) | rpl::map([=] {
		return QString::number(manager->tasks().list(Scope::Account()).size());
	});
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_tasks_tab(),
		tasksLabel,
		st::settingsButton,
		{ &st::menuIconSchedule }
	)->setClickedCallback([=] {
		showMode(ProfileMode::Tasks);
	});

	const auto memoryLabel = rpl::single(
		rpl::empty
	) | rpl::then(
		manager->memories().changes() | rpl::filter([](PeerKey owner) {
			return !owner;
		}) | rpl::to_empty
	) | rpl::map([=] {
		return QString::number(manager->memories().get(0).facts.size());
	});
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_memory_tab(),
		memoryLabel,
		st::settingsButton,
		{ &st::menuIconFave }
	)->setClickedCallback([=] {
		showMode(ProfileMode::Memory);
	});

	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_profile_settings(),
		st::settingsButton,
		{ &st::menuIconSettings }
	)->setClickedCallback([=] {
		window->showSettings(Settings::AgentId());
	});
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_profile_about());
}

void ProfileWidget::showMode(ProfileMode mode) {
	controller()->showSection(std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<Info::ContentMemento>>{
			std::make_shared<ProfileMemento>(_peer, mode),
		}));
}

bool ProfileWidget::showInternal(not_null<Info::ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	} else if (const auto my = dynamic_cast<ProfileMemento*>(memento.get())) {
		return (my->mode() == _mode);
	}
	return false;
}

void ProfileWidget::setInternalState(
		const QRect &geometry,
		not_null<Info::ContentMemento*> memento) {
	setGeometry(geometry);
	::Ui::SendPendingMoveResizeEvents(this);
}

rpl::producer<QString> ProfileWidget::title() {
	switch (_mode) {
	case ProfileMode::Tasks: return tr::lng_agent_tasks_tab();
	case ProfileMode::Memory: return tr::lng_agent_memory_tab();
	case ProfileMode::Main: break;
	}
	return tr::lng_agent_profile_title();
}

void ProfileWidget::setupTopBar(
		not_null<Info::TopBar*> topBar,
		Info::Wrap wrap) {
	if (_mode != ProfileMode::Main) {
		return;
	}
	const auto layer = (wrap == Info::Wrap::Layer);
	const auto edit = topBar->addButton(
		base::make_unique_q<::Ui::IconButton>(
			topBar,
			(layer ? st::infoLayerTopBarEdit : st::infoTopBarEdit)));
	edit->setAccessibleName(tr::lng_agent_profile_edit_title(tr::now));
	edit->setClickedCallback([=] {
		ShowEditAgentProfileBox(controller()->parentController());
	});
}

std::shared_ptr<Info::ContentMemento> ProfileWidget::doCreateMemento() {
	return std::make_shared<ProfileMemento>(_peer, _mode);
}

void ShowEditAgentProfileBox(
		not_null<Window::SessionController*> controller) {
	const auto chat = &controller->session().agent().chat();
	controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
		struct State {
			std::optional<QImage> image;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto providers = LoadProviders();
		box->setTitle(tr::lng_agent_profile_edit_title());

		const auto userpic = box->addRow(
			object_ptr<::Ui::UserpicButton>(
				box,
				&controller->window(),
				::Ui::UserpicButton::Role::ChoosePhoto,
				st::defaultUserpicButton),
			style::al_top);
		userpic->showCustom(chat->photo());
		userpic->chosenImages(
		) | rpl::on_next([=](::Ui::UserpicButton::ChosenImage &&chosen) {
			state->image = chosen.image;
			userpic->showCustom(std::move(chosen.image));
		}, userpic->lifetime());

		const auto reset = box->addRow(
			object_ptr<::Ui::LinkButton>(
				box,
				tr::lng_agent_profile_photo_reset(tr::now)),
			st::agentProfileResetPadding,
			style::al_top);
		reset->setClickedCallback([=] {
			state->image = QImage();
			userpic->showCustom(DefaultAgentPhoto(kPhotoSize));
		});

		const auto name = box->addRow(
			object_ptr<::Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_profile_name(),
				providers.agentName),
			st::boxRowPadding);
		name->setMaxLength(64);
		box->setFocusCallback([=] {
			name->setFocusFast();
		});
		const auto save = [=] {
			const auto text = name->getLastText().trimmed();
			UpdateProviders([&](ProvidersState &providers) {
				providers.agentName = text;
				if (state->image) {
					providers.agentPhoto = state->image->isNull()
						? QByteArray()
						: PhotoToBytes(*state->image);
				}
			});
			box->closeBox();
		};
		name->submits() | rpl::on_next(save, name->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Agent::Ui
