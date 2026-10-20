/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_claude_boxes.h"

#include "agent/agent_check.h"
#include "agent/agent_claude.h"
#include "agent/agent_manager.h"
#include "agent/agent_settings.h"
#include "agent/core/agent_anthropic.h"
#include "agent/core/agent_claude_engine.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_model_boxes_common.h"
#include "base/unixtime.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

#include <rpl/map.h>
#include <rpl/then.h>

#include "styles/style_agent_general.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Agent::Ui {
namespace {

using namespace details;

constexpr auto kClaudeContextWindow = 200'000;

[[nodiscard]] base::flat_map<QString, std::vector<QString>> &EffortLevelsCache() {
	static auto result = base::flat_map<QString, std::vector<QString>>();
	return result;
}

[[nodiscard]] base::flat_map<QString, QString> &ClaudeDescriptions() {
	static auto result = base::flat_map<QString, QString>();
	return result;
}

[[nodiscard]] std::vector<QString> EffortChoices(const QString &model) {
	const auto &cache = EffortLevelsCache();
	const auto i = cache.find(model);
	return (i != cache.end() && !i->second.empty())
		? i->second
		: Anthropic::EffortLevels();
}

[[nodiscard]] QString LimitText(const Claude::LimitWindow &window) {
	if (window.utilization < 0.) {
		return tr::lng_agent_claude_limit_unknown(tr::now);
	}
	const auto percent = int(std::lround(window.utilization * 100.));
	const auto resets = base::unixtime::parse(TimeId(window.resetsAt));
	const auto today = QDate::currentDate();
	const auto when = (resets.date() == today)
		? QLocale().toString(resets.time(), QLocale::ShortFormat)
		: QLocale().toString(resets, QLocale::ShortFormat);
	return tr::lng_agent_claude_limit_value(
		tr::now,
		lt_percent,
		QString::number(percent),
		lt_when,
		when);
}

[[nodiscard]] QString PlanText(const Claude::AuthStatus &status) {
	auto plan = status.subscription;
	if (!plan.isEmpty()) {
		plan[0] = plan[0].toUpper();
	} else {
		plan = status.method;
	}
	return tr::lng_agent_claude_signed_in(
		tr::now,
		lt_email,
		status.email,
		lt_status,
		plan);
}

[[nodiscard]] QString OutputError(int code, const QByteArray &output) {
	const auto lines = QString::fromUtf8(output).split(
		'\n',
		Qt::SkipEmptyParts);
	return !lines.isEmpty()
		? lines.back().trimmed()
		: (u"exit code "_q + QString::number(code));
}

void EffortBox(
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
			tr::lng_agent_effort_about(),
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

struct SetupState {
	std::unique_ptr<ClaudeCommand> command;
	QString program;
};

void ClaudeSetupBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString program, Claude::AuthStatus status)> ready) {
	box->setTitle(tr::lng_agent_claude_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto label = container->add(
		object_ptr<::Ui::FlatLabel>(container, st::boxLabel),
		st::boxRowPadding);
	const auto command = container->add(
		object_ptr<::Ui::SlideWrap<::Ui::FlatLabel>>(
			container,
			object_ptr<::Ui::FlatLabel>(container, st::boxLabel),
			st::agentClaudeCommandPadding));
	command->entity()->setMarkedText(::Ui::Text::Wrapped(
		TextWithEntities{ ClaudeInstallCommand() },
		EntityType::Code));
	command->entity()->setSelectable(true);
	command->toggle(false, anim::type::instant);
	::Ui::AddSkip(container);

	const auto state = box->lifetime().make_state<SetupState>();
	const auto check = std::make_shared<Fn<void()>>();
	const auto later = [=](Fn<void(int, QByteArray)> handler) {
		return [=](int code, QByteArray output) {
			crl::on_main(box, [=] {
				handler(code, output);
			});
		};
	};
	const auto show = [=](QString text, bool install) {
		label->setText(text);
		command->toggle(install, anim::type::instant);
		box->clearButtons();
	};
	const auto addCheckAgain = [=] {
		box->addButton(tr::lng_agent_claude_check_again(), [=] {
			(*check)();
		});
	};
	const auto addCancel = [=] {
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	const auto choosePath = [=] {
		FileDialog::GetOpenPath(
			box.get(),
			tr::lng_agent_claude_choose_path(tr::now),
			QString(),
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					return;
				}
				const auto path = result.paths.front();
				UpdateProviders([&](ProvidersState &providers) {
					providers.claudePath = path;
				});
				(*check)();
			}));
	};
	const auto failed = [=](QString error) {
		show(tr::lng_agent_claude_failed(tr::now, lt_error, error), false);
		addCheckAgain();
		addCancel();
	};
	const auto login = [=] {
		show(tr::lng_agent_claude_login_waiting(tr::now), false);
		addCheckAgain();
		addCancel();
		state->command = std::make_unique<ClaudeCommand>(
			state->program,
			QStringList{ u"auth"_q, u"login"_q, u"--claudeai"_q },
			later([=](int code, QByteArray output) {
				state->command = nullptr;
				if (code == 0) {
					(*check)();
				} else {
					failed(OutputError(code, output));
				}
			}));
	};
	const auto status = [=] {
		state->command = std::make_unique<ClaudeCommand>(
			state->program,
			QStringList{ u"auth"_q, u"status"_q },
			later([=](int code, QByteArray output) {
				state->command = nullptr;
				const auto parsed = Claude::ParseAuthStatus(output);
				if (!parsed) {
					failed(OutputError(code, output));
				} else if (!parsed->loggedIn) {
					show(tr::lng_agent_claude_logged_out(tr::now), false);
					box->addButton(tr::lng_agent_claude_login(), login);
					addCancel();
				} else {
					const auto program = state->program;
					const auto weak = QPointer<::Ui::GenericBox>(box);
					ready(program, *parsed);
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
				}
			}));
	};
	*check = [=] {
		state->command = nullptr;
		show(tr::lng_agent_claude_checking(tr::now), false);
		addCancel();
		const auto configured = LoadProviders().claudePath;
		state->program = FindClaudeProgram(configured);
		if (state->program.isEmpty()) {
			auto text = tr::lng_agent_claude_missing(tr::now);
			if (!configured.isEmpty()) {
				text = tr::lng_agent_claude_missing_path(
					tr::now,
					lt_path,
					configured) + u"\n\n"_q + text;
			}
			show(text, true);
			addCheckAgain();
			box->addLeftButton(
				tr::lng_agent_claude_choose_path(),
				choosePath);
			addCancel();
			return;
		}
		state->command = std::make_unique<ClaudeCommand>(
			state->program,
			QStringList{ u"--version"_q },
			later([=](int code, QByteArray output) {
				state->command = nullptr;
				const auto version = Claude::ParseVersion(output);
				if (!version) {
					failed(OutputError(code, output));
				} else if (!Claude::VersionSupported(*version)) {
					show(tr::lng_agent_claude_old(
						tr::now,
						lt_version,
						u"%1.%2.%3"_q
							.arg((*version)[0])
							.arg((*version)[1])
							.arg((*version)[2])), false);
					addCheckAgain();
					addCancel();
				} else {
					status();
				}
			}));
	};
	(*check)();
}

