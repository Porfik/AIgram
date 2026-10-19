/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_widget.h"

#include "base/variant.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"

#include "styles/style_agent.h"

#include <map>

namespace Agent::Ui {
namespace {

[[nodiscard]] std::vector<QString> ComputeKeys(
		const std::vector<FeedItem> &items) {
	auto result = std::vector<QString>();
	result.reserve(items.size());
	auto counts = std::map<std::pair<int, int>, int>();
	for (const auto &item : items) {
		const auto kind = int(item.index());
		const auto index = v::match(item,
			[](const FeedUser &data) { return data.index; },
			[](const FeedAssistant &data) { return data.index; },
			[](const FeedSteps &data) { return data.index; },
			[](const FeedSummary &data) { return data.until; });
		result.push_back(u"%1:%2:%3"_q.arg(kind).arg(index).arg(
			counts[{ index, kind }]++));
	}
	return result;
}

[[nodiscard]] FeedStepsWidget *AsSteps(FeedItemWidget *widget) {
	return dynamic_cast<FeedStepsWidget*>(widget);
}

} // namespace

FeedWidget::FeedWidget(QWidget *parent, FeedContext context)
: RpWidget(parent)
, _scroll(this, st::defaultScrollArea)
, _list(_scroll->setOwnedWidget(object_ptr<VerticalLayout>(_scroll.data())))
, _status(this)
, _context(std::move(context))
, _unfreezeStick([=] {
	_stickFrozen = false;
	updateStick();
}) {
	if (!_context.scrollBy) {
		_context.scrollBy = [=](int pixels) {
			_scroll->scrollToY(_scroll->scrollTop() + pixels);
		};
	}
	_scroll->scrollTopChanges(
	) | rpl::on_next([=] {
		updateStick();
	}, lifetime());
	_list->heightValue(
	) | rpl::on_next([=] {
		applyStick();
	}, lifetime());
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_list->heightValue()
	) | rpl::on_next([=](int top, int height, int) {
		_list->setVisibleTopBottom(top, top + height);
	}, lifetime());
	_status->retryClicks(
	) | rpl::on_next([=] {
		_retryRequests.fire({});
	}, lifetime());
}

rpl::producer<> FeedWidget::retryRequests() const {
	return _retryRequests.events();
}

object_ptr<FeedItemWidget> FeedWidget::makeItemWidget(
		const FeedItem &item) {
	using Result = object_ptr<FeedItemWidget>;
	return v::match(item,
		[&](const FeedUser &data) -> Result {
			auto result = object_ptr<BubbleWidget>(
				_list,
				st::agentFeedUserLabel,
				st::agentFeedUserBubbleBg,
				st::agentFeedUserRadius,
				st::agentFeedUserPadding,
				st::agentFeedMaxTextWidth);
			result->setText(data.text);
			return result;
		},
		[&](const FeedAssistant &data) -> Result {
			return object_ptr<FeedAssistantWidget>(_list, _context, data.text);
		},
		[&](const FeedSteps &data) -> Result {
			return object_ptr<FeedStepsWidget>(_list, data);
		},
		[&](const FeedSummary &data) -> Result {
			return object_ptr<FeedSummaryWidget>(_list);
		});
}

FeedItemWidget *FeedWidget::addWidget(
		object_ptr<FeedItemWidget> widget,
		const FeedItem &item) {
	const auto user = std::holds_alternative<FeedUser>(item);
	const auto result = _list->add(
		std::move(widget),
		user ? st::agentFeedUserMargin : st::agentFeedItemMargin,
		user ? style::al_topright : style::al_left);
	result->manualToggles(
	) | rpl::on_next([=] {
		freezeStick();
	}, result->lifetime());
	return result;
}

void FeedWidget::rebuild(const std::vector<FeedItem> &items) {
	dropLive();
	const auto keys = ComputeKeys(items);
	const auto keep = int(std::min(_rows.size(), keys.size()));
	auto same = 0;
	for (; same != keep; ++same) {
		if (_rows[same].key != keys[same]) {
			break;
		}
		_rows[same].widget->updatePayload(items[same]);
	}
	for (auto i = same; i != int(_rows.size()); ++i) {
		delete _rows[i].widget;
	}
	_rows.resize(same);
	for (auto i = same; i != int(items.size()); ++i) {
		_rows.push_back({
			keys[i],
			addWidget(makeItemWidget(items[i]), items[i]),
		});
	}
	refreshActive();
}

FeedItemWidget *FeedWidget::tail() const {
	return !_live.empty()
		? _live.back()
		: !_rows.empty()
		? _rows.back().widget
		: nullptr;
}

FeedStepsWidget *FeedWidget::liveSteps() {
	if (const auto steps = AsSteps(tail())) {
		return steps;
	}
	const auto item = FeedItem(FeedSteps{ .index = -1 });
	const auto result = AsSteps(addWidget(makeItemWidget(item), item));
	_live.push_back(result);
	return result;
}

