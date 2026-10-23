/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_agent.h"

#include "agent/agent_chat.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_files.h"
#include "agent/agent_manager.h"
#include "agent/agent_memories.h"
#include "agent/agent_session_store.h"
#include "agent/agent_settings.h"
#include "agent/agent_spending_tracker.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_chatgpt_auth.h"
#include "agent/ui/agent_claude_boxes.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_profile.h"
#include "agent/ui/agent_spending_format.h"
#include "base/unixtime.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "core/file_utilities.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <rpl/map.h>
#include <rpl/then.h>

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

#include <memory>
#include <optional>

#include "styles/style_agent_general.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kInstructionsPreviewChars = 24;

struct AgentState {
	::Agent::ProvidersState providers;
	rpl::event_stream<> changed;

};

[[nodiscard]] std::optional<double> ParsePrice(QString text) {
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

void SaveState(not_null<AgentState*> state) {
	::Agent::SaveProviders(state->providers);
}

[[nodiscard]] QString ProviderRowTitle(
		const ::Agent::ProviderSettings &provider) {
	return ::Agent::Ui::ModelTitle(provider);
}

[[nodiscard]] rpl::producer<> StateValue(
		const std::shared_ptr<AgentState> &state) {
	return rpl::single(rpl::empty) | rpl::then(state->changed.events());
}

class Agent final : public Section<Agent> {
public:
	Agent(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

class AgentSpending final : public Section<AgentSpending> {
public:
	AgentSpending(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

class AgentCover final : public Ui::FixedHeightWidget {
public:
	AgentCover(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

private:
	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _edit;

};

AgentCover::AgentCover(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: FixedHeightWidget(
	parent,
	st::settingsPhotoTop
		+ st::infoProfileCover.photo.size.height()
		+ st::settingsPhotoBottom)
, _userpic(
	this,
	controller->session().agent().chat().user(),
	st::infoProfileCover.photo)
, _name(
	this,
	controller->session().agent().chat().nameValue(),
	st::infoProfileCover.name)
, _status(
	this,
	tr::lng_agent_profile_status(),
	st::infoProfileCover.status)
, _edit(
	this,
	tr::lng_agent_cover_edit(tr::link),
	st::infoProfileCover.status) {
	const auto edit = [=] {
		::Agent::Ui::ShowEditAgentProfileBox(controller);
	};
	_userpic->setClickedCallback(edit);
	_edit->overrideLinkClickHandler(edit);
	widthValue(
	) | rpl::on_next([=](int width) {
		const auto available = width
			- st::settingsNameLeft
			- st::infoProfileCover.rightSkip;
		_userpic->moveToLeft(
			st::settingsPhotoLeft,
			st::settingsPhotoTop,
			width);
		_name->resizeToNaturalWidth(available);
		_name->moveToLeft(st::settingsNameLeft, st::settingsNameTop, width);
		_status->resizeToNaturalWidth(available);
		_status->moveToLeft(
			st::settingsPhoneLeft,
			st::settingsPhoneTop,
			width);
		_edit->resizeToNaturalWidth(available);
		_edit->moveToLeft(
			st::settingsUsernameLeft,
			st::settingsUsernameTop,
			width);
	}, lifetime());
}

class HideChatsController final : public ChooseRecipientBoxController {
public:
	HideChatsController(
		not_null<Main::Session*> session,
		Fn<bool(not_null<Data::Thread*>)> filter,
		FnMut<void(not_null<Data::Thread*>)> callback)
	: ChooseRecipientBoxController({
		.session = session,
		.callback = std::move(callback),
		.filter = std::move(filter),
	}) {
	}

	void prepareViewHook() override;

};

void HideChatsController::prepareViewHook() {
	ChooseRecipientBoxController::prepareViewHook();
	delegate()->peerListSetTitle(tr::lng_agent_hidden_choose());
}

void HiddenChatsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_agent_hidden_box_title());
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});

	const auto exclusions
		= &controller->session().agent().exclusions();
	const auto container = box->verticalLayout();
	const auto rows = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto fill = [=] {
		rows->clear();
		const auto addFolderRow = [=](int folderId) {
			const auto &filters
				= controller->session().data().chatsFilters().list();
			auto title = QString();
			for (const auto &filter : filters) {
				if (filter.id() == folderId) {
					title = filter.title().text.text;
					break;
				}
			}
			if (title.isEmpty()) {
				return;
			}
			const auto row = rows->add(object_ptr<Ui::RpWidget>(rows));
			row->resize(row->width(), st::agentHiddenRowHeight);
			const auto icon = Ui::CreateChild<Ui::RpWidget>(row);
			icon->resize(
				st::agentHiddenRowUserpicSize,
				st::agentHiddenRowUserpicSize);
			icon->paintRequest(
			) | rpl::on_next([=] {
				auto p = QPainter(icon);
				st::agentHiddenRowIcon.paint(
					p,
					(icon->width() - st::agentHiddenRowIcon.width()) / 2,
					(icon->height() - st::agentHiddenRowIcon.height()) / 2,
					icon->width());
			}, icon->lifetime());
			const auto name = Ui::CreateChild<Ui::FlatLabel>(
				row,
				rpl::single(title),
				st::agentHiddenRowName);
			const auto show = Ui::CreateChild<Ui::IconButton>(
				row,
				st::agentHiddenRowShow);
			show->setClickedCallback([=] {
				exclusions->setFolderExcluded(folderId, false);
			});
			row->showChildren();
			row->sizeValue(
			) | rpl::on_next([=](QSize size) {
				icon->moveToLeft(
					st::boxRowPadding.left(),
					(size.height() - icon->height()) / 2);
				show->moveToRight(
					st::boxRowPadding.right(),
					(size.height() - show->height()) / 2);
				const auto textLeft = st::boxRowPadding.left()
					+ icon->width()
					+ st::agentHiddenRowSkip;
				const auto textRight = st::boxRowPadding.right()
					+ show->width()
					+ st::agentHiddenRowSkip;
				name->resizeToWidth(size.width() - textLeft - textRight);
				name->moveToLeft(
					textLeft,
					(size.height() - name->height()) / 2);
			}, row->lifetime());
		};
		const auto addChatRow = [=](not_null<PeerData*> peer) {
			const auto key = peer->id.value;
			const auto row = rows->add(object_ptr<Ui::RpWidget>(rows));
			row->resize(row->width(), st::agentHiddenRowHeight);
			const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
				row,
				peer,
				st::agentHiddenRowUserpic,
				Ui::PeerUserpicShape::Circle);
			userpic->showSavedMessagesOnSelf(peer->isSelf());
			const auto name = Ui::CreateChild<Ui::FlatLabel>(
				row,
				rpl::single(peer->name()),
				st::agentHiddenRowName);
			const auto show = Ui::CreateChild<Ui::IconButton>(
				row,
				st::agentHiddenRowShow);
			show->setClickedCallback([=] {
				exclusions->setPeerExcluded(key, false);
			});
			row->showChildren();
			row->sizeValue(
			) | rpl::on_next([=](QSize size) {
				userpic->moveToLeft(
					st::boxRowPadding.left(),
					(size.height() - userpic->height()) / 2);
				show->moveToRight(
					st::boxRowPadding.right(),
					(size.height() - show->height()) / 2);
				const auto textLeft = st::boxRowPadding.left()
					+ userpic->width()
					+ st::agentHiddenRowSkip;
				const auto textRight = st::boxRowPadding.right()
					+ show->width()
					+ st::agentHiddenRowSkip;
				name->resizeToWidth(size.width() - textLeft - textRight);
				name->moveToLeft(
					textLeft,
					(size.height() - name->height()) / 2);
			}, row->lifetime());
		};
		const auto &list = exclusions->list();
		auto hiddenFolders = false;
		auto hiddenChats = false;
		for (const auto folderId : list.folders) {
			if (!hiddenFolders) {
				hiddenFolders = true;
				rows->add(
					object_ptr<Ui::FlatLabel>(
						rows,
						tr::lng_agent_hidden_folders(),
						st::boxLabel),
					st::boxRowPadding);
			}
			addFolderRow(folderId);
		}
		for (const auto peerId : list.peers) {
			const auto peer = controller->session().data().peerLoaded(
				PeerId(peerId));
			if (!peer) {
				continue;
			}
			if (!hiddenChats) {
				hiddenChats = true;
				rows->add(
					object_ptr<Ui::FlatLabel>(
						rows,
						tr::lng_agent_hidden_chats(),
						st::boxLabel),
					st::boxRowPadding);
			}
			addChatRow(peer);
		}
		if (!hiddenFolders && !hiddenChats) {
			rows->add(
				object_ptr<Ui::FlatLabel>(
					rows,
					tr::lng_agent_hidden_empty(),
					st::boxLabel),
				st::boxRowPadding);
		}
		rows->resizeToWidth(rows->width());
		rows->showChildren();
	};
	const auto weakRows = QPointer<Ui::VerticalLayout>(rows);
	exclusions->changes(
	) | rpl::on_next([=] {
		crl::on_main([=] {
			if (weakRows) {
				fill();
			}
		});
	}, container->lifetime());
	fill();
	Ui::AddSkip(container);
	const auto add = AddButtonWithIcon(
		container,
		tr::lng_agent_hidden_add(),
		st::settingsButtonActive,
		{ &st::agentHiddenAddIcon });
	add->setClickedCallback([=] {
		const auto picker = std::make_shared<
			base::weak_qptr<Ui::BoxContent>>();
		*picker = controller->show(
			Box<PeerListBox>(
				std::make_unique<HideChatsController>(
					&controller->session(),
					[=](not_null<Data::Thread*> thread) {
						return !exclusions->excluded(
							thread->peer()->id.value);
					},
					[=](not_null<Data::Thread*> thread) {
						exclusions->setPeerExcluded(
							thread->peer()->id.value,
							true);
						if (const auto strong = picker->get()) {
							strong->closeBox();
						}
					}),
				[=](not_null<PeerListBox*> box) {
					box->addButton(tr::lng_cancel(), [=] {
						box->closeBox();
					});
				}));
	});
}

[[nodiscard]] QString RolesText(
		const ::Agent::ProvidersState &providers,
		const QString &id) {
	auto roles = QStringList();
	const auto main = ::Agent::DefaultProvider(providers);
	if (main && main->id == id) {
		roles.push_back(tr::lng_agent_role_default(tr::now));
	}
	if (!id.isEmpty() && providers.backgroundId == id) {
		roles.push_back(tr::lng_agent_role_background(tr::now));
	}
	if (!id.isEmpty() && providers.mediaId == id) {
		roles.push_back(tr::lng_agent_role_media(tr::now));
	}
	return roles.isEmpty()
		? QString()
		: (QString(QChar(0x00B7)) + ' ' + roles.join(u", "_q));
}

void RebuildProviderRows(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	while (container->count()) {
		delete container->widgetAt(0);
	}
	const auto &st = st::peerListBoxItem;
	for (const auto &provider : state->providers.list) {
		const auto row = container->add(
			object_ptr<Ui::AbstractButton>(container));
		row->resize(row->width(), st.height);

		const auto name = row->lifetime().make_state<Ui::Text::String>(
			st.nameStyle,
			ProviderRowTitle(provider));
		const auto about = row->lifetime().make_state<Ui::Text::String>(
			st::defaultTextStyle,
			::Agent::Ui::ModelDescription(provider));
		const auto roles = row->lifetime().make_state<Ui::Text::String>(
			st::defaultTextStyle,
			RolesText(state->providers, provider.id));
		const auto service = provider.service;
		const auto &icon = (service == ::Agent::ProviderService::OpenRouter)
			? st::agentModelChoiceOpenRouterIcon
			: (service == ::Agent::ProviderService::Anthropic
				|| service == ::Agent::ProviderService::ClaudeSubscription)
			? st::agentModelChoiceAnthropicIcon
			: (service == ::Agent::ProviderService::OpenAi
				|| service == ::Agent::ProviderService::ChatGptSubscription)
			? st::agentModelChoiceOpenAiIcon
			: st::agentModelChoiceCustomIcon;
		row->paintOn([=, &icon](QPainter &p) {
			if (row->isOver()) {
				p.fillRect(row->rect(), st::windowBgOver);
			}
			icon.paint(
				p,
				st::settingsButton.iconLeft,
				(row->height() - icon.height()) / 2,
				row->width());
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
			const auto space = st::defaultTextStyle.font->spacew;
			const auto rolesWidth = roles->isEmpty()
				? 0
				: std::min(roles->maxWidth() + space, textWidth / 2);
			const auto aboutWidth = textWidth - rolesWidth;
			p.setPen(st.statusFg);
			about->draw(p, {
				.position = { textLeft, st.statusPosition.y() },
				.outerWidth = row->width(),
				.availableWidth = aboutWidth,
				.elisionLines = 1,
			});
			if (rolesWidth > 0) {
				p.setPen(st.statusFgActive);
				roles->draw(p, {
					.position = {
						(textLeft
							+ std::min(about->maxWidth(), aboutWidth)
							+ space),
						st.statusPosition.y(),
					},
					.outerWidth = row->width(),
					.availableWidth = rolesWidth - space,
					.elisionLines = 1,
				});
			}
		});
		row->setClickedCallback([=, id = provider.id] {
			::Agent::Ui::ShowEditModel(controller, id);
		});
	}
	container->resizeToWidth(container->width());
	container->showChildren();
}

[[nodiscard]] QString DefaultModelText(
		const std::shared_ptr<AgentState> &state) {
	const auto provider = ::Agent::DefaultProvider(state->providers);
	return provider
		? ProviderRowTitle(*provider)
		: tr::lng_agent_media_model_none(tr::now);
}

void DefaultModelBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<AgentState> state) {
	box->setTitle(tr::lng_agent_default_model_title());
	const auto choices = state->providers.list;
	const auto current = ::Agent::DefaultProvider(state->providers);
	auto index = -1;
	for (auto i = 0; i != int(choices.size()); ++i) {
		if (current && choices[i].id == current->id) {
			index = i;
			break;
		}
	}
	const auto group = std::make_shared<Ui::RadioenumGroup<int>>(index);
	for (auto i = 0; i != int(choices.size()); ++i) {
		box->addRow(
			object_ptr<Ui::Radioenum<int>>(
				box,
				group,
				i,
				ProviderRowTitle(choices[i]),
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int index) {
		state->providers.defaultId = choices[index].id;
		SaveState(state.get());
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

[[nodiscard]] QString MediaModelText(
		const std::shared_ptr<AgentState> &state) {
	const auto id = state->providers.mediaId;
	if (!id.isEmpty()) {
		for (const auto &provider : state->providers.list) {
			if (provider.id == id) {
				return ProviderRowTitle(provider);
			}
		}
	}
	return tr::lng_agent_media_model_none(tr::now);
}

void MediaModelBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<AgentState> state) {
	box->setTitle(tr::lng_agent_media_model_title());
	const auto choices = [&] {
		auto result = std::vector<::Agent::ProviderSettings>{};
		auto &none = result.emplace_back();
		none.title = tr::lng_agent_media_model_none(tr::now);
		for (const auto &provider : state->providers.list) {
			result.push_back(provider);
		}
		return result;
	}();
	const auto group = std::make_shared<Ui::RadioenumGroup<int>>(-1);
	auto current = -1;
	for (auto i = 0; i < int(choices.size()); ++i) {
		if (choices[i].id == state->providers.mediaId) {
			current = i;
			break;
		}
	}
	group->setValue(current);
	for (const auto &provider : choices) {
		const auto index = &provider - choices.data();
		box->addRow(
			object_ptr<Ui::Radioenum<int>>(
				box,
				group,
				index,
				index ? ProviderRowTitle(provider) : provider.title,
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int index) {
		state->providers.mediaId = choices[index].id;
		SaveState(state.get());
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

[[nodiscard]] QString BackgroundModelText(
		const std::shared_ptr<AgentState> &state) {
	const auto id = state->providers.backgroundId;
	if (!id.isEmpty()) {
		for (const auto &provider : state->providers.list) {
			if (provider.id == id) {
				return ProviderRowTitle(provider);
			}
		}
	}
	return tr::lng_agent_background_model_main(tr::now);
}

void BackgroundModelBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<AgentState> state) {
	box->setTitle(tr::lng_agent_background_model_title());
	const auto choices = [&] {
		auto result = std::vector<::Agent::ProviderSettings>{};
		auto &main = result.emplace_back();
		main.title = tr::lng_agent_background_model_main(tr::now);
		for (const auto &provider : state->providers.list) {
			result.push_back(provider);
		}
		return result;
	}();
	const auto group = std::make_shared<Ui::RadioenumGroup<int>>(-1);
	auto current = 0;
	for (auto i = 0; i < int(choices.size()); ++i) {
		if (choices[i].id == state->providers.backgroundId) {
			current = i;
			break;
		}
	}
	group->setValue(current);
	for (const auto &provider : choices) {
		const auto index = &provider - choices.data();
		box->addRow(
			object_ptr<Ui::Radioenum<int>>(
				box,
				group,
				index,
				index ? ProviderRowTitle(provider) : provider.title,
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int index) {
		state->providers.backgroundId = choices[index].id;
		SaveState(state.get());
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void BuildCover(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = object_ptr<AgentCover>(ctx.container, ctx.controller),
		};
	}, [] {
		return SearchEntry{
			.id = u"agent/profile"_q,
			.title = tr::lng_agent_cover_edit(tr::now),
			.keywords = { u"name"_q, u"photo"_q, u"avatar"_q, u"agent"_q },
		};
	});
	builder.addDivider();
}

void BuildModels(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	const auto empty = [=] {
		return state->providers.list.empty();
	};
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/providers"_q,
		.title = tr::lng_agent_providers_title(),
		.keywords = { u"models"_q, u"providers"_q, u"api"_q },
	});
	builder.scope([&] {
		builder.add([=](const WidgetContext &ctx) {
			const auto container = ctx.container;
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					tr::lng_agent_models_empty(),
					st::agentSettingsEmptyAbout),
				st::agentSettingsEmptyAboutPadding);
			const auto add = container->add(
				object_ptr<Ui::RoundButton>(
					container,
					tr::lng_agent_add_provider(),
					st::agentSettingsAddFirst),
				st::agentSettingsAddFirstPadding);
			add->setClickedCallback([=] {
				::Agent::Ui::ShowAddModel(controller);
			});
			container->widthValue(
			) | rpl::on_next([=](int width) {
				const auto &padding = st::agentSettingsAddFirstPadding;
				add->setFullWidth(width - padding.left() - padding.right());
			}, add->lifetime());
			return SectionBuilder::WidgetToAdd{};
		}, nullptr);
	}, StateValue(state) | rpl::map(empty));
	builder.scope([&] {
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
				.keywords = { u"model"_q, u"provider"_q, u"api key"_q },
			};
		});
		builder.addButton({
			.id = u"agent/add-provider"_q,
			.title = tr::lng_agent_add_provider(),
			.icon = { &st::menuIconAdd },
			.onClick = [=] {
				::Agent::Ui::ShowAddModel(controller);
			},
			.keywords = { u"add"_q, u"new"_q, u"model"_q, u"openrouter"_q },
		});
		builder.addSkip();
	}, StateValue(state) | rpl::map([=] { return !empty(); }));
}

void BuildRoles(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	const auto empty = [=] {
		return state->providers.list.empty();
	};
	builder.scope([&] {
		builder.addDivider();
		builder.addSkip();
		builder.addSubsectionTitle({
			.id = u"agent/roles"_q,
			.title = tr::lng_agent_roles_title(),
			.keywords = { u"roles"_q, u"default"_q, u"model"_q },
		});
		builder.addButton({
			.id = u"agent/default-model"_q,
			.title = tr::lng_agent_default_model(),
			.st = &st::settingsButtonNoIcon,
			.label = StateValue(state) | rpl::map([=] {
				return DefaultModelText(state);
			}),
			.onClick = [=] {
				if (!state->providers.list.empty()) {
					controller->show(Box(DefaultModelBox, state));
				}
			},
			.keywords = { u"default"_q, u"model"_q },
		});
		builder.addButton({
			.id = u"agent/background/model"_q,
			.title = tr::lng_agent_background_model(),
			.st = &st::settingsButtonNoIcon,
			.label = StateValue(state) | rpl::map([=] {
				return BackgroundModelText(state);
			}),
			.onClick = [=] {
				controller->show(Box(BackgroundModelBox, state));
			},
			.keywords = { u"background"_q, u"model"_q },
		});
		builder.addButton({
			.id = u"agent/media/model"_q,
			.title = tr::lng_agent_media_model(),
			.st = &st::settingsButtonNoIcon,
			.label = StateValue(state) | rpl::map([=] {
				return MediaModelText(state);
			}),
			.onClick = [=] {
				controller->show(Box(MediaModelBox, state));
			},
			.keywords = { u"media"_q, u"model"_q },
		});
		builder.addSkip();
		builder.addDividerText(tr::lng_agent_roles_about());
	}, StateValue(state) | rpl::map([=] { return !empty(); }));
	builder.scope([&] {
		builder.addDivider();
	}, StateValue(state) | rpl::map(empty));
}

[[nodiscard]] QString InstructionsPreview(const QString &text) {
	const auto line = text.simplified();
	if (line.isEmpty()) {
		return tr::lng_agent_instructions_empty(tr::now);
	}
	return (line.size() > kInstructionsPreviewChars)
		? (line.left(kInstructionsPreviewChars).trimmed() + QChar(0x2026))
		: line;
}

void InstructionsBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_agent_instructions_box_title());
	box->setWidth(st::boxWideWidth);
	const auto current = ::Agent::LoadProviders().instructions;
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_agent_instructions_field(),
			current),
		st::boxRowPadding);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_agent_instructions_box_about());
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto save = [=] {
		const auto text = field->getLastText().trimmed();
		if (text != current) {
			::Agent::UpdateProviders([&](::Agent::ProvidersState &state) {
				state.instructions = text;
			});
		}
		box->closeBox();
	};
	field->submits(
	) | rpl::on_next(save, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void ShowProfilePage(
		not_null<Window::SessionController*> controller,
		::Agent::Ui::ProfileMode mode) {
	controller->showSection(std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<Info::ContentMemento>>{
			std::make_shared<::Agent::Ui::ProfileMemento>(
				controller->session().agent().chat().user(),
				mode),
		}));
}

