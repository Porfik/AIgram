/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_openai_boxes.h"

#include "agent/agent_check.h"
#include "agent/agent_chatgpt.h"
#include "agent/agent_manager.h"
#include "agent/agent_settings.h"
#include "agent/core/agent_chatgpt_auth.h"
#include "agent/core/agent_openai_responses.h"
#include "agent/core/agent_transport.h"
#include "agent/ui/agent_claude_boxes.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_model_boxes_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <rpl/map.h>
#include <rpl/then.h>

#include <QtCore/QPointer>

#include "styles/style_agent_general.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Agent::Ui {
namespace {

using namespace details;
using OpenAi::ModelInfo;

void StartChatGptSubscription(
	not_null<Window::SessionController*> controller,
	const Fn<void(QString id)> &added);
void StartOpenAiKey(
	not_null<Window::SessionController*> controller,
	const Fn<void(QString id)> &added);

[[nodiscard]] base::flat_map<QString, std::vector<QString>> &EffortLevelsCache() {
	static auto result = base::flat_map<QString, std::vector<QString>>();
	return result;
}

[[nodiscard]] std::vector<QString> EffortChoices(const QString &model) {
	const auto &cache = EffortLevelsCache();
	const auto i = cache.find(model);
	if (i != cache.end() && !i->second.empty()) {
		return i->second;
	}
	const auto known = OpenAi::FindKnownModel(model);
	if (known && !known->effortLevels.empty()) {
		return known->effortLevels;
	}
	return OpenAi::ResponsesEffortLevels();
}

void RememberEffortLevels(const std::vector<ModelInfo> &models) {
	for (const auto &model : models) {
		if (!model.effortLevels.empty()) {
			EffortLevelsCache()[model.id] = model.effortLevels;
		}
	}
}

[[nodiscard]] ProviderSettings ChatGptPlanSettings() {
	auto result = ProviderSettings();
	result.kind = ProviderKind::OpenAiResponses;
	result.service = ProviderService::ChatGptSubscription;
	result.baseUrl = OpenAiBaseUrl();
	return result;
}

[[nodiscard]] ProviderSettings OpenAiKeySettings(const QString &key) {
	auto result = ProviderSettings();
	result.kind = ProviderKind::OpenAiResponses;
	result.service = ProviderService::OpenAi;
	result.baseUrl = OpenAiBaseUrl();
	result.apiKey = key;
	return result;
}

[[nodiscard]] Error ParseResponsesError(
		const HttpHead &head,
		const QByteArray &body) {
	return OpenAi::ParseResponsesError(head, body);
}

void RememberParsedModels(
		Fn<void(std::vector<ModelInfo>, QString)> done,
		FetchResult result) {
	if (!result.error.isEmpty() || result.status != 200) {
		done({}, FetchErrorText(result, ParseResponsesError));
		return;
	}
	auto models = OpenAi::ParseResponsesModels(result.body);
	if (models.empty()) {
		done({}, FetchErrorText(result, ParseResponsesError));
		return;
	}
	RememberEffortLevels(models);
	done(std::move(models), QString());
}

[[nodiscard]] ModelSource ChatGptSource(not_null<Manager*> manager) {
	auto result = ModelSource();
	const auto settings = ChatGptPlanSettings();
	result.load = [=](
			not_null<::Ui::GenericBox*> box,
			Fn<void(std::vector<ModelInfo>, QString)> done) {
		Fetch(
			manager,
			settings,
			box->lifetime(),
			OpenAi::ResponsesModelsRequest(settings),
			[=](FetchResult result) {
				RememberParsedModels(done, std::move(result));
			});
	};
	result.describe = [](const ModelInfo &model) {
		auto parts = QStringList();
		if (model.contextWindow > 0) {
			parts.push_back(tr::lng_agent_model_context(
				tr::now,
				lt_amount,
				FormatContext(model.contextWindow)));
		}
		parts.push_back(model.id);
		return parts.join(u" · "_q);
	};
	return result;
}

[[nodiscard]] ModelSource OpenAiKeySource(
		not_null<Manager*> manager,
		QString key) {
	auto result = ModelSource();
	const auto settings = OpenAiKeySettings(key);
	result.load = [=](
			not_null<::Ui::GenericBox*> box,
			Fn<void(std::vector<ModelInfo>, QString)> done) {
		Fetch(
			manager,
			settings,
			box->lifetime(),
			OpenAi::ResponsesModelsRequest(settings),
			[=](FetchResult result) {
				RememberParsedModels(done, std::move(result));
			});
	};
	result.describe = [](const ModelInfo &model) {
		return ModelInfoDescription(model);
	};
	return result;
}

void OpenAiEffortBox(
		not_null<::Ui::GenericBox*> box,
		QString model,
		QString current,
		Fn<void(QString)> chosen) {
	box->setTitle(tr::lng_agent_effort());
	auto values = std::vector<QString>{ QString() };
	for (const auto &level : EffortChoices(model)) {
		values.push_back(level);
	}
	auto index = 0;
	for (auto i = 0; i != int(values.size()); ++i) {
		if (values[i] == current) {
			index = i;
		}
	}
	const auto group = std::make_shared<::Ui::RadioenumGroup<int>>(index);
	for (auto i = 0; i != int(values.size()); ++i) {
		box->addRow(
			object_ptr<::Ui::Radioenum<int>>(
				box,
				group,
				i,
				EffortText(values[i]),
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	box->addRow(
		object_ptr<::Ui::FlatLabel>(
			box,
			tr::lng_agent_effort_about_openai(),
			st::boxDividerLabel),
		st::boxRowPadding);
	group->setChangedCallback([=](int index) {
		chosen(values[index]);
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

[[nodiscard]] QString PlanNameText(const QString &plan) {
	auto result = plan.trimmed();
	if (!result.isEmpty()) {
		result[0] = result[0].toUpper();
	}
	return result;
}

[[nodiscard]] QString AccountText(const ChatGptAccount &account) {
	const auto plan = PlanNameText(account.plan);
	return plan.isEmpty()
		? account.email
		: (account.email + u" · "_q + plan);
}

void OpenAiChoiceBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	box->setTitle(tr::lng_agent_openai_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_openai_subscription(),
		st::settingsButton,
		{ &st::agentModelChoiceSubscriptionIcon }
	)->setClickedCallback([=] {
		box->closeBox();
		StartChatGptSubscription(controller, added);
	});
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_openai_api_key(),
		st::settingsButton,
		{ &st::agentModelChoiceKeyIcon }
	)->setClickedCallback([=] {
		box->closeBox();
		StartOpenAiKey(controller, added);
	});
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_openai_about());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

struct SetupState {
	std::unique_ptr<ChatGptLogin> login;
};

void ChatGptSetupBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void()> ready) {
	box->setTitle(tr::lng_agent_chatgpt_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto label = container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_chatgpt_about(),
			st::boxLabel),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	const auto paste = container->add(
		object_ptr<::Ui::SlideWrap<::Ui::VerticalLayout>>(
			container,
			object_ptr<::Ui::VerticalLayout>(container),
			st::boxRowPadding));
	const auto inner = paste->entity();
	const auto pasteField = inner->add(
		object_ptr<::Ui::InputField>(
			inner,
			st::defaultInputField,
			tr::lng_agent_chatgpt_paste_placeholder()),
		st::boxRowPadding);
	::Ui::AddSkip(inner);
	const auto pasteAbout = inner->add(
		object_ptr<::Ui::FlatLabel>(inner, st::boxDividerLabel),
		st::boxRowPadding);
	paste->toggle(false, anim::type::instant);
	::Ui::AddSkip(container);

	const auto state = box->lifetime().make_state<SetupState>();
	const auto start = std::make_shared<Fn<void()>>();
	const auto continueWrap = container->add(
		object_ptr<::Ui::SlideWrap<::Ui::VerticalLayout>>(
			container,
			object_ptr<::Ui::VerticalLayout>(container)));
	const auto continueInner = continueWrap->entity();
	Settings::AddButtonWithIcon(
		continueInner,
		tr::lng_agent_chatgpt_continue(),
		st::agentChatGptButton,
		{ &st::agentChatGptButtonIcon }
	)->setClickedCallback([=] {
		(*start)();
	});
	continueWrap->toggle(true, anim::type::instant);
	const auto failed = [=](QString error) {
		state->login = nullptr;
		paste->toggle(false, anim::type::instant);
		label->setText(error);
		continueWrap->toggle(true, anim::type::instant);
		box->clearButtons();
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	const auto pasteSubmitted = [=] {
		if (!state->login) {
			return;
		}
		const auto error = state->login->paste(
			pasteField->getLastText().trimmed());
		if (!error.isEmpty()) {
			pasteField->showError();
			box->uiShow()->showToast(
				tr::lng_agent_chatgpt_paste_wrong(tr::now));
		}
	};
	const auto showWaiting = [=] {
		label->setText(tr::lng_agent_chatgpt_waiting(tr::now));
		pasteAbout->setText((state->login && state->login->listening())
			? tr::lng_agent_chatgpt_paste_about(tr::now)
			: tr::lng_agent_chatgpt_paste_busy(tr::now));
		pasteField->setText(u""_q);
		paste->toggle(true, anim::type::instant);
		continueWrap->toggle(false, anim::type::instant);
		box->clearButtons();
		box->addLeftButton(tr::lng_agent_chatgpt_open_again(), [=] {
			if (state->login) {
				state->login->openBrowser();
			}
		});
		box->addButton(tr::lng_intro_next(), pasteSubmitted);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	*start = [=] {
		state->login = std::make_unique<ChatGptLogin>(crl::guard(box,
			[=](QString error) {
				if (error.isEmpty()) {
					ready();
					box->closeBox();
				} else {
					failed(std::move(error));
				}
			}));
		showWaiting();
	};
	pasteField->submits(
	) | rpl::on_next(pasteSubmitted, pasteField->lifetime());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ChatGptNameBox(
		not_null<::Ui::GenericBox*> box,
		ModelInfo model,
		Fn<void(QString id)> added) {
	box->setTitle(tr::lng_agent_model_name_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto name = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			model.name.isEmpty() ? model.id : model.name),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_model_name_about());
	::Ui::AddSkip(container);
	const auto state = LoadProviders();
	const auto makeDefault = container->add(
		object_ptr<::Ui::Checkbox>(
			container,
			tr::lng_agent_provider_make_active(),
			!FindProvider(state, state.defaultId),
			st::defaultBoxCheckbox),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	const auto save = [=] {
		const auto title = name->getLastText().trimmed();
		if (title.isEmpty()) {
			name->showError();
			return;
		}
		auto provider = ChatGptPlanSettings();
		provider.id = GenerateProviderId();
		provider.title = title;
		provider.model = model.id;
		provider.contextWindow = model.contextWindow;
		provider.vision = model.vision.value_or(false);
		provider.files = model.files.value_or(false);
		const auto checked = makeDefault->checked();
		UpdateProviders([&](ProvidersState &state) {
			state.list.push_back(provider);
			ApplyDefault(state, provider.id, checked);
		});
		const auto id = provider.id;
		box->closeBox();
		if (added) {
			added(id);
		}
	};
	name->submits(
	) | rpl::on_next(save, name->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		name->setFocusFast();
		name->selectAll();
	});
}

void OpenAiKeyBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString key)> next) {
	box->setTitle(tr::lng_agent_openai_key_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_openai_key_about(
				lt_link,
				rpl::single(tr::link(
					u"platform.openai.com/api-keys"_q,
					u"https://platform.openai.com/api-keys"_q)),
				tr::marked),
			st::boxLabel),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	const auto key = AddKeyField(container, QString());
	::Ui::AddSkip(container);
	const auto manager = &controller->session().agent();
	const auto checking = box->lifetime().make_state<bool>(false);
	const auto submit = [=] {
		const auto value = key->getLastText().trimmed();
		if (*checking) {
			return;
		} else if (value.isEmpty()) {
			key->showError();
			return;
		}
		*checking = true;
		box->uiShow()->showToast(
			tr::lng_agent_openrouter_key_checking(tr::now));
		Fetch(
			manager,
			OpenAiKeySettings(value),
			box->lifetime(),
			OpenAi::ResponsesModelsRequest(OpenAiKeySettings(value)),
			[=](FetchResult result) {
				*checking = false;
				if (result.error.isEmpty() && result.status == 200) {
					next(value);
					box->closeBox();
				} else if (result.error.isEmpty()
					&& (result.status == 401 || result.status == 403)) {
					key->showError();
					box->uiShow()->showToast(
						tr::lng_agent_openai_key_wrong(tr::now));
				} else {
					box->uiShow()->showToast(
						tr::lng_agent_openrouter_key_failed(
							tr::now,
							lt_text,
							FetchErrorText(result, ParseResponsesError)));
				}
			});
	};
	QObject::connect(key, &::Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_intro_next(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		key->setFocusFast();
	});
}

void OpenAiNameBox(
		not_null<::Ui::GenericBox*> box,
		QString key,
		ModelInfo model,
		Fn<void(QString id)> added) {
	box->setTitle(tr::lng_agent_model_name_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto name = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			model.name.isEmpty() ? model.id : model.name),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_model_name_about());
	::Ui::AddSkip(container);
	const auto state = LoadProviders();
	const auto makeDefault = container->add(
		object_ptr<::Ui::Checkbox>(
			container,
			tr::lng_agent_provider_make_active(),
			!FindProvider(state, state.defaultId),
			st::defaultBoxCheckbox),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	const auto save = [=] {
		const auto title = name->getLastText().trimmed();
		if (title.isEmpty()) {
			name->showError();
			return;
		}
		auto provider = OpenAiKeySettings(key);
		provider.id = GenerateProviderId();
		provider.title = title;
		ApplyModelInfo(provider, model);
		const auto checked = makeDefault->checked();
		UpdateProviders([&](ProvidersState &state) {
			state.list.push_back(provider);
			SetServiceKey(state, ProviderService::OpenAi, key);
			ApplyDefault(state, provider.id, checked);
		});
		const auto id = provider.id;
		box->closeBox();
		if (added) {
			added(id);
		}
	};
	name->submits(
	) | rpl::on_next(save, name->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		name->setFocusFast();
		name->selectAll();
	});
}

void StartChatGptSubscription(
		not_null<Window::SessionController*> controller,
		const Fn<void(QString id)> &added) {
	const auto manager = &controller->session().agent();
	const auto pickModel = [=] {
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				QString(),
				ChatGptSource(manager),
				[=](ModelInfo model) {
					controller->show(Box(ChatGptNameBox, model, added));
				});
		}));
	};
	if (ChatGptSignedIn()) {
		pickModel();
	} else {
		controller->show(Box(ChatGptSetupBox, controller, pickModel));
	}
}