[[nodiscard]] ModelSource ClaudeSource(
		not_null<Manager*> manager,
		QString program) {
	auto result = ModelSource();
	result.load = [=](
			not_null<::Ui::GenericBox*> box,
			Fn<void(std::vector<ModelInfo>, QString)> done) {
		const auto query = box->lifetime().make_state<
			std::unique_ptr<Claude::ModelsQuery>>();
		*query = std::make_unique<Claude::ModelsQuery>(
			&manager->claudeLauncher(),
			ClaudeLaunchSettings(program),
			[=](std::vector<Claude::ModelOption> options, QString error) {
				crl::on_main(box, [=] {
					*query = nullptr;
					auto models = std::vector<ModelInfo>();
					for (const auto &option : options) {
						if (option.value == u"default"_q) {
							continue;
						}
						EffortLevelsCache()[option.value]
							= option.effortLevels;
						auto info = ModelInfo();
						info.id = option.value;
						info.name = option.name;
						info.contextWindow = kClaudeContextWindow;
						info.tools = true;
						info.vision = true;
						models.push_back(std::move(info));
					}
					ClaudeDescriptions() = {};
					for (const auto &option : options) {
						ClaudeDescriptions()[option.value]
							= option.description;
					}
					done(std::move(models), error);
				});
			});
	};
	result.describe = [](const ModelInfo &model) {
		const auto &descriptions = ClaudeDescriptions();
		const auto i = descriptions.find(model.id);
		const auto description = (i != descriptions.end())
			? i->second
			: QString();
		return description.isEmpty()
			? model.id
			: (description + u" · "_q + model.id);
	};
	return result;
}