void FeedWidget::handleLiveEvent(const EngineEvent &event) {
	v::match(event,
		[&](const TextDelta &data) {
			if (!_liveAssistant || tail() != _liveAssistant) {
				const auto item = FeedItem(FeedAssistant{ .index = -1 });
				_liveAssistant = static_cast<FeedAssistantWidget*>(
					addWidget(makeItemWidget(item), item));
				_live.push_back(_liveAssistant);
			}
			_liveAssistant->streamDelta(data.text);
		},
		[&](const ThinkingDelta &data) {
			liveSteps()->appendThinking(data.text);
		},
		[&](const ToolUseStarted &data) {
			liveSteps()->appendTool(data.id, data.name);
		},
		[&](const ToolCallFinished &data) {
			for (const auto &row : _rows) {
				const auto steps = AsSteps(row.widget);
				if (steps && steps->applyResult(data.result)) {
					return;
				}
			}
			for (const auto widget : _live) {
				const auto steps = AsSteps(widget);
				if (steps && steps->applyResult(data.result)) {
					return;
				}
			}
		},
		[](const auto &) {
		});
	refreshActive();
}

void FeedWidget::refreshActive() {
	const auto last = tail();
	const auto apply = [&](FeedItemWidget *widget) {
		if (const auto steps = AsSteps(widget)) {
			steps->setActive(_working && (widget == last));
		}
	};
	for (const auto &row : _rows) {
		apply(row.widget);
	}
	for (const auto widget : _live) {
		apply(widget);
	}
}

void FeedWidget::setStatus(const SessionState &state) {
	using Kind = FeedStatusWidget::Kind;
	_working = (state.status == SessionStatus::Working)
		|| (state.status == SessionStatus::Compacting);
	if (state.status == SessionStatus::Working) {
		_status->showStatus(Kind::Working, tr::lng_agent_working(tr::now));
	} else if (state.status == SessionStatus::Compacting) {
		_status->showStatus(
			Kind::Compacting,
			tr::lng_agent_compacting(tr::now));
	} else if (state.status == SessionStatus::Failed) {
		const auto error = state.error.has_value()
			? (!state.error->text.isEmpty()
				? state.error->text
				: ErrorKindName(state.error->kind))
			: QString();
		_status->showStatus(
			Kind::Failed,
			tr::lng_agent_failed(tr::now, lt_error, error));
	} else if (state.lastEnd.has_value()
		&& *state.lastEnd != TurnEnd::Completed) {
		auto text = QString();
		switch (*state.lastEnd) {
		case TurnEnd::Interrupted:
			text = tr::lng_agent_turn_interrupted(tr::now);
			break;
		case TurnEnd::StepLimit:
			text = tr::lng_agent_turn_step_limit(tr::now);
			break;
		case TurnEnd::MaxTokens:
			text = tr::lng_agent_turn_max_tokens(tr::now);
			break;
		case TurnEnd::Refusal:
			text = tr::lng_agent_turn_refusal(tr::now);
			break;
		case TurnEnd::Completed:
		case TurnEnd::Failed:
			break;
		}
		if (!text.isEmpty()) {
			_status->showStatus(Kind::TurnEnd, std::move(text));
		} else {
			_status->hide();
		}
	} else {
		_status->hide();
	}
	refreshActive();
	relayout();
}

void FeedWidget::scrollToBottom() {
	_stickFrozen = false;
	_unfreezeStick.cancel();
	_stick = true;
	applyStick();
}

bool FeedWidget::empty() const {
	return _rows.empty() && _live.empty();
}

void FeedWidget::updateStick() {
	if (!_stickFrozen) {
		_stick = (_scroll->scrollTop() + st::agentFeedBottomSkip
			>= _scroll->scrollTopMax());
	}
}

void FeedWidget::applyStick() {
	if (_stick && !_stickFrozen) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

void FeedWidget::freezeStick() {
	_stickFrozen = true;
	_unfreezeStick.callOnce(st::slideWrapDuration * 2);
}

void FeedWidget::dropLive() {
	for (const auto widget : base::take(_live)) {
		delete widget;
	}
	_liveAssistant = nullptr;
}

void FeedWidget::relayout() {
	const auto width = this->width();
	if (!_status->isHidden()) {
		_status->resizeToWidth(width);
	}
	const auto statusHeight = _status->isHidden() ? 0 : _status->height();
	_status->moveToLeft(0, height() - statusHeight, width);
	_scroll->setGeometry(0, 0, width, height() - statusHeight);
	_list->resizeToWidth(width);
	applyStick();
}

void FeedWidget::resizeEvent(QResizeEvent *e) {
	relayout();
}

} // namespace Agent::Ui