void StartOpenAiKey(
		not_null<Window::SessionController*> controller,
		const Fn<void(QString id)> &added) {
	const auto manager = &controller->session().agent();
	const auto pickModel = [=](QString key) {
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				QString(),
				OpenAiKeySource(manager, key),
				[=](ModelInfo model) {
					controller->show(Box(OpenAiNameBox, key, model, added));
				});
		}));
	};
	const auto key = ServiceKey(
		LoadProviders(),
		ProviderService::OpenAi);
	if (key.isEmpty()) {
		controller->show(Box(OpenAiKeyBox, controller, pickModel));
	} else {
		pickModel(key);
	}
}

struct EditState {
	ProviderSettings current;
	rpl::event_stream<> changed;
};

struct PriceFields {
	::Ui::InputField *input = nullptr;
	::Ui::InputField *output = nullptr;
	::Ui::InputField *cached = nullptr;
};

[[nodiscard]] rpl::producer<QString> EditValue(
		not_null<EditState*> state,
		Fn<QString()> compute) {
	return rpl::single(rpl::empty) | rpl::then(
		state->changed.events()
	) | rpl::map(compute);
}

not_null<::Ui::Checkbox*> AddDefaultCheckbox(
		not_null<::Ui::VerticalLayout*> container,
		const QString &id) {
	return container->add(
		object_ptr<::Ui::Checkbox>(
			container,
			tr::lng_agent_provider_make_active(),
			(id == LoadProviders().defaultId),
			st::defaultBoxCheckbox),
		st::boxRowPadding);
}