void BuildAgentBlock(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	const auto manager = &controller->session().agent();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/agent"_q,
		.title = tr::lng_agent_settings_agent(),
		.keywords = { u"agent"_q },
	});
	builder.addButton({
		.id = u"agent/instructions"_q,
		.title = tr::lng_agent_instructions_title(),
		.icon = { &st::menuIconEdit },
		.label = StateValue(state) | rpl::map([=] {
			return InstructionsPreview(state->providers.instructions);
		}),
		.onClick = [=] {
			controller->show(Box(InstructionsBox));
		},
		.keywords = { u"instructions"_q, u"prompt"_q, u"system"_q },
	});
	builder.addButton({
		.id = u"agent/memory"_q,
		.title = tr::lng_agent_memory_tab(),
		.icon = { &st::menuIconFave },
		.label = rpl::single(
			rpl::empty
		) | rpl::then(
			manager->memories().changes() | rpl::filter([](
					::Agent::PeerKey owner) {
				return !owner;
			}) | rpl::to_empty
		) | rpl::map([=] {
			return QString::number(manager->memories().get(0).facts.size());
		}),
		.onClick = [=] {
			ShowProfilePage(controller, ::Agent::Ui::ProfileMode::Memory);
		},
		.keywords = { u"memory"_q, u"facts"_q, u"remember"_q },
	});
	builder.addButton({
		.id = u"agent/tasks"_q,
		.title = tr::lng_agent_tasks_tab(),
		.icon = { &st::menuIconSchedule },
		.label = rpl::single(
			rpl::empty
		) | rpl::then(
			manager->tasks().changes() | rpl::to_empty
		) | rpl::map([=] {
			return QString::number(
				manager->tasks().list(::Agent::Scope::Account()).size());
		}),
		.onClick = [=] {
			ShowProfilePage(controller, ::Agent::Ui::ProfileMode::Tasks);
		},
		.keywords = { u"tasks"_q, u"schedule"_q, u"background"_q },
	});
	builder.addButton({
		.id = u"agent/journal"_q,
		.title = tr::lng_agent_journal(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			ShowProfilePage(controller, ::Agent::Ui::ProfileMode::Journal);
		},
		.keywords = { u"journal"_q, u"actions"_q, u"undo"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_agent_instructions_about());
}

[[nodiscard]] QString RowPreviewText(::Agent::RowPreview mode) {
	switch (mode) {
	case ::Agent::RowPreview::LastMessage:
		return tr::lng_agent_row_preview_message(tr::now);
	case ::Agent::RowPreview::SessionTitle:
		return tr::lng_agent_row_preview_session(tr::now);
	case ::Agent::RowPreview::Hint:
		return tr::lng_agent_row_about(tr::now);
	}
	Unexpected("Mode in RowPreviewText.");
}

void RowPreviewBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<AgentState> state) {
	using Mode = ::Agent::RowPreview;
	box->setTitle(tr::lng_agent_row_preview_title());
	const auto group = std::make_shared<Ui::RadioenumGroup<Mode>>(
		state->providers.rowPreview);
	const auto modes = { Mode::LastMessage, Mode::SessionTitle, Mode::Hint };
	for (const auto mode : modes) {
		box->addRow(
			object_ptr<Ui::Radioenum<Mode>>(
				box,
				group,
				mode,
				RowPreviewText(mode),
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](Mode mode) {
		state->providers.rowPreview = mode;
		SaveState(state.get());
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void BuildChatList(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/chat-list"_q,
		.title = tr::lng_agent_settings_chats(),
		.keywords = { u"chats"_q, u"list"_q, u"agent"_q },
	});
	const auto show = builder.addButton({
		.id = u"agent/show-entry-points"_q,
		.title = tr::lng_agent_show_entry_points(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(state->providers.showEntryPoints),
		.keywords = { u"show"_q, u"agent"_q, u"chats"_q },
	});
	auto shown = rpl::producer<bool>();
	if (show) {
		show->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != state->providers.showEntryPoints);
		}) | rpl::on_next([=](bool toggled) {
			state->providers.showEntryPoints = toggled;
			SaveState(state.get());
		}, show->lifetime());
		shown = show->toggledValue();
	}
	builder.scope([&] {
		builder.addButton({
			.id = u"agent/row-preview"_q,
			.title = tr::lng_agent_row_preview(),
			.st = &st::settingsButtonNoIcon,
			.label = StateValue(state) | rpl::map([=] {
				return RowPreviewText(state->providers.rowPreview);
			}),
			.onClick = [=] {
				controller->show(Box(RowPreviewBox, state));
			},
			.keywords = { u"row"_q, u"preview"_q, u"agent"_q },
		});
		const auto botIcon = builder.addButton({
			.id = u"agent/bot-icon"_q,
			.title = tr::lng_agent_show_bot_icon(),
			.st = &st::settingsButtonNoIcon,
			.toggled = rpl::single(state->providers.showBotIcon),
			.keywords = { u"bot"_q, u"icon"_q, u"agent"_q },
		});
		if (botIcon) {
			botIcon->toggledChanges(
			) | rpl::filter([=](bool toggled) {
				return (toggled != state->providers.showBotIcon);
			}) | rpl::on_next([=](bool toggled) {
				state->providers.showBotIcon = toggled;
				SaveState(state.get());
			}, botIcon->lifetime());
		}
	}, std::move(shown));
	builder.addSkip();
	builder.addDividerText(tr::lng_agent_show_entry_points_about());
}

