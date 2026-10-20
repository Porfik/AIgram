/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_model_boxes.h"

#include "agent/agent_check.h"
#include "agent/agent_manager.h"
#include "agent/agent_settings.h"
#include "agent/core/agent_openai.h"
#include "agent/core/agent_transport.h"
#include "base/weak_ptr.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <rpl/map.h>
#include <rpl/then.h>

#include <QtCore/QPointer>
#include <QtCore/QUrl>

#include "styles/style_agent_general.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {
namespace {

using OpenAi::ModelInfo;
using OpenAi::ModelRoute;

constexpr auto kModelsCacheTimeout = crl::time(10 * 60 * 1000);

struct FetchResult {
	int status = 0;
	QByteArray body;
	QString error;
};

struct ModelsCache {
	std::vector<ModelInfo> list;
	crl::time loaded = 0;
};

[[nodiscard]] ModelsCache &OpenRouterModels() {
	static auto result = ModelsCache();
	return result;
}

[[nodiscard]] ProviderSettings OpenRouterRequestSettings(
		const QString &key = QString()) {
	auto result = ProviderSettings();
	result.service = ProviderService::OpenRouter;
	result.baseUrl = OpenRouterBaseUrl();
	result.apiKey = key;
	return result;
}

void Fetch(
		not_null<Manager*> manager,
		rpl::lifetime &lifetime,
		HttpRequest request,
		Fn<void(FetchResult)> done) {
	const auto result = std::make_shared<FetchResult>();
	const auto id = manager->transport().start(
		std::move(request),
		TransportHandlers{
			[=](HttpHead head) {
				result->status = head.status;
			},
			[=](QByteArray chunk) {
				result->body += chunk;
			},
			[=](QString error) {
				result->error = std::move(error);
				done(std::move(*result));
			},
		});
	const auto weak = base::make_weak(manager);
	lifetime.add([=] {
		if (const auto strong = weak.get()) {
			strong->transport().cancel(id);
		}
	});
}

[[nodiscard]] QString FetchErrorText(const FetchResult &result) {
	if (!result.error.isEmpty()) {
		return result.error;
	}
	const auto error = OpenAi::ParseError(
		HttpHead{ .status = result.status },
		result.body);
	return !error.text.isEmpty()
		? error.text
		: (u"HTTP "_q + QString::number(result.status));
}

[[nodiscard]] QString FormatContext(int tokens) {
	if (tokens >= 1'000'000) {
		return QString::number(tokens / 1'000'000., 'g', 3) + 'M';
	} else if (tokens >= 1000) {
		return QString::number(tokens / 1000) + 'K';
	}
	return QString::number(tokens);
}

[[nodiscard]] QString FormatPrice(double price) {
	return '$' + QString::number(price, 'g', 3);
}

[[nodiscard]] QString ModelInfoDescription(const ModelInfo &model) {
	auto parts = QStringList();
	if (model.contextWindow > 0) {
		parts.push_back(tr::lng_agent_model_context(
			tr::now,
			lt_amount,
			FormatContext(model.contextWindow)));
	}
	parts.push_back((model.priceInput > 0. || model.priceOutput > 0.)
		? tr::lng_agent_model_prices(
			tr::now,
			lt_input,
			FormatPrice(model.priceInput),
			lt_output,
			FormatPrice(model.priceOutput))
		: tr::lng_agent_model_free(tr::now));
	parts.push_back(model.id);
	return parts.join(u" · "_q);
}

[[nodiscard]] QString SuggestedName(const ModelInfo &model) {
	const auto name = model.name.trimmed();
	const auto colon = name.indexOf(u": "_q);
	if (colon > 0 && colon + 2 < name.size()) {
		return name.mid(colon + 2).trimmed();
	}
	return name.isEmpty() ? model.id : name;
}

void ApplyModelInfo(ProviderSettings &provider, const ModelInfo &model) {
	provider.model = model.id;
	provider.contextWindow = model.contextWindow;
	provider.vision = model.vision.value_or(false);
	provider.audio = model.audio.value_or(false);
	provider.video = model.video.value_or(false);
	provider.files = model.files.value_or(false);
	provider.priceInput = model.priceInput;
	provider.priceOutput = model.priceOutput;
	provider.priceCached = model.priceCached;
}

[[nodiscard]] QString RoutesText(const ProviderSettings &provider) {
	if (provider.ignoredRoutes.empty()) {
		return tr::lng_agent_routes_auto(tr::now);
	}
	auto names = QStringList();
	for (const auto &name : provider.ignoredRoutes) {
		names.push_back(name);
	}
	return tr::lng_agent_routes_skip(
		tr::now,
		lt_text,
		names.join(u", "_q));
}

[[nodiscard]] not_null<::Ui::PasswordInput*> AddKeyField(
		not_null<::Ui::VerticalLayout*> container,
		const QString &value) {
	auto wrap = object_ptr<::Ui::RpWidget>(container);
	const auto key = ::Ui::CreateChild<::Ui::PasswordInput>(
		wrap.data(),
		st::defaultInputField,
		tr::lng_agent_provider_key(),
		value);
	key->move(0, 0);
	key->heightValue(
	) | rpl::on_next([=, raw = wrap.data()](int height) {
		raw->resize(raw->width(), height);
	}, key->lifetime());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		key->resize(width, key->height());
	}, key->lifetime());
	container->add(std::move(wrap), st::boxRowPadding);
	return key;
}

