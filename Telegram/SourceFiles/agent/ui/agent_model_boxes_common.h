/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_openai.h"
#include "agent/core/agent_providers.h"

namespace Ui {
class GenericBox;
class PasswordInput;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
} // namespace Agent

namespace Agent::Ui::details {

using ModelInfo = OpenAi::ModelInfo;

struct FetchResult {
	int status = 0;
	QByteArray body;
	QString error;
};

struct ModelSource {
	Fn<void(
		not_null<::Ui::GenericBox*> box,
		Fn<void(std::vector<ModelInfo> models, QString error)> done)> load;
	Fn<QString(const ModelInfo&)> describe;
	Fn<bool(const ModelInfo&)> usable;
};

void Fetch(
	not_null<Manager*> manager,
	rpl::lifetime &lifetime,
	HttpRequest request,
	Fn<void(FetchResult)> done);
[[nodiscard]] QString FetchErrorText(
	const FetchResult &result,
	Fn<Error(const HttpHead&, const QByteArray&)> parse = nullptr);

[[nodiscard]] QString FormatContext(int tokens);
[[nodiscard]] QString FormatPrice(double price);
[[nodiscard]] QString ModelInfoDescription(const ModelInfo &model);

[[nodiscard]] not_null<::Ui::PasswordInput*> AddKeyField(
	not_null<::Ui::VerticalLayout*> container,
	const QString &value);
void RemoveProfile(const QString &id);
void ConfirmRemove(
	not_null<Window::SessionController*> controller,
	not_null<::Ui::GenericBox*> box,
	const ProviderSettings &provider);
void ApplyDefault(
	ProvidersState &state,
	const QString &id,
	bool makeDefault);

void ModelListBox(
	not_null<::Ui::GenericBox*> box,
	QString currentId,
	ModelSource source,
	Fn<void(ModelInfo)> chosen);

} // namespace Agent::Ui::details