[[nodiscard]] QString SpendingTodayText(
		not_null<::Agent::SpendingTracker*> spending) {
	const auto totals = spending->today();
	if (totals.empty()) {
		return QString();
	}
	const auto value = totals.hasCost
		? ::Agent::Ui::FormatCost(totals.cost)
		: tr::lng_agent_spending_tokens(
			tr::now,
			lt_count_short,
			float64(totals.tokens()));
	return tr::lng_agent_spending_today_short(tr::now, lt_text, value);
}

void BuildMore(
		SectionBuilder &builder,
		not_null<Window::SessionController*> controller) {
	const auto spending = &controller->session().agent().spending();
	const auto exclusions = &controller->session().agent().exclusions();
	const auto count = [=] {
		const auto &list = exclusions->list();
		return QString::number(
			int(list.peers.size() + list.folders.size()));
	};
	const auto showOther = builder.showOther();
	builder.addSkip();
	builder.addButton({
		.id = u"agent/spending-limits"_q,
		.title = tr::lng_agent_spending_limits(),
		.icon = { &st::menuIconStats },
		.label = rpl::single(
			SpendingTodayText(spending)
		) | rpl::then(spending->changes() | rpl::map([=] {
			return SpendingTodayText(spending);
		})),
		.onClick = [=] {
			showOther(AgentSpending::Id());
		},
		.keywords = { u"spending"_q, u"cost"_q, u"limits"_q },
	});
	builder.addButton({
		.id = u"agent/hidden"_q,
		.title = tr::lng_agent_hidden_title(),
		.icon = { &st::menuIconStealth },
		.label = rpl::single(count())
			| rpl::then(exclusions->changes() | rpl::map(count)),
		.onClick = [=] {
			controller->show(Box(HiddenChatsBox, controller));
		},
		.keywords = { u"hidden"_q, u"chats"_q, u"exclude"_q },
	});
	builder.addButton({
		.id = u"agent/folder"_q,
		.title = tr::lng_agent_folder(),
		.icon = { &st::menuIconShowInFolder },
		.onClick = [=] {
			const auto path = ::Agent::AgentFolderPath(
				&controller->session());
			if (!path.isEmpty()) {
				QDesktopServices::openUrl(QUrl::fromLocalFile(path));
			}
		},
		.keywords = { u"folder"_q, u"path"_q, u"files"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_agent_settings_more_about(
		lt_path,
		rpl::single(::Agent::AgentFolderPath(&controller->session()))));
}


[[nodiscard]] QString DailyLimitText(
		const std::shared_ptr<AgentState> &state) {
	return tr::lng_agent_background_limit_value(
		tr::now,
		lt_cost,
		u"$"_q + QString::number(
			state->providers.backgroundDailyCost,
			'f',
			2));
}

void DailyLimitBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<AgentState> state) {
	box->setTitle(tr::lng_agent_background_limit_title());
	const auto container = box->verticalLayout();
	const auto cost = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_background_limit_cost(),
			QString::number(
				state->providers.backgroundDailyCost,
				'f',
				2)),
		st::boxRowPadding);
	const auto tokens = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_background_limit_tokens(),
			QString::number(state->providers.backgroundDailyTokens)),
		st::boxRowPadding);
	const auto save = [=] {
		auto changed = false;
		if (const auto value = ParsePrice(cost->getLastText());
				value && *value > 0.) {
			state->providers.backgroundDailyCost = *value;
			changed = true;
		}
		const auto tokensText = tokens->getLastText().trimmed();
		auto ok = false;
		const auto value = tokensText.toLongLong(&ok);
		if (ok && value > 0) {
			state->providers.backgroundDailyTokens = value;
			changed = true;
		}
		if (changed) {
			SaveState(state.get());
		}
		box->closeBox();
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void AddInfoRow(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		rpl::producer<QString> label,
		QStringList keywords) {
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::agentSettingsInfoButton,
		.label = std::move(label),
		.keywords = std::move(keywords),
	});
	if (button) {
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void BuildSpendingTotals(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	const auto spending = &controller->session().agent().spending();
	const auto totalsText = [](const ::Agent::SpendingTotals &totals) {
		return totals.empty()
			? tr::lng_agent_spending_none(tr::now)
			: ::Agent::Ui::FormatSpending(totals, false);
	};
	const auto label = [=](auto compute) {
		return rpl::single(totalsText(compute()))
			| rpl::then(spending->changes()
				| rpl::map([=] { return totalsText(compute()); }));
	};
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/spending"_q,
		.title = tr::lng_agent_spending_title(),
		.keywords = { u"spending"_q, u"cost"_q, u"tokens"_q },
	});
	AddInfoRow(
		builder,
		u"agent/spending/today"_q,
		tr::lng_agent_spending_today(),
		label([=] { return spending->today(); }),
		{ u"today"_q, u"spending"_q });
	AddInfoRow(
		builder,
		u"agent/spending/month"_q,
		tr::lng_agent_spending_month(),
		label([=] { return spending->lastDays(30); }),
		{ u"month"_q, u"30"_q, u"spending"_q });
	AddInfoRow(
		builder,
		u"agent/spending/all"_q,
		tr::lng_agent_spending_all(),
		label([=] { return spending->allTime(); }),
		{ u"all"_q, u"time"_q, u"spending"_q });
	const auto cacheText = [=] {
		const auto totals = spending->lastDays(30);
		return tr::lng_agent_spending_cache_value(
			tr::now,
			lt_percent,
			QString::number(::Agent::CachedPercent(totals)));
	};
	AddInfoRow(
		builder,
		u"agent/spending/cache"_q,
		tr::lng_agent_spending_cache(),
		rpl::single(cacheText()) | rpl::then(
			spending->changes() | rpl::map(cacheText)),
		{ u"cache"_q, u"prompt"_q, u"spending"_q });
	const auto button = builder.addButton({
		.id = u"agent/spending/show"_q,
		.title = tr::lng_agent_spending_show(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(state->providers.showSpending),
		.keywords = { u"spending"_q, u"sessions"_q, u"show"_q },
	});
	if (button) {
		button->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != state->providers.showSpending);
		}) | rpl::on_next([=](bool toggled) {
			state->providers.showSpending = toggled;
			SaveState(state.get());
		}, button->lifetime());
	}
	builder.addSkip();
	builder.addDividerText(tr::lng_agent_spending_about());
}

