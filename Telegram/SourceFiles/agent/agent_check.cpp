/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_check.h"

#include "agent/agent_http_transport.h"
#include "agent/core/agent_engine.h"
#include "agent/core/agent_loop.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <type_traits>
#include <utility>

#include "styles/style_layers.h"

namespace Agent {
namespace {

class TestExecutor final : public ToolExecutor {
public:
	[[nodiscard]] std::vector<ToolSpec> tools() const override {
		return { {
			.name = u"get_current_time"_q,
			.description = u"Returns the current local date and time."_q,
			.inputSchema = QJsonObject{
				{ u"type"_q, u"object"_q },
				{ u"properties"_q, QJsonObject() },
			},
		} };
	}

	void execute(ToolUseBlock call, Fn<void(ToolResultBlock)> done) override {
		done(ToolResultBlock{
			.toolUseId = call.id,
			.content = QDateTime::currentDateTime().toString(Qt::ISODate),
		});
	}

	void cancel(const QString &) override {
	}

};

struct CheckState {
	HttpTransport transport;
	TestExecutor executor;
	std::unique_ptr<Loop> loop;
	QString log;

};

void AppendLine(CheckState &state, QString line) {
	auto &log = state.log;
	if (!log.isEmpty() && !log.endsWith('\n')) {
		log += '\n';
	}
	log += std::move(line) + '\n';
}

void HandleEvent(
		CheckState &state,
		const EngineEvent &event) {
	std::visit([&](const auto &data) {
		using Type = std::decay_t<decltype(data)>;
		if constexpr (std::is_same_v<Type, TextDelta>) {
			state.log += data.text;
		} else if constexpr (std::is_same_v<Type, ToolCallRequested>) {
			AppendLine(state, tr::lng_agent_check_calling(
				tr::now,
				lt_name,
				data.call.name));
		} else if constexpr (std::is_same_v<Type, ToolCallFinished>) {
			AppendLine(state, data.result.error
				? tr::lng_agent_check_result_error(
					tr::now,
					lt_text,
					data.result.content)
				: tr::lng_agent_check_result(
					tr::now,
					lt_text,
					data.result.content));
		} else if constexpr (std::is_same_v<Type, UsageReported>) {
			AppendLine(state, data.usage.cachedInputTokens > 0
				? tr::lng_agent_check_usage_cached(
					tr::now,
					lt_input,
					QString::number(data.usage.inputTokens),
					lt_cached,
					QString::number(data.usage.cachedInputTokens),
					lt_output,
					QString::number(data.usage.outputTokens))
				: tr::lng_agent_check_usage(
					tr::now,
					lt_input,
					QString::number(data.usage.inputTokens),
					lt_output,
					QString::number(data.usage.outputTokens)));
		} else if constexpr (std::is_same_v<Type, TurnFinished>) {
			if (data.end == TurnEnd::Completed) {
				AppendLine(state, tr::lng_agent_check_success(tr::now));
			} else if (data.error) {
				AppendLine(state, data.error->text.isEmpty()
					? ErrorKindName(data.error->kind)
					: ErrorKindName(data.error->kind)
						+ u": "_q
						+ data.error->text);
			} else if (data.end == TurnEnd::Interrupted) {
				AppendLine(state, tr::lng_agent_check_interrupted(tr::now));
			} else {
				AppendLine(state, tr::lng_agent_check_finished(tr::now));
			}
		}
	}, event);
}

void AgentCheckBox(not_null<Ui::GenericBox*> box, ProviderSettings provider) {
	box->setTitle(tr::lng_agent_check_title());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	Ui::AddSkip(container);

	const auto state = box->lifetime().make_state<CheckState>();

	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(container, st::boxLabel),
		st::boxRowPadding);
	Ui::AddSkip(container);

	const auto update = [=] {
		label->setText(state->log);
	};
	container->heightValue(
	) | rpl::on_next([=](int height) {
		box->scrollToY(height);
	}, container->lifetime());

	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});

	if (auto adapter = CreateAdapter(provider)) {
		state->loop = std::make_unique<Loop>(
			std::move(adapter),
			&state->transport,
			&state->executor,
			LoopOptions{ .model = provider.model });
		state->loop->events(
		) | rpl::on_next([=](EngineEvent event) {
			HandleEvent(*state, event);
			update();
		}, box->lifetime());
		auto message = Message();
		message.role = Role::User;
		message.blocks.push_back(TextBlock{
			.text = tr::lng_agent_check_prompt(tr::now),
		});
		state->loop->send(std::move(message));
	} else {
		AppendLine(*state, tr::lng_agent_check_unsupported(tr::now));
		update();
	}
}

} // namespace

void ShowCheckBox(
		not_null<Window::SessionController*> controller,
		ProviderSettings provider) {
	controller->show(Box(AgentCheckBox, provider));
}

} // namespace Agent