void ClaudeNameBox(
		not_null<::Ui::GenericBox*> box,
		ModelInfo model,
		Fn<void(QString id)> added) {
	box->setTitle(tr::lng_agent_model_name_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto suggested = model.name.isEmpty()
		? model.id
		: (u"Claude "_q + model.name);
	const auto name = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			suggested),
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
		auto provider = ProviderSettings();
		provider.id = GenerateProviderId();
		provider.kind = ProviderKind::ClaudeCode;
		provider.service = ProviderService::ClaudeSubscription;
		provider.title = title;
		provider.model = model.id;
		provider.contextWindow = kClaudeContextWindow;
		provider.vision = true;
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

void StartClaudeSubscription(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	const auto manager = &controller->session().agent();
	controller->show(Box(ClaudeSetupBox, controller, [=](
			QString program,
			Claude::AuthStatus status) {
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				QString(),
				ClaudeSource(manager, program),
				[=](ModelInfo model) {
					controller->show(Box(ClaudeNameBox, model, added));
				});
		}));
	}));
}

[[nodiscard]] ProviderSettings AnthropicRequestSettings(const QString &key) {
	auto result = ProviderSettings();
	result.kind = ProviderKind::Anthropic;
	result.service = ProviderService::Anthropic;
	result.baseUrl = AnthropicBaseUrl();
	result.apiKey = key;
	return result;
}

[[nodiscard]] Error ParseAnthropicError(
		const HttpHead &head,
		const QByteArray &body) {
	return Anthropic::ParseError(head, body);
}

[[nodiscard]] ModelSource AnthropicSource(
		not_null<Manager*> manager,
		QString key) {
	auto result = ModelSource();
	result.load = [=](
			not_null<::Ui::GenericBox*> box,
			Fn<void(std::vector<ModelInfo>, QString)> done) {
		Fetch(
			manager,
			box->lifetime(),
			Anthropic::ModelsRequest(AnthropicRequestSettings(key)),
			[=](FetchResult result) {
				if (!result.error.isEmpty() || result.status != 200) {
					done({}, FetchErrorText(result, ParseAnthropicError));
					return;
				}
				auto models = std::vector<ModelInfo>();
				for (const auto &entry : Anthropic::ParseModels(
						result.body)) {
					EffortLevelsCache()[entry.id] = entry.effortLevels;
					auto info = ModelInfo();
					info.id = entry.id;
					info.name = entry.name;
					info.contextWindow = entry.contextWindow;
					info.tools = true;
					info.vision = entry.vision;
					info.files = entry.pdf;
					if (entry.prices) {
						info.priceInput = entry.prices->input;
						info.priceOutput = entry.prices->output;
						info.priceCached = entry.prices->cached;
					}
					models.push_back(std::move(info));
				}
				done(std::move(models), QString());
			});
	};
	return result;
}

void ApplyAnthropicModel(ProviderSettings &provider, const ModelInfo &model) {
	provider.model = model.id;
	provider.contextWindow = model.contextWindow;
	provider.vision = model.vision.value_or(false);
	provider.files = model.files.value_or(false);
	provider.audio = false;
	provider.video = false;
	const auto prices = Anthropic::KnownPrices(model.id);
	provider.priceInput = prices ? prices->input : 0.;
	provider.priceOutput = prices ? prices->output : 0.;
	provider.priceCached = prices ? prices->cached : 0.;
	provider.priceCacheWrite = prices ? prices->cacheWrite : 0.;
	const auto levels = EffortChoices(model.id);
	if (!provider.effort.isEmpty()
		&& !ranges::contains(levels, provider.effort)) {
		provider.effort = QString();
	}
}

void AnthropicKeyBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString key)> next) {
	box->setTitle(tr::lng_agent_anthropic_key_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_anthropic_key_about(
				lt_link,
				rpl::single(tr::link(
					u"platform.claude.com"_q,
					u"https://platform.claude.com/settings/keys"_q)),
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
			box->lifetime(),
			Anthropic::ModelsRequest(AnthropicRequestSettings(value)),
			[=](FetchResult result) {
				*checking = false;
				if (result.error.isEmpty() && result.status == 200) {
					next(value);
					box->closeBox();
				} else if (result.error.isEmpty()
					&& (result.status == 401 || result.status == 403)) {
					key->showError();
					box->uiShow()->showToast(
						tr::lng_agent_anthropic_key_wrong(tr::now));
				} else {
					box->uiShow()->showToast(
						tr::lng_agent_openrouter_key_failed(
							tr::now,
							lt_text,
							FetchErrorText(result, ParseAnthropicError)));
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

void AnthropicNameBox(
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
		auto provider = AnthropicRequestSettings(key);
		provider.id = GenerateProviderId();
		provider.title = title;
		ApplyAnthropicModel(provider, model);
		const auto checked = makeDefault->checked();
		UpdateProviders([&](ProvidersState &state) {
			state.list.push_back(provider);
			SetServiceKey(state, ProviderService::Anthropic, key);
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

void StartAnthropicKey(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	const auto manager = &controller->session().agent();
	const auto pickModel = [=](QString key) {
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				QString(),
				AnthropicSource(manager, key),
				[=](ModelInfo model) {
					controller->show(Box(
						AnthropicNameBox,
						key,
						std::move(model),
						added));
				});
		}));
	};
	const auto key = ServiceKey(LoadProviders(), ProviderService::Anthropic);
	if (key.isEmpty()) {
		controller->show(Box(AnthropicKeyBox, controller, pickModel));
	} else {
		pickModel(key);
	}
}

void AnthropicChoiceBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	box->setTitle(tr::lng_agent_anthropic_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_anthropic_subscription(),
		st::settingsButton,
		{ &st::agentModelChoiceSubscriptionIcon }
	)->setClickedCallback([=] {
		box->closeBox();
		StartClaudeSubscription(controller, added);
	});
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_anthropic_api_key(),
		st::settingsButton,
		{ &st::agentModelChoiceKeyIcon }
	)->setClickedCallback([=] {
		box->closeBox();
		StartAnthropicKey(controller, added);
	});
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_anthropic_about());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ProgramBox(
		not_null<::Ui::GenericBox*> box,
		Fn<void()> changed) {
	box->setTitle(tr::lng_agent_claude_program());
	const auto container = box->verticalLayout();
	const auto configured = LoadProviders().claudePath;
	const auto found = FindClaudeProgram(configured);
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			!found.isEmpty()
				? found
				: tr::lng_agent_claude_missing_path(
					tr::now,
					lt_path,
					configured.isEmpty() ? u"PATH"_q : configured),
			st::boxLabel),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	box->addButton(tr::lng_agent_claude_choose_path(), [=] {
		FileDialog::GetOpenPath(
			box.get(),
			tr::lng_agent_claude_choose_path(tr::now),
			QString(),
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					return;
				}
				const auto path = result.paths.front();
				UpdateProviders([&](ProvidersState &state) {
					state.claudePath = path;
				});
				changed();
				box->closeBox();
			}));
	});
	if (!configured.isEmpty()) {
		box->addLeftButton(tr::lng_agent_claude_program_auto(), [=] {
			UpdateProviders([&](ProvidersState &state) {
				state.claudePath = QString();
			});
			changed();
			box->closeBox();
		});
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

struct EditState {
	ProviderSettings current;
	rpl::event_stream<> changed;
	std::optional<Claude::AuthStatus> auth;
	std::unique_ptr<ClaudeCommand> command;
};

[[nodiscard]] rpl::producer<QString> EditValue(
		not_null<EditState*> state,
		Fn<QString()> compute) {
	return rpl::single(rpl::empty) | rpl::then(
		state->changed.events()
	) | rpl::map(compute);
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
			EffortBox,
			state->current.model,
			state->current.effort,
			[=](QString effort) {
				state->current.effort = effort;
				state->changed.fire({});
			}));
	});
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