void BuildSubscriptionLimits(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state) {
	const auto subscription = ranges::any_of(
		state->providers.list,
		[](const ::Agent::ProviderSettings &provider) {
			return (provider.service
				== ::Agent::ProviderService::ClaudeSubscription);
		});
	const auto chatGpt = ranges::any_of(
		state->providers.list,
		[](const ::Agent::ProviderSettings &provider) {
			return ::Agent::UsesChatGptPlan(provider);
		});
	if (!subscription && !chatGpt) {
		return;
	}
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/limits"_q,
		.title = tr::lng_agent_limits_title(),
		.keywords = { u"limits"_q, u"subscription"_q, u"plan"_q },
	});
	if (subscription) {
		AddInfoRow(
			builder,
			u"agent/spending/claude/hours"_q,
			tr::lng_agent_claude_limit_five_hours(),
			::Agent::Ui::ClaudeLimitValue(false),
			{ u"claude"_q, u"limit"_q, u"subscription"_q });
		AddInfoRow(
			builder,
			u"agent/spending/claude/week"_q,
			tr::lng_agent_claude_limit_week(),
			::Agent::Ui::ClaudeLimitValue(true),
			{ u"claude"_q, u"limit"_q, u"week"_q });
	}
	if (chatGpt) {
		builder.addButton({
			.id = u"agent/spending/chatgpt"_q,
			.title = tr::lng_agent_spending_chatgpt(),
			.st = &st::settingsButtonNoIcon,
			.label = rpl::single(tr::lng_agent_chatgpt_usage(tr::now)),
			.onClick = [] {
				File::OpenUrl(::Agent::ChatGpt::UsageUrl());
			},
			.keywords = { u"chatgpt"_q, u"usage"_q, u"limit"_q },
		});
	}
	auto about = rpl::producer<QString>();
	if (subscription && chatGpt) {
		about = rpl::combine(
			tr::lng_agent_claude_limits_about(),
			tr::lng_agent_chatgpt_usage_about()
		) | rpl::map([](const QString &claude, const QString &chatGpt) {
			return claude + u"\n\n"_q + chatGpt;
		});
	} else if (subscription) {
		about = tr::lng_agent_claude_limits_about();
	} else {
		about = tr::lng_agent_chatgpt_usage_about();
	}
	builder.addSkip();
	builder.addDividerText(std::move(about));
}