void AddEffortRow(
		not_null<::Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<EditState*> state) {
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_effort(),
		EditValue(state, [=] { return EffortText(state->current.effort); }),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		controller->show(Box(
			OpenAiEffortBox,
			state->current.model,
			state->current.effort,
			[=](QString effort) {
				state->current.effort = effort;
				state->changed.fire({});
			}));
	});
}

void AddModelRow(
		not_null<::Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<EditState*> state,
		Fn<ModelSource()> makeSource,
		Fn<void(const ModelInfo &model)> apply) {
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_provider_model(),
		EditValue(state, [=] { return state->current.model; }),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				state->current.model,
				makeSource(),
				[=](ModelInfo model) {
					apply(model);
					if (!ranges::contains(
							EffortChoices(model.id),
							state->current.effort)) {
						state->current.effort = QString();
					}
					state->changed.fire({});
				});
		}));
	});
}

void AddCheckRow(
		not_null<::Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Fn<ProviderSettings()> collect) {
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_check_button(),
		st::settingsButtonLightNoIcon
	)->setClickedCallback([=] {
		ShowCheckBox(controller, collect());
	});
}

void SaveEdited(
		const ProviderSettings &result,
		bool makeDefault,
		ProviderService keyService) {
	UpdateProviders([&](ProvidersState &state) {
		for (auto &item : state.list) {
			if (item.id == result.id) {
				item = result;
				break;
			}
		}
		if (keyService != ProviderService::Custom) {
			SetServiceKey(state, keyService, result.apiKey);
		}
		if (FindProvider(state, result.id)) {
			ApplyDefault(state, result.id, makeDefault);
		}
	});
}

void EditChatGptBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ProviderSettings provider) {
	box->setTitle(tr::lng_agent_edit_provider());
	box->setWidth(st::boxWideWidth);
	const auto state = box->lifetime().make_state<EditState>();
	state->current = provider;
	const auto manager = &controller->session().agent();
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto name = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			provider.title),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	AddModelRow(
		container,
		controller,
		state,
		[=] { return ChatGptSource(manager); },
		[=](const ModelInfo &model) {
			ApplyModelInfo(state->current, model);
			state->current.priceInput = 0.;
			state->current.priceOutput = 0.;
			state->current.priceCached = 0.;
			state->current.priceCacheWrite = 0.;
		});
	AddEffortRow(container, controller, state);
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_chatgpt_account(),
		EditValue(state, [=] {
			return ChatGptSignedIn()
				? AccountText(ChatGptAccountState())
				: tr::lng_agent_chatgpt_account_none(tr::now);
		}),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		if (ChatGptSignedIn()) {
			return;
		}
		controller->show(Box(ChatGptSetupBox, controller, [=] {
			state->changed.fire({});
		}));
	});
	const auto signOutWrap = container->add(
		object_ptr<::Ui::SlideWrap<::Ui::VerticalLayout>>(
			container,
			object_ptr<::Ui::VerticalLayout>(container)));
	Settings::AddButtonWithIcon(
		signOutWrap->entity(),
		tr::lng_agent_chatgpt_sign_out(),
		st::settingsAttentionButton
	)->setClickedCallback([=] {
		controller->show(::Ui::MakeConfirmBox({
			.text = tr::lng_agent_chatgpt_sign_out_sure(),
			.confirmed = [=](Fn<void()> close) {
				ChatGptSignOut();
				close();
			},
			.confirmText = tr::lng_agent_chatgpt_sign_out(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
	signOutWrap->toggle(ChatGptSignedIn(), anim::type::instant);
	ChatGptAccountChanged(
	) | rpl::on_next([=] {
		signOutWrap->toggle(ChatGptSignedIn(), anim::type::instant);
		state->changed.fire({});
	}, box->lifetime());
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_chatgpt_usage(),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		UrlClickHandler::Open(ChatGpt::UsageUrl());
	});
	::Ui::AddDividerText(container, tr::lng_agent_chatgpt_usage_about());
	const auto collect = [=] {
		auto result = state->current;
		result.title = name->getLastText().trimmed();
		return result;
	};
	AddCheckRow(container, controller, collect);
	::Ui::AddSkip(container);
	const auto makeDefault = AddDefaultCheckbox(container, provider.id);
	::Ui::AddSkip(container);
	const auto save = [=] {
		const auto result = collect();
		if (result.title.isEmpty()) {
			name->showError();
			return;
		}
		SaveEdited(
			result,
			makeDefault->checked(),
			ProviderService::Custom);
		box->closeBox();
	};
	name->submits(
	) | rpl::on_next(save, name->lifetime());
	box->addLeftButton(tr::lng_box_delete(), [=] {
		ConfirmRemove(controller, box, state->current);
	});
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		name->setFocusFast();
	});
}

void EditOpenAiKeyBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ProviderSettings provider) {
	box->setTitle(tr::lng_agent_edit_provider());
	box->setWidth(st::boxWideWidth);
	const auto state = box->lifetime().make_state<EditState>();
	state->current = provider;
	const auto prices = box->lifetime().make_state<PriceFields>();
	const auto manager = &controller->session().agent();
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto name = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			provider.title),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	const auto key = AddKeyField(container, provider.apiKey);
	::Ui::AddSkip(container);
	AddModelRow(
		container,
		controller,
		state,
		[=] {
			return OpenAiKeySource(
				manager,
				key->getLastText().trimmed());
		},
		[=](const ModelInfo &model) {
			ApplyModelInfo(state->current, model);
			prices->input->setText(PriceText(state->current.priceInput));
			prices->output->setText(PriceText(state->current.priceOutput));
			prices->cached->setText(PriceText(state->current.priceCached));
		});
	AddEffortRow(container, controller, state);
	const auto addPrice = [&](rpl::producer<QString> text, double price) {
		return container->add(
			object_ptr<::Ui::InputField>(
				container,
				st::defaultInputField,
				std::move(text),
				PriceText(price)),
			st::boxRowPadding);
	};
	const auto priceInput = addPrice(
		tr::lng_agent_provider_price_input(),
		provider.priceInput);
	prices->input = priceInput;
	const auto priceOutput = addPrice(
		tr::lng_agent_provider_price_output(),
		provider.priceOutput);
	prices->output = priceOutput;
	const auto priceCached = addPrice(
		tr::lng_agent_provider_price_cached(),
		provider.priceCached);
	prices->cached = priceCached;
	const auto collect = [=]() -> std::optional<ProviderSettings> {
		auto result = state->current;
		result.title = name->getLastText().trimmed();
		result.apiKey = key->getLastText().trimmed();
		if (result.title.isEmpty()) {
			name->showError();
			return std::nullopt;
		} else if (result.apiKey.isEmpty()) {
			key->showError();
			return std::nullopt;
		}
		const auto parse = [&](not_null<::Ui::InputField*> field) {
			const auto value = ParsePrice(field->getLastText());
			if (!value) {
				field->showError();
				box->uiShow()->showToast(
					tr::lng_agent_error_price(tr::now));
			}
			return value;
		};
		const auto input = parse(priceInput);
		const auto output = parse(priceOutput);
		const auto cached = parse(priceCached);
		if (!input || !output || !cached) {
			return std::nullopt;
		}
		result.priceInput = *input;
		result.priceOutput = *output;
		result.priceCached = *cached;
		return result;
	};
	AddCheckRow(container, controller, [=] {
		return collect().value_or(state->current);
	});
	::Ui::AddSkip(container);
	const auto makeDefault = AddDefaultCheckbox(container, provider.id);
	::Ui::AddSkip(container);
	const auto save = [=] {
		const auto result = collect();
		if (!result) {
			return;
		}
		SaveEdited(
			*result,
			makeDefault->checked(),
			ProviderService::OpenAi);
		box->closeBox();
	};
	name->submits(
	) | rpl::on_next(save, name->lifetime());
	box->addLeftButton(tr::lng_box_delete(), [=] {
		ConfirmRemove(controller, box, state->current);
	});
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		name->setFocusFast();
	});
}

} // namespace

void ShowOpenAiChoice(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	controller->show(Box(OpenAiChoiceBox, controller, std::move(added)));
}

void ShowEditChatGpt(
		not_null<Window::SessionController*> controller,
		const ProviderSettings &provider) {
	controller->show(Box(EditChatGptBox, controller, provider));
}

void ShowEditOpenAiKey(
		not_null<Window::SessionController*> controller,
		const ProviderSettings &provider) {
	controller->show(Box(EditOpenAiKeyBox, controller, provider));
}

} // namespace Agent::Ui