void RemoveProfile(const QString &id) {
	UpdateProviders([&](ProvidersState &state) {
		auto &list = state.list;
		list.erase(
			ranges::remove(list, id, &ProviderSettings::id),
			end(list));
		if (state.defaultId == id) {
			state.defaultId = QString();
		}
		if (state.mediaId == id) {
			state.mediaId = QString();
		}
		if (state.backgroundId == id) {
			state.backgroundId = QString();
		}
	});
}

void ConfirmRemove(
		not_null<Window::SessionController*> controller,
		not_null<::Ui::GenericBox*> box,
		const ProviderSettings &provider) {
	const auto weak = QPointer<::Ui::GenericBox>(box.get());
	const auto id = provider.id;
	controller->show(::Ui::MakeConfirmBox({
		.text = tr::lng_agent_model_delete_sure(
			tr::now,
			lt_name,
			ModelTitle(provider)),
		.confirmed = [=](Fn<void()> close) {
			RemoveProfile(id);
			close();
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		},
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void ApplyDefault(
		ProvidersState &state,
		const QString &id,
		bool makeDefault) {
	if (makeDefault || state.list.size() == 1) {
		state.defaultId = id;
	} else if (state.defaultId == id) {
		state.defaultId = QString();
	}
}

class ModelList final : public ::Ui::RpWidget {
public:
	ModelList(
		QWidget *parent,
		std::vector<ModelInfo> models,
		QString currentId);

	void filter(const QString &query);
	void chooseFirst();
	[[nodiscard]] bool empty() const;
	[[nodiscard]] rpl::producer<ModelInfo> chosen() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int rowHeight() const;
	[[nodiscard]] int indexAt(QPoint point) const;
	void setSelected(int index);

	const std::vector<ModelInfo> _models;
	const QString _currentId;
	std::vector<QString> _descriptions;
	std::vector<int> _filtered;
	int _selected = -1;
	int _pressed = -1;
	rpl::event_stream<ModelInfo> _chosen;

};

ModelList::ModelList(
	QWidget *parent,
	std::vector<ModelInfo> models,
	QString currentId)
: RpWidget(parent)
, _models(std::move(models))
, _currentId(std::move(currentId)) {
	_descriptions.reserve(_models.size());
	for (const auto &model : _models) {
		_descriptions.push_back(ModelInfoDescription(model));
	}
	setMouseTracking(true);
	filter(QString());
}

void ModelList::filter(const QString &query) {
	_filtered = OpenAi::SearchModels(_models, query);
	_selected = _pressed = -1;
	resizeToWidth(width());
	update();
}

void ModelList::chooseFirst() {
	if (!_filtered.empty()) {
		_chosen.fire_copy(_models[_filtered.front()]);
	}
}

bool ModelList::empty() const {
	return _filtered.empty();
}

rpl::producer<ModelInfo> ModelList::chosen() const {
	return _chosen.events();
}

int ModelList::rowHeight() const {
	const auto &padding = st::agentModelRowPadding;
	return padding.top()
		+ st::semiboldFont->height
		+ st::agentModelRowSkip
		+ st::normalFont->height
		+ padding.bottom();
}

int ModelList::resizeGetHeight(int newWidth) {
	return int(_filtered.size()) * rowHeight();
}

int ModelList::indexAt(QPoint point) const {
	const auto index = (point.y() >= 0) ? (point.y() / rowHeight()) : -1;
	return (index >= 0 && index < int(_filtered.size())) ? index : -1;
}

void ModelList::setSelected(int index) {
	if (_selected != index) {
		_selected = index;
		setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void ModelList::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	const auto height = rowHeight();
	const auto &padding = st::agentModelRowPadding;
	const auto &check = st::mediaPlayerMenuCheck;
	const auto from = std::max(clip.y() / height, 0);
	const auto till = std::min(
		(clip.y() + clip.height() + height - 1) / height,
		int(_filtered.size()));
	for (auto i = from; i < till; ++i) {
		const auto &model = _models[_filtered[i]];
		const auto top = i * height;
		const auto selected = (i == _selected);
		if (selected) {
			p.fillRect(0, top, width(), height, st::windowBgOver);
		}
		const auto current = (model.id == _currentId);
		const auto right = padding.right()
			+ (current ? (check.width() + st::agentModelRowCheckSkip) : 0);
		const auto available = std::max(
			width() - padding.left() - right,
			1);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawTextLeft(
			padding.left(),
			top + padding.top(),
			width(),
			st::semiboldFont->elided(
				model.name.isEmpty() ? model.id : model.name,
				available));
		p.setFont(st::normalFont);
		p.setPen(selected ? st::windowSubTextFgOver : st::windowSubTextFg);
		p.drawTextLeft(
			padding.left(),
			top + padding.top() + st::semiboldFont->height
				+ st::agentModelRowSkip,
			width(),
			st::normalFont->elided(_descriptions[_filtered[i]], available));
		if (current) {
			check.paint(
				p,
				width() - padding.right() - check.width(),
				top + (height - check.height()) / 2,
				width());
		}
	}
}

void ModelList::mouseMoveEvent(QMouseEvent *e) {
	setSelected(indexAt(e->pos()));
}

void ModelList::mousePressEvent(QMouseEvent *e) {
	_pressed = indexAt(e->pos());
}

void ModelList::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed >= 0 && pressed == indexAt(e->pos())) {
		_chosen.fire_copy(_models[_filtered[pressed]]);
	}
}

void ModelList::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void ModelListBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString currentId,
		Fn<void(ModelInfo)> chosen) {
	box->setTitle(tr::lng_agent_models_choose_title());
	box->setWidth(st::boxWideWidth);
	box->setMinHeight(st::boxMaxListHeight);
	box->setMaxHeight(st::boxMaxListHeight);
	const auto select = box->setPinnedToTopContent(
		object_ptr<::Ui::MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_participant_filter()));
	const auto container = box->verticalLayout();
	const auto status = container->add(
		object_ptr<::Ui::SlideWrap<::Ui::FlatLabel>>(
			container,
			object_ptr<::Ui::FlatLabel>(
				container,
				tr::lng_agent_models_loading(tr::now),
				st::agentModelListStatus),
			st::agentModelListStatusPadding));
	const auto list = box->lifetime().make_state<QPointer<ModelList>>();
	const auto refreshStatus = [=] {
		const auto nothing = *list && (*list)->empty();
		if (nothing) {
			status->entity()->setText(tr::lng_agent_models_nothing(tr::now));
		}
		status->toggle(!*list || nothing, anim::type::instant);
	};
	const auto show = [=](std::vector<ModelInfo> models) {
		auto usable = std::vector<ModelInfo>();
		for (auto &model : models) {
			if (model.tools.value_or(true)) {
				usable.push_back(std::move(model));
			}
		}
		const auto raw = container->add(object_ptr<ModelList>(
			container,
			std::move(usable),
			currentId));
		*list = raw;
		raw->filter(select->getQuery());
		raw->chosen(
		) | rpl::on_next([=](ModelInfo model) {
			chosen(std::move(model));
			box->closeBox();
		}, raw->lifetime());
		refreshStatus();
	};
	const auto manager = &controller->session().agent();
	const auto load = std::make_shared<Fn<void()>>();
	*load = [=] {
		const auto &cache = OpenRouterModels();
		if (!cache.list.empty()
			&& crl::now() - cache.loaded < kModelsCacheTimeout) {
			show(cache.list);
			return;
		}
		box->clearButtons();
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		status->entity()->setText(tr::lng_agent_models_loading(tr::now));
		status->toggle(true, anim::type::instant);
		Fetch(
			manager,
			box->lifetime(),
			OpenAi::ModelsRequest(OpenRouterRequestSettings()),
			[=](FetchResult result) {
				auto models = (result.error.isEmpty()
					&& result.status == 200)
					? OpenAi::ParseModels(result.body)
					: std::vector<ModelInfo>();
				if (models.empty()) {
					status->entity()->setText(tr::lng_agent_models_failed(
						tr::now,
						lt_text,
						FetchErrorText(result)));
					box->clearButtons();
					box->addButton(tr::lng_agent_models_retry(), [=] {
						(*load)();
					});
					box->addButton(tr::lng_cancel(), [=] {
						box->closeBox();
					});
					return;
				}
				auto &cache = OpenRouterModels();
				cache.list = models;
				cache.loaded = crl::now();
				show(std::move(models));
			});
	};
	select->setQueryChangedCallback([=](const QString &query) {
		if (*list) {
			(*list)->filter(query);
			refreshStatus();
		}
	});
	select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		if (*list) {
			(*list)->chooseFirst();
		}
	});
	select->setCancelledCallback([=] {
		select->clearQuery();
	});
	box->setFocusCallback([=] {
		select->setInnerFocus();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	(*load)();
}

void OpenRouterKeyBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString key)> next) {
	box->setTitle(tr::lng_agent_openrouter_key_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_openrouter_key_about(
				lt_link,
				rpl::single(tr::link(
					u"openrouter.ai/keys"_q,
					u"https://openrouter.ai/keys"_q)),
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
			OpenAi::KeyRequest(OpenRouterRequestSettings(value)),
			[=](FetchResult result) {
				*checking = false;
				if (result.error.isEmpty() && result.status == 200) {
					next(value);
					box->closeBox();
				} else if (result.error.isEmpty()
					&& (result.status == 401 || result.status == 403)) {
					key->showError();
					box->uiShow()->showToast(
						tr::lng_agent_openrouter_key_wrong(tr::now));
				} else {
					box->uiShow()->showToast(tr::lng_agent_openrouter_key_failed(
						tr::now,
						lt_text,
						FetchErrorText(result)));
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

void OpenRouterNameBox(
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
			SuggestedName(model)),
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
		provider.kind = ProviderKind::OpenAiCompatible;
		provider.service = ProviderService::OpenRouter;
		provider.title = title;
		provider.baseUrl = OpenRouterBaseUrl();
		provider.apiKey = key;
		ApplyModelInfo(provider, model);
		const auto checked = makeDefault->checked();
		UpdateProviders([&](ProvidersState &state) {
			state.list.push_back(provider);
			SetServiceKey(state, ProviderService::OpenRouter, key);
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

void StartOpenRouter(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	const auto pickModel = [=](QString key) {
		controller->show(Box(
			ModelListBox,
			controller,
			QString(),
			[=](ModelInfo model) {
				controller->show(Box(
					OpenRouterNameBox,
					key,
					std::move(model),
					added));
			}));
	};
	const auto key = ServiceKey(
		LoadProviders(),
		ProviderService::OpenRouter);
	if (key.isEmpty()) {
		controller->show(Box(OpenRouterKeyBox, controller, pickModel));
	} else {
		pickModel(key);
	}
}

void RoutesBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ProviderSettings provider,
		Fn<void(std::vector<QString> ignored)> done) {
	box->setTitle(tr::lng_agent_routes_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	const auto container = box->verticalLayout();
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_routes_about(),
			st::boxLabel),
		st::boxRowPadding);
	const auto status = container->add(
		object_ptr<::Ui::SlideWrap<::Ui::FlatLabel>>(
			container,
			object_ptr<::Ui::FlatLabel>(
				container,
				tr::lng_agent_routes_loading(tr::now),
				st::agentModelListStatus),
			st::agentModelListStatusPadding));
	const auto rows = container->add(
		object_ptr<::Ui::VerticalLayout>(container));
	using Row = std::pair<QString, not_null<::Ui::Checkbox*>>;
	const auto checks = box->lifetime().make_state<std::vector<Row>>();
	Fetch(
		&controller->session().agent(),
		box->lifetime(),
		OpenAi::EndpointsRequest(provider),
		[=](FetchResult result) {
			const auto routes = (result.error.isEmpty()
				&& result.status == 200)
				? OpenAi::ParseEndpoints(result.body)
				: std::vector<ModelRoute>();
			if (routes.empty()) {
				status->entity()->setText(tr::lng_agent_routes_failed(
					tr::now,
					lt_text,
					FetchErrorText(result)));
				return;
			}
			status->toggle(false, anim::type::instant);
			::Ui::AddSkip(rows);
			for (const auto &route : routes) {
				if (!route.tools) {
					continue;
				}
				const auto checkbox = rows->add(
					object_ptr<::Ui::Checkbox>(
						rows,
						route.provider,
						!ranges::contains(
							provider.ignoredRoutes,
							route.provider),
						st::defaultBoxCheckbox),
					st::boxRowPadding + style::margins(
						0,
						st::boxLittleSkip,
						0,
						st::boxLittleSkip));
				checks->emplace_back(route.provider, checkbox);
			}
			::Ui::AddSkip(rows);
		});
	box->addButton(tr::lng_settings_save(), [=] {
		if (checks->empty()) {
			box->closeBox();
			return;
		}
		auto ignored = std::vector<QString>();
		auto checked = 0;
		for (const auto &[name, checkbox] : *checks) {
			if (checkbox->checked()) {
				++checked;
			} else {
				ignored.push_back(name);
			}
		}
		if (!checked) {
			box->uiShow()->showToast(
				tr::lng_agent_routes_none_error(tr::now));
			return;
		}
		for (const auto &name : provider.ignoredRoutes) {
			const auto listed = ranges::contains(
				*checks,
				name,
				&Row::first);
			if (!listed) {
				ignored.push_back(name);
			}
		}
		done(std::move(ignored));
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void EditOpenRouterBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ProviderSettings provider) {
	box->setTitle(tr::lng_agent_edit_provider());
	box->setWidth(st::boxWideWidth);
	const auto current = std::make_shared<ProviderSettings>(provider);
	const auto changed = box->lifetime().make_state<rpl::event_stream<>>();
	const auto value = [=](Fn<QString()> compute) {
		return rpl::single(rpl::empty) | rpl::then(
			changed->events()
		) | rpl::map(compute);
	};
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
	const auto model = Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_provider_model(),
		value([=] { return current->model; }),
		st::settingsButtonNoIcon);
	model->setClickedCallback([=] {
		controller->show(Box(
			ModelListBox,
			controller,
			current->model,
			[=](ModelInfo info) {
				if (info.id != current->model) {
					current->ignoredRoutes.clear();
				}
				ApplyModelInfo(*current, info);
				changed->fire({});
			}));
	});
	const auto routes = Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_routes(),
		value([=] { return RoutesText(*current); }),
		st::settingsButtonNoIcon);
	routes->setClickedCallback([=] {
		controller->show(Box(
			RoutesBox,
			controller,
			*current,
			[=](std::vector<QString> ignored) {
				current->ignoredRoutes = std::move(ignored);
				changed->fire({});
			}));
	});
	const auto collect = [=] {
		auto result = *current;
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
	const auto wasDefault = (provider.id == LoadProviders().defaultId);
	const auto makeDefault = container->add(
		object_ptr<::Ui::Checkbox>(
			container,
			tr::lng_agent_provider_make_active(),
			wasDefault,
			st::defaultBoxCheckbox),
		st::boxRowPadding);
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
		const auto checked = makeDefault->checked();
		UpdateProviders([&](ProvidersState &state) {
			for (auto &item : state.list) {
				if (item.id == result.id) {
					item = result;
					break;
				}
			}
			SetServiceKey(
				state,
				ProviderService::OpenRouter,
				result.apiKey);
			if (FindProvider(state, result.id)) {
				ApplyDefault(state, result.id, checked);
			}
		});
		box->closeBox();
	};
	name->submits(
	) | rpl::on_next(save, name->lifetime());
	box->addLeftButton(tr::lng_box_delete(), [=] {
		ConfirmRemove(controller, box, *current);
	});
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		name->setFocusFast();
	});
}

[[nodiscard]] QString PriceText(double price) {
	return (price > 0.) ? QString::number(price) : QString();
}

[[nodiscard]] std::optional<double> ParsePrice(QString text) {
	text = text.trimmed();
	if (text.isEmpty()) {
		return 0.;
	}
	text.replace(QChar(','), QChar('.'));
	auto seenDot = false;
	auto digits = 0;
	for (const auto &ch : text) {
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
	return digits ? std::make_optional(text.toDouble()) : std::nullopt;
}

void EditCustomBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ProviderSettings provider,
		Fn<void(QString id)> added) {
	const auto creating = provider.id.isEmpty();
	box->setTitle(creating
		? tr::lng_agent_new_provider()
		: tr::lng_agent_edit_provider());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	const auto title = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_title(),
			provider.title),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_provider_url(),
			st::boxLabel),
		st::boxRowPadding);
	const auto url = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_url_placeholder(),
			provider.baseUrl),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	const auto key = AddKeyField(container, provider.apiKey);
	::Ui::AddSkip(container);
	const auto model = container->add(
		object_ptr<::Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_agent_provider_model(),
			provider.model),
		st::boxRowPadding);
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_provider_abilities(),
			st::boxLabel),
		st::boxRowPadding);
	const auto addAbility = [&](rpl::producer<QString> text, bool checked) {
		return container->add(
			object_ptr<::Ui::Checkbox>(
				container,
				std::move(text),
				checked,
				st::defaultBoxCheckbox),
			style::margins(
				st::boxRowPadding.left(),
				st::boxLittleSkip,
				st::boxRowPadding.right(),
				st::boxLittleSkip));
	};
	const auto vision = addAbility(
		tr::lng_agent_provider_images(),
		provider.vision);
	const auto audio = addAbility(
		tr::lng_agent_provider_audio(),
		provider.audio);
	const auto video = addAbility(
		tr::lng_agent_provider_video(),
		provider.video);
	const auto files = addAbility(
		tr::lng_agent_provider_files(),
		provider.files);
	::Ui::AddSkip(container);
	::Ui::AddDividerText(
		container,
		tr::lng_agent_provider_abilities_about());
	::Ui::AddSkip(container);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_provider_prices(),
			st::boxLabel),
		st::boxRowPadding);
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
	const auto priceOutput = addPrice(
		tr::lng_agent_provider_price_output(),
		provider.priceOutput);
	const auto priceCached = addPrice(
		tr::lng_agent_provider_price_cached(),
		provider.priceCached);
	::Ui::AddDividerText(
		container,
		tr::lng_agent_provider_prices_about());
	::Ui::AddSkip(container);

	const auto state = LoadProviders();
	const auto makeDefault = container->add(
		object_ptr<::Ui::Checkbox>(
			container,
			tr::lng_agent_provider_make_active(),
			creating
				? !FindProvider(state, state.defaultId)
				: (provider.id == state.defaultId),
			st::defaultBoxCheckbox),
		st::boxRowPadding);
	::Ui::AddSkip(container);

	const auto collect = [=]() -> std::optional<ProviderSettings> {
		auto result = provider;
		result.baseUrl = url->getLastText().trimmed();
		if (!result.baseUrl.startsWith(u"http://"_q)
			&& !result.baseUrl.startsWith(u"https://"_q)) {
			url->showError();
			box->uiShow()->showToast(tr::lng_agent_error_url(tr::now));
			return std::nullopt;
		}
		result.model = model->getLastText().trimmed();
		if (result.model.isEmpty()) {
			model->showError();
			box->uiShow()->showToast(tr::lng_agent_error_model(tr::now));
			return std::nullopt;
		}
		const auto parse = [&](not_null<::Ui::InputField*> field) {
			const auto value = ParsePrice(field->getLastText());
			if (!value) {
				field->showError();
				box->uiShow()->showToast(tr::lng_agent_error_price(tr::now));
			}
			return value;
		};
		const auto input = parse(priceInput);
		const auto output = parse(priceOutput);
		const auto cached = parse(priceCached);
		if (!input || !output || !cached) {
			return std::nullopt;
		}
		result.title = title->getLastText().trimmed();
		result.apiKey = key->getLastText().trimmed();
		result.service = ProviderService::Custom;
		result.vision = vision->checked();
		result.audio = audio->checked();
		result.video = video->checked();
		result.files = files->checked();
		result.priceInput = *input;
		result.priceOutput = *output;
		result.priceCached = *cached;
		return result;
	};
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_check_button(),
		st::settingsButtonLightNoIcon
	)->setClickedCallback([=] {
		if (const auto result = collect()) {
			ShowCheckBox(controller, *result);
		}
	});
	::Ui::AddSkip(container);

	const auto save = [=] {
		auto result = collect();
		if (!result) {
			return;
		}
		if (creating) {
			result->id = GenerateProviderId();
		}
		const auto checked = makeDefault->checked();
		UpdateProviders([&](ProvidersState &state) {
			auto found = false;
			for (auto &item : state.list) {
				if (item.id == result->id) {
					item = *result;
					found = true;
					break;
				}
			}
			if (!found && creating) {
				state.list.push_back(*result);
				found = true;
			}
			if (found) {
				ApplyDefault(state, result->id, checked);
			}
		});
		const auto id = result->id;
		box->closeBox();
		if (creating && added) {
			added(id);
		}
	};
	if (!creating) {
		box->addLeftButton(tr::lng_box_delete(), [=] {
			ConfirmRemove(controller, box, provider);
		});
	}
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		title->setFocusFast();
	});
}