void BuildBackgroundLimit(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/background"_q,
		.title = tr::lng_agent_background_title(),
		.keywords = { u"background"_q, u"tasks"_q, u"schedule"_q },
	});
	builder.addButton({
		.id = u"agent/background/limit"_q,
		.title = tr::lng_agent_background_limit(),
		.st = &st::settingsButtonNoIcon,
		.label = StateValue(state) | rpl::map([=] {
			return DailyLimitText(state);
		}),
		.onClick = [=] {
			controller->show(Box(DailyLimitBox, state));
		},
		.keywords = { u"limit"_q, u"spending"_q, u"budget"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_agent_background_limit_about());
}

void BuildSpendingReset(
		SectionBuilder &builder,
		not_null<Window::SessionController*> controller) {
	const auto spending = &controller->session().agent().spending();
	builder.addSkip();
	builder.addButton({
		.id = u"agent/spending/reset"_q,
		.title = tr::lng_agent_spending_reset(),
		.st = &st::settingsAttentionButton,
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_agent_spending_reset_sure(),
				.confirmed = [=](Fn<void()> close) {
					spending->reset();
					close();
				},
			}));
		},
		.keywords = { u"reset"_q, u"statistics"_q, u"spending"_q },
	});
	builder.addSkip();
}

[[nodiscard]] QString ExportCut(const QString &text, int limit) {
	return (text.size() <= limit)
		? text
		: (text.left(limit) + u" [... %1 more]"_q.arg(text.size() - limit));
}