void EditClaudeBox(
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
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_provider_model(),
		EditValue(state, [=] { return state->current.model; }),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		const auto program = FindClaudeProgram(LoadProviders().claudePath);
		if (program.isEmpty()) {
			controller->show(Box(ProgramBox, [=] {
				state->changed.fire({});
			}));
			return;
		}
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				state->current.model,
				ClaudeSource(manager, program),
				[=](ModelInfo model) {
					state->current.model = model.id;
					const auto levels = EffortChoices(model.id);
					if (!ranges::contains(levels, state->current.effort)) {
						state->current.effort = QString();
					}
					state->changed.fire({});
				});
		}));
	});
	AddEffortRow(container, controller, state);
	const auto refreshAuth = [=] {
		const auto program = FindClaudeProgram(LoadProviders().claudePath);
		state->auth = std::nullopt;
		state->changed.fire({});
		if (program.isEmpty()) {
			return;
		}
		state->command = std::make_unique<ClaudeCommand>(
			program,
			QStringList{ u"auth"_q, u"status"_q },
			[=](int code, QByteArray output) {
				crl::on_main(box, [=] {
					state->command = nullptr;
					state->auth = Claude::ParseAuthStatus(output);
					state->changed.fire({});
				});
			});
	};
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_claude_account(),
		EditValue(state, [=] {
			return !state->auth
				? QString()
				: state->auth->loggedIn
				? PlanText(*state->auth)
				: tr::lng_agent_claude_account_none(tr::now);
		}),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		controller->show(Box(ClaudeSetupBox, controller, [=](
				QString,
				Claude::AuthStatus status) {
			state->auth = status;
			state->changed.fire({});
		}));
	});
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_claude_program(),
		EditValue(state, [=] {
			const auto configured = LoadProviders().claudePath;
			return configured.isEmpty()
				? tr::lng_agent_claude_program_auto(tr::now)
				: configured;
		}),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		controller->show(Box(ProgramBox, refreshAuth));
	});
	AddClaudeLimits(container);
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_claude_limits_about());
	const auto collect = [=] {
		auto result = state->current;
		result.title = name->getLastText().trimmed();
		return result;
	};
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_check_button(),
		st::settingsButtonLightNoIcon
	)->setClickedCallback([=] {
		ShowCheckBox(controller, collect());
	});
	::Ui::AddSkip(container);
	const auto makeDefault = AddDefaultCheckbox(container, provider.id);
	::Ui::AddSkip(container);
	const auto save = [=] {
		const auto result = collect();
		if (result.title.isEmpty()) {
			name->showError();
			return;
		}
		SaveEdited(result, makeDefault->checked(), ProviderService::Custom);
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
	refreshAuth();
}

void EditAnthropicBox(
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
	const auto key = AddKeyField(container, provider.apiKey);
	::Ui::AddSkip(container);
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_provider_model(),
		EditValue(state, [=] { return state->current.model; }),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		const auto value = key->getLastText().trimmed();
		if (value.isEmpty()) {
			key->showError();
			return;
		}
		controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
			details::ModelListBox(
				box,
				state->current.model,
				AnthropicSource(manager, value),
				[=](ModelInfo model) {
					ApplyAnthropicModel(state->current, model);
					state->changed.fire({});
				});
		}));
	});
	AddEffortRow(container, controller, state);
	const auto collect = [=] {
		auto result = state->current;
		result.title = name->getLastText().trimmed();
		result.apiKey = key->getLastText().trimmed();
		return result;
	};
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_check_button(),
		st::settingsButtonLightNoIcon
	)->setClickedCallback([=] {
		ShowCheckBox(controller, collect());
	});
	::Ui::AddSkip(container);
	const auto makeDefault = AddDefaultCheckbox(container, provider.id);
	::Ui::AddSkip(container);
	const auto save = [=] {
		const auto result = collect();
		if (result.title.isEmpty()) {
			name->showError();
			return;
		} else if (result.apiKey.isEmpty()) {
			key->showError();
			return;
		}
		SaveEdited(
			result,
			makeDefault->checked(),
			ProviderService::Anthropic);
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

QString EffortText(const QString &effort) {
	if (effort == u"low"_q) {
		return tr::lng_agent_effort_low(tr::now);
	} else if (effort == u"medium"_q) {
		return tr::lng_agent_effort_medium(tr::now);
	} else if (effort == u"high"_q) {
		return tr::lng_agent_effort_high(tr::now);
	} else if (effort == u"xhigh"_q) {
		return tr::lng_agent_effort_xhigh(tr::now);
	} else if (effort == u"max"_q) {
		return tr::lng_agent_effort_max(tr::now);
	}
	return tr::lng_agent_effort_default(tr::now);
}

rpl::producer<QString> ClaudeLimitValue(bool week) {
	return ClaudeLimitsValue(
	) | rpl::map([=](const Claude::Limits &limits) {
		return LimitText(week ? limits.sevenDay : limits.fiveHour);
	});
}

void AddClaudeLimits(not_null<::Ui::VerticalLayout*> container) {
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_claude_limit_five_hours(),
		ClaudeLimitValue(false),
		st::settingsButtonNoIcon
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_claude_limit_week(),
		ClaudeLimitValue(true),
		st::settingsButtonNoIcon
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ShowAnthropicChoice(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	controller->show(Box(AnthropicChoiceBox, controller, std::move(added)));
}

void ShowEditClaude(
		not_null<Window::SessionController*> controller,
		const ProviderSettings &provider) {
	controller->show(Box(EditClaudeBox, controller, provider));
}

void ShowEditAnthropic(
		not_null<Window::SessionController*> controller,
		const ProviderSettings &provider) {
	controller->show(Box(EditAnthropicBox, controller, provider));
}

} // namespace Agent::Ui