void AddModelBox(
		not_null<::Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	box->setTitle(tr::lng_agent_add_model_title());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	::Ui::AddSkip(container);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_add_openrouter(),
		st::settingsButton,
		{ &st::agentModelChoiceOpenRouterIcon }
	)->setClickedCallback([=] {
		box->closeBox();
		StartOpenRouter(controller, added);
	});
	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_add_custom(),
		st::settingsButton,
		{ &st::agentModelChoiceCustomIcon }
	)->setClickedCallback([=] {
		box->closeBox();
		controller->show(Box(
			EditCustomBox,
			controller,
			ProviderSettings(),
			added));
	});
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_add_model_about());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

QString ModelTitle(const ProviderSettings &provider) {
	return !provider.title.isEmpty()
		? provider.title
		: !provider.model.isEmpty()
		? provider.model
		: tr::lng_agent_provider_default(tr::now);
}

QString ModelDescription(const ProviderSettings &provider) {
	const auto where = (provider.service == ProviderService::OpenRouter)
		? tr::lng_agent_service_openrouter(tr::now)
		: QUrl(provider.baseUrl).host();
	return where.isEmpty()
		? provider.model
		: provider.model.isEmpty()
		? where
		: (where + u" · "_q + provider.model);
}

void ShowAddModel(
		not_null<Window::SessionController*> controller,
		Fn<void(QString id)> added) {
	controller->show(Box(AddModelBox, controller, std::move(added)));
}

void ShowEditModel(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	const auto state = LoadProviders();
	const auto provider = FindProvider(state, id);
	if (!provider) {
		return;
	} else if (provider->service == ProviderService::OpenRouter) {
		controller->show(Box(EditOpenRouterBox, controller, *provider));
	} else {
		controller->show(Box(
			EditCustomBox,
			controller,
			*provider,
			Fn<void(QString)>()));
	}
}

} // namespace Agent::Ui