[[nodiscard]] QString ExportDate(qint64 date) {
	return date
		? QDateTime::fromSecsSinceEpoch(date).toString(
			u"yyyy-MM-dd HH:mm:ss"_q)
		: u"-"_q;
}

[[nodiscard]] QString ExportPeer(
		not_null<Main::Session*> session,
		::Agent::PeerKey peer) {
	const auto data = session->data().peerLoaded(PeerId(peer));
	return u"\"%1\" (%2)"_q.arg(data ? data->name() : u"?"_q).arg(peer);
}

[[nodiscard]] QString ExportCardKind(::Agent::CardKind kind) {
	using Kind = ::Agent::CardKind;
	switch (kind) {
	case Kind::Send: return u"send"_q;
	case Kind::Draft: return u"draft"_q;
	case Kind::Forward: return u"forward"_q;
	case Kind::Edit: return u"edit"_q;
	case Kind::Delete: return u"delete"_q;
	case Kind::React: return u"react"_q;
	case Kind::PinMessage: return u"pin"_q;
	case Kind::UnpinMessage: return u"unpin"_q;
	case Kind::DeleteFolder: return u"delete_folder"_q;
	case Kind::Handoff: return u"handoff"_q;
	case Kind::Transcribe: return u"transcribe"_q;
	case Kind::Download: return u"download"_q;
	case Kind::SendFiles: return u"send_files"_q;
	case Kind::CreateTask: return u"create_task"_q;
	case Kind::UpdateTask: return u"update_task"_q;
	case Kind::Action: return u"action"_q;
	case Kind::Change: return u"change"_q;
	case Kind::Method: return u"method"_q;
	}
	return u"?"_q;
}

[[nodiscard]] QString ExportCardStatus(::Agent::CardStatus status) {
	using Status = ::Agent::CardStatus;
	switch (status) {
	case Status::Pending: return u"pending"_q;
	case Status::Running: return u"running"_q;
	case Status::Done: return u"done"_q;
	case Status::Drafted: return u"drafted"_q;
	case Status::Declined: return u"declined"_q;
	case Status::Failed: return u"failed"_q;
	case Status::NotDone: return u"not_done"_q;
	case Status::Expired: return u"expired"_q;
	}
	return u"?"_q;
}

