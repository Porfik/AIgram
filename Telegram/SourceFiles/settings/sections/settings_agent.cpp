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
#include "agent/agent_settings.h"
#include "agent/agent_spending_tracker.h"
#include "agent/ui/agent_claude_boxes.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_profile.h"
#include "agent/ui/agent_spending_format.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
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
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <rpl/map.h>
#include <rpl/then.h>

#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

#include <memory>
#include <optional>

#include "styles/style_agent_general.h"
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

class Agent final : public Section<Agent> {
public:
	Agent(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

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
		const auto service = provider.service;
		const auto &icon = (service == ::Agent::ProviderService::OpenRouter)
			? st::agentModelChoiceOpenRouterIcon
			: (service == ::Agent::ProviderService::Anthropic
				|| service == ::Agent::ProviderService::ClaudeSubscription)
			? st::agentModelChoiceAnthropicIcon
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
			p.setPen(st.statusFg);
			about->draw(p, {
				.position = { textLeft, st.statusPosition.y() },
				.outerWidth = row->width(),
				.availableWidth = textWidth,
				.elisionLines = 1,
			});
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

void BuildProviders(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSubsectionTitle({
		.id = u"agent/providers"_q,
		.title = tr::lng_agent_providers_title(),
		.keywords = { u"models"_q, u"providers"_q, u"api"_q },
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
	builder.addButton({
		.id = u"agent/default-model"_q,
		.title = tr::lng_agent_default_model(),
		.label = rpl::single(rpl::empty) | rpl::then(
			state->changed.events()
		) | rpl::map([=] {
			return DefaultModelText(state);
		}),
		.onClick = [=] {
			if (!state->providers.list.empty()) {
				controller->show(Box(DefaultModelBox, state));
			}
		},
		.keywords = { u"default"_q, u"model"_q },
	});
	builder.addDividerText(tr::lng_agent_default_model_about());
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
	builder.addDividerText(tr::lng_agent_instructions_about());
}

[[nodiscard]] QString RowPreviewText(::Agent::RowPreview mode) {
	switch (mode) {
	case ::Agent::RowPreview::LastMessage:
		return tr::lng_agent_row_preview_message(tr::now);
	case ::Agent::RowPreview::SessionTitle:
		return tr::lng_agent_row_preview_session(tr::now);
	case ::Agent::RowPreview::Hint:
		return tr::lng_agent_row_preview_hint(tr::now);
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

void BuildMedia(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"agent/media"_q,
		.title = tr::lng_agent_media_title(),
		.keywords = { u"media"_q, u"model"_q },
	});
	builder.addButton({
		.id = u"agent/media/model"_q,
		.title = tr::lng_agent_media_model(),
		.label = rpl::single(rpl::empty) | rpl::then(
			state->changed.events()
		) | rpl::map([=] {
			return MediaModelText(state);
		}),
		.onClick = [=] {
			controller->show(Box(MediaModelBox, state));
		},
		.keywords = { u"media"_q, u"model"_q },
	});
	builder.addDividerText(tr::lng_agent_media_model_about());

	builder.addSkip();
	builder.addButton({
		.id = u"agent/folder"_q,
		.title = tr::lng_agent_folder(),
		.onClick = [=] {
			const auto path = ::Agent::AgentFolderPath(
				&controller->session());
			if (!path.isEmpty()) {
				QDesktopServices::openUrl(QUrl::fromLocalFile(path));
			}
		},
		.keywords = { u"folder"_q, u"path"_q },
	});
	builder.addDividerText(tr::lng_agent_folder_about(
		lt_path,
		rpl::single(::Agent::AgentFolderPath(&controller->session()))));
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

[[nodiscard]] QString DailyLimitText(
		const std::shared_ptr<AgentState> &state) {
	return tr::lng_agent_background_limit_value(
		tr::now,
		lt_cost,
		u"$"_q + QString::number(
			state->providers.backgroundDailyCost,
			'f',
			2),
		lt_amount,
		Lang::FormatCountDecimal(
			state->providers.backgroundDailyTokens));
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

void BuildBackground(
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
		.id = u"agent/background/model"_q,
		.title = tr::lng_agent_background_model(),
		.label = rpl::single(rpl::empty) | rpl::then(
			state->changed.events()
		) | rpl::map([=] {
			return BackgroundModelText(state);
		}),
		.onClick = [=] {
			controller->show(Box(BackgroundModelBox, state));
		},
		.keywords = { u"background"_q, u"model"_q },
	});
	builder.addDividerText(tr::lng_agent_background_model_about());

	builder.addSkip();
	builder.addButton({
		.id = u"agent/background/limit"_q,
		.title = tr::lng_agent_background_limit(),
		.label = rpl::single(rpl::empty) | rpl::then(
			state->changed.events()
		) | rpl::map([=] {
			return DailyLimitText(state);
		}),
		.onClick = [=] {
			controller->show(Box(DailyLimitBox, state));
		},
		.keywords = { u"limit"_q, u"spending"_q, u"budget"_q },
	});
	builder.addDividerText(tr::lng_agent_background_limit_about());
}

void BuildEntryPoints(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSkip();
	builder.addButton({
		.id = u"agent/profile"_q,
		.title = tr::lng_agent_profile_button(),
		.label = rpl::single(rpl::empty) | rpl::then(
			state->changed.events()
		) | rpl::map([=] {
			return ::Agent::AgentName(state->providers.agentName);
		}),
		.onClick = [=] {
			::Agent::Ui::ShowEditAgentProfileBox(controller);
		},
		.keywords = { u"name"_q, u"photo"_q, u"avatar"_q, u"agent"_q },
	});
	const auto button = builder.addButton({
		.id = u"agent/show-entry-points"_q,
		.title = tr::lng_agent_show_entry_points(),
		.toggled = rpl::single(state->providers.showEntryPoints),
		.keywords = { u"show"_q, u"agent"_q, u"chats"_q },
	});
	if (button) {
		button->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != state->providers.showEntryPoints);
		}) | rpl::on_next([=](bool toggled) {
			state->providers.showEntryPoints = toggled;
			SaveState(state.get());
		}, button->lifetime());
	}
	builder.addButton({
		.id = u"agent/row-preview"_q,
		.title = tr::lng_agent_row_preview(),
		.label = rpl::single(rpl::empty) | rpl::then(
			state->changed.events()
		) | rpl::map([=] {
			return RowPreviewText(state->providers.rowPreview);
		}),
		.onClick = [=] {
			controller->show(Box(RowPreviewBox, state));
		},
		.keywords = { u"row"_q, u"preview"_q, u"agent"_q },
	});
	builder.add([=](const WidgetContext &ctx) {
		Ui::AddDividerText(
			ctx.container,
			tr::lng_agent_show_entry_points_about());
		return SectionBuilder::WidgetToAdd{};
	}, nullptr);
}

void BuildSpending(
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
	builder.addButton({
		.id = u"agent/spending/today"_q,
		.title = tr::lng_agent_spending_today(),
		.label = label([=] { return spending->today(); }),
		.keywords = { u"today"_q, u"spending"_q },
	});
	builder.addButton({
		.id = u"agent/spending/month"_q,
		.title = tr::lng_agent_spending_month(),
		.label = label([=] { return spending->lastDays(30); }),
		.keywords = { u"month"_q, u"30"_q, u"spending"_q },
	});
	builder.addButton({
		.id = u"agent/spending/all"_q,
		.title = tr::lng_agent_spending_all(),
		.label = label([=] { return spending->allTime(); }),
		.keywords = { u"all"_q, u"time"_q, u"spending"_q },
	});
	const auto cacheText = [=] {
		const auto totals = spending->lastDays(30);
		return tr::lng_agent_spending_cache_value(
			tr::now,
			lt_percent,
			QString::number(::Agent::CachedPercent(totals)));
	};
	builder.addButton({
		.id = u"agent/spending/cache"_q,
		.title = tr::lng_agent_spending_cache(),
		.label = rpl::single(cacheText()) | rpl::then(
			spending->changes() | rpl::map(cacheText)),
		.keywords = { u"cache"_q, u"prompt"_q, u"spending"_q },
	});
	const auto subscription = ranges::any_of(
		state->providers.list,
		[](const ::Agent::ProviderSettings &provider) {
			return (provider.service
				== ::Agent::ProviderService::ClaudeSubscription);
		});
	if (subscription) {
		builder.addButton({
			.id = u"agent/spending/claude/hours"_q,
			.title = tr::lng_agent_claude_limit_five_hours(),
			.label = ::Agent::Ui::ClaudeLimitValue(false),
			.keywords = { u"claude"_q, u"limit"_q, u"subscription"_q },
		});
		builder.addButton({
			.id = u"agent/spending/claude/week"_q,
			.title = tr::lng_agent_claude_limit_week(),
			.label = ::Agent::Ui::ClaudeLimitValue(true),
			.keywords = { u"claude"_q, u"limit"_q, u"week"_q },
		});
	}
	builder.addButton({
		.id = u"agent/spending/reset"_q,
		.title = tr::lng_agent_spending_reset(),
		.icon = { &st::menuIconClear },
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
	const auto button = builder.addButton({
		.id = u"agent/spending/show"_q,
		.title = tr::lng_agent_spending_show(),
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
	builder.addDividerText(tr::lng_agent_spending_about());
}

void BuildHiddenChats(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	const auto exclusions
		= &controller->session().agent().exclusions();
	const auto count = [=] {
		const auto &list = exclusions->list();
		return QString::number(
			int(list.peers.size() + list.folders.size()));
	};
	builder.addSkip();
	builder.addButton({
		.id = u"agent/hidden"_q,
		.title = tr::lng_agent_hidden_title(),
		.label = rpl::single(count())
			| rpl::then(exclusions->changes() | rpl::map(count)),
		.onClick = [=] {
			controller->show(Box(HiddenChatsBox, controller));
		},
		.keywords = { u"hidden"_q, u"chats"_q, u"exclude"_q },
	});
	builder.addDividerText(tr::lng_agent_hidden_about());
}

void BuildClearData(
		SectionBuilder &builder,
		const std::shared_ptr<AgentState> &state,
		not_null<Window::SessionController*> controller) {
	builder.addSkip();
	builder.addButton({
		.id = u"agent/clear-data"_q,
		.title = tr::lng_agent_clear_data(),
		.st = &st::settingsAttentionButton,
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
	::Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		state->providers = ::Agent::LoadProviders();
		state->changed.fire({});
	}, lifetime());

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
		BuildEntryPoints(builder, state, controller);
		BuildMedia(builder, state, controller);
		BuildBackground(builder, state, controller);
		BuildSpending(builder, state, controller);
		BuildHiddenChats(builder, state, controller);
		BuildClearData(builder, state, controller);
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
			.id = u"agent/spending"_q,
			.title = tr::lng_agent_spending_title(tr::now),
			.keywords = { u"spending"_q, u"cost"_q, u"tokens"_q },
		};
	});

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"agent/hidden"_q,
			.title = tr::lng_agent_hidden_title(tr::now),
			.keywords = { u"hidden"_q, u"chats"_q, u"exclude"_q },
		};
	});

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"agent/clear-data"_q,
			.title = tr::lng_agent_clear_data(tr::now),
			.keywords = { u"delete"_q, u"data"_q, u"agent"_q },
		};
	});
});

} // namespace

Type AgentId() {
	return Agent::Id();
}

} // namespace Settings