[[nodiscard]] QString ExportTurnEnd(::Agent::TurnEnd end) {
	using End = ::Agent::TurnEnd;
	switch (end) {
	case End::Completed: return u"completed"_q;
	case End::Interrupted: return u"interrupted"_q;
	case End::StepLimit: return u"step_limit"_q;
	case End::MaxTokens: return u"max_tokens"_q;
	case End::Refusal: return u"refusal"_q;
	case End::Failed: return u"failed"_q;
	}
	return u"?"_q;
}

void ExportBlock(QStringList &lines, const ::Agent::Block &block) {
	v::match(block, [&](const ::Agent::TextBlock &data) {
		lines.push_back((data.hidden ? u"[hidden] "_q : u"[text] "_q)
			+ ExportCut(data.text, data.hidden ? 1500 : 4000));
	}, [&](const ::Agent::MediaBlock &data) {
		lines.push_back(u"[media] %1 %2 %3 bytes%4"_q
			.arg(data.attachment.isEmpty()
				? u"message %1"_q.arg(data.messageId)
				: data.attachment)
			.arg(data.mimeType)
			.arg(data.size)
			.arg(data.name.isEmpty() ? QString() : (u" "_q + data.name)));
	}, [&](const ::Agent::ThinkingBlock &data) {
		lines.push_back(u"[thinking %1 ms] "_q.arg(data.durationMs)
			+ ExportCut(data.text, 1500));
	}, [&](const ::Agent::ToolUseBlock &data) {
		lines.push_back(u"[call %1] %2 %3%4"_q
			.arg(data.id)
			.arg(data.name)
			.arg(ExportCut(QString::fromUtf8(QJsonDocument(
				data.input).toJson(QJsonDocument::Compact)), 2000))
			.arg(data.inputError.isEmpty()
				? QString()
				: (u" INPUT ERROR: "_q + data.inputError)));
	}, [&](const ::Agent::ToolResultBlock &data) {
		lines.push_back(u"[%1 %2] "_q
			.arg(data.error ? u"error"_q : u"result"_q)
			.arg(data.toolUseId)
			+ ExportCut(data.content, data.error ? 1500 : 800));
	});
}

void ExportRecord(
		QStringList &lines,
		not_null<Main::Session*> session,
		const ::Agent::ProvidersState &providers,
		const ::Agent::SessionRecord &record) {
	const auto &info = record.info;
	const auto provider = ::Agent::FindProvider(providers, info.profileId);
	lines.push_back(QString());
	lines.push_back(u"=== Session %1: %2"_q.arg(info.id, info.title));
	lines.push_back(u"scope: %1; model: %2; created: %3; updated: %4;"
		" tokens: %5 in, %6 cached, %7 out; tainted: %8; groups: %9"_q
		.arg(info.scope.peer
			? (u"chat "_q + ExportPeer(session, info.scope.peer))
			: u"general"_q)
		.arg(provider
			? ProviderRowTitle(*provider)
			: (info.profileId.isEmpty() ? u"default"_q : info.profileId))
		.arg(ExportDate(info.created))
		.arg(ExportDate(info.updated))
		.arg(info.usage.inputTokens)
		.arg(info.usage.cachedInputTokens)
		.arg(info.usage.outputTokens)
		.arg(record.tainted ? u"yes"_q : u"no"_q)
		.arg(QStringList(
			record.loadedGroups.begin(),
			record.loadedGroups.end()).join(u", "_q)));
	if (record.summaryUntil > 0) {
		lines.push_back(u"[summary of the first %1 messages] "_q.arg(
			record.summaryUntil) + ExportCut(record.summary, 4000));
	}
	for (auto i = 0; i != int(record.messages.size()); ++i) {
		const auto &message = record.messages[i];
		lines.push_back(u"--- %1 #%2 %3%4"_q
			.arg(message.role == ::Agent::Role::User
				? u"user"_q
				: u"assistant"_q)
			.arg(i)
			.arg(ExportDate(message.date))
			.arg(message.replyTo.isEmpty()
				? QString()
				: (u" reply to "_q + message.replyTo)));
		for (const auto &block : message.blocks) {
			ExportBlock(lines, block);
		}
		for (const auto &mark : record.turnEnds) {
			if (mark.after == i + 1) {
				lines.push_back(u"[turn end] "_q + ExportTurnEnd(mark.end));
			}
		}
	}
	for (const auto &card : record.cards) {
		auto targets = QStringList();
		for (const auto &target : card.targets) {
			targets.push_back((target.checked ? u"+"_q : u"-"_q)
				+ target.title
				+ (target.error.isEmpty()
					? QString()
					: (u" ("_q + target.error + ')')));
		}
		lines.push_back(u"[card %1] %2 %3%4%5 targets: %6; text: %7"_q
			.arg(card.toolUseId)
			.arg(ExportCardKind(card.kind))
			.arg(ExportCardStatus(card.status))
			.arg(card.autoApproved ? u" auto"_q : QString())
			.arg(card.allowedForTurn ? u" allowed_for_turn"_q : QString())
			.arg(targets.join(u", "_q))
			.arg(ExportCut(card.text, 600)));
	}
	if (!record.undone.empty()) {
		lines.push_back(u"[undone] "_q + QStringList(
			record.undone.begin(),
			record.undone.end()).join(u", "_q));
	}
}

[[nodiscard]] QString ExportSessions(
		not_null<Main::Session*> session,
		const ::Agent::ProvidersState &providers) {
	const auto store = ::Agent::SessionStore(session);
	auto &manager = session->agent();
	auto lines = QStringList();
	lines.push_back(u"AIgram agent sessions, exported %1"_q.arg(
		ExportDate(base::unixtime::now())));
	auto infos = manager.listAll();
	ranges::sort(infos, ranges::less(), &::Agent::SessionInfo::created);
	for (const auto &info : infos) {
		if (const auto record = store.readSession(info.id)) {
			ExportRecord(lines, session, providers, *record);
		} else {
			lines.push_back(u"=== Session %1: unreadable"_q.arg(info.id));
		}
	}
	for (const auto &task : manager.tasks().list()) {
		lines.push_back(QString());
		lines.push_back(u"##### Task %1: %2"_q.arg(task.id, task.title));
		lines.push_back(u"[instruction] "_q + task.instruction);
		auto runs = manager.tasks().runs(task.id);
		ranges::reverse(runs);
		for (const auto &run : runs) {
			if (const auto record = store.readSession(run.sessionId)) {
				ExportRecord(lines, session, providers, *record);
			}
		}
	}
	const auto path = ::Agent::UniqueAgentFilePath(
		session,
		u"agent-sessions-%1.txt"_q.arg(QDateTime::currentDateTime().toString(
			u"yyyy-MM-dd_HH-mm"_q)));
	auto file = QFile(path);
	if (path.isEmpty()
		|| !file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| file.write(lines.join('\n').toUtf8()) < 0) {
		return QString();
	}
	return path;
}

void BuildClearData(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSkip();
	builder.addButton({
		.id = u"agent/export-sessions"_q,
		.title = tr::lng_agent_export_sessions(),
		.icon = { &st::menuIconExport },
		.onClick = [=] {
			const auto path = ExportSessions(
				&controller->session(),
				state->providers);
			if (path.isEmpty()) {
				controller->showToast(
					tr::lng_agent_export_sessions_failed(tr::now));
				return;
			}
			controller->showToast(
				tr::lng_agent_export_sessions_done(tr::now));
			File::ShowInFolder(path);
		},
		.keywords = { u"export"_q, u"debug"_q, u"sessions"_q },
	});
	builder.addButton({
		.id = u"agent/clear-data"_q,
		.title = tr::lng_agent_clear_data(),
		.st = &st::settingsAttentionButtonWithIcon,
		.icon = { &st::menuIconDeleteAttention },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_agent_clear_data_sure(),
				.confirmed = [=](Fn<void()> close) {
					controller->session().agent().clearAllData();
					close();
					controller->showToast(
						tr::lng_agent_clear_data_done(tr::now));
				},
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		},
		.keywords = { u"delete"_q, u"data"_q, u"agent"_q },
	});
	builder.addSkip();
}

[[nodiscard]] std::shared_ptr<AgentState> MakeState(rpl::lifetime &lifetime) {
	const auto state = std::make_shared<AgentState>();
	state->providers = ::Agent::LoadProviders();
	::Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		state->providers = ::Agent::LoadProviders();
		state->changed.fire({});
	}, lifetime);
	return state;
}

[[nodiscard]] SectionBuildMethod MakeBuildMethod(
		std::shared_ptr<AgentState> state,
		Fn<void(
			SectionBuilder &builder,
			const std::shared_ptr<AgentState> &state,
			not_null<Window::SessionController*> controller)> fill) {
	return [=](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		const auto highlights
			= container->lifetime().make_state<HighlightRegistry>();
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.highlights = highlights,
		});
		fill(builder, state, controller);
		std::move(showFinished) | rpl::on_next([=] {
			for (const auto &[id, entry] : *highlights) {
				if (entry.widget) {
					controller->checkHighlightControl(
						id,
						entry.widget,
						base::duplicate(entry.args));
				}
			}
		}, container->lifetime());
	};
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
	const auto state = MakeState(lifetime());
	build(content, MakeBuildMethod(state, [](
			SectionBuilder &builder,
			const std::shared_ptr<AgentState> &state,
			not_null<Window::SessionController*> controller) {
		BuildCover(builder);
		BuildModels(builder, state, controller);
		BuildRoles(builder, state, controller);
		BuildAgentBlock(builder, state, controller);
		BuildChatList(builder, state, controller);
		BuildMore(builder, controller);
		BuildClearData(builder, state, controller);
	}));
	Ui::ResizeFitChild(this, content);
}

AgentSpending::AgentSpending(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> AgentSpending::title() {
	return tr::lng_agent_spending_limits_title();
}

void AgentSpending::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto state = MakeState(lifetime());
	build(content, MakeBuildMethod(state, [](
			SectionBuilder &builder,
			const std::shared_ptr<AgentState> &state,
			not_null<Window::SessionController*> controller) {
		BuildSpendingTotals(builder, state, controller);
		BuildSubscriptionLimits(builder, state);
		BuildBackgroundLimit(builder, state, controller);
		BuildSpendingReset(builder, controller);
	}));
	Ui::ResizeFitChild(this, content);
}

void AddSearchEntry(
		SectionBuilder &builder,
		QString id,
		QString title,
		QStringList keywords) {
	builder.add(nullptr, [=] {
		return SearchEntry{
			.id = id,
			.title = title,
			.keywords = keywords,
		};
	});
}

const auto kMeta = BuildHelper({
	.id = Agent::Id(),
	.parentId = MainId(),
	.title = &tr::lng_agent_title,
	.icon = &st::menuIconBot,
}, [](SectionBuilder &builder) {
	AddSearchEntry(
		builder,
		u"agent/profile"_q,
		tr::lng_agent_cover_edit(tr::now),
		{ u"name"_q, u"photo"_q, u"avatar"_q });
	AddSearchEntry(
		builder,
		u"agent/providers"_q,
		tr::lng_agent_providers_title(tr::now),
		{ u"providers"_q, u"api"_q, u"models"_q });
	AddSearchEntry(
		builder,
		u"agent/roles"_q,
		tr::lng_agent_roles_title(tr::now),
		{ u"default"_q, u"background"_q, u"media"_q, u"model"_q });
	AddSearchEntry(
		builder,
		u"agent/instructions"_q,
		tr::lng_agent_instructions_title(tr::now),
		{ u"instructions"_q, u"prompt"_q });
	AddSearchEntry(
		builder,
		u"agent/memory"_q,
		tr::lng_agent_memory_tab(tr::now),
		{ u"memory"_q, u"facts"_q });
	AddSearchEntry(
		builder,
		u"agent/chat-list"_q,
		tr::lng_agent_show_entry_points(tr::now),
		{ u"chats"_q, u"row"_q, u"bot"_q });
	AddSearchEntry(
		builder,
		u"agent/hidden"_q,
		tr::lng_agent_hidden_title(tr::now),
		{ u"hidden"_q, u"chats"_q, u"exclude"_q });
	AddSearchEntry(
		builder,
		u"agent/clear-data"_q,
		tr::lng_agent_clear_data(tr::now),
		{ u"delete"_q, u"data"_q, u"agent"_q });
});

const auto kSpendingMeta = BuildHelper({
	.id = AgentSpending::Id(),
	.parentId = Agent::Id(),
	.title = &tr::lng_agent_spending_limits_title,
	.icon = &st::menuIconStats,
}, [](SectionBuilder &builder) {
	AddSearchEntry(
		builder,
		u"agent/spending"_q,
		tr::lng_agent_spending_title(tr::now),
		{ u"spending"_q, u"cost"_q, u"tokens"_q });
	AddSearchEntry(
		builder,
		u"agent/limits"_q,
		tr::lng_agent_limits_title(tr::now),
		{ u"limits"_q, u"subscription"_q, u"claude"_q, u"chatgpt"_q });
	AddSearchEntry(
		builder,
		u"agent/background/limit"_q,
		tr::lng_agent_background_limit(tr::now),
		{ u"limit"_q, u"budget"_q, u"background"_q });
});

} // namespace

Type AgentId() {
	return Agent::Id();
}

} // namespace Settings
