/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_widget.h"

#include "base/variant.h"
#include "lang/lang_keys.h"

#include "styles/style_agent.h"

#include <map>

namespace Agent::Ui {

class FeedWidget::List final : public RpWidget {
public:
	List(QWidget *parent = nullptr);

	template <typename Widget>
	Widget *append(
		object_ptr<Widget> widget,
		const style::margins &margin,
		style::align align);
	void remove(not_null<FeedItemWidget*> widget);
	void clear();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	struct Entry {
		object_ptr<FeedItemWidget> widget;
		style::margins margin;
		style::align align = style::al_left;
	};

	void refresh();

	std::vector<Entry> _entries;

};

FeedWidget::List::List(QWidget *parent) : RpWidget(parent) {
}

template <typename Widget>
Widget *FeedWidget::List::append(
		object_ptr<Widget> widget,
		const style::margins &margin,
		style::align align) {
	const auto raw = widget.data();
	_entries.push_back({ std::move(widget), margin, align });
	refresh();
	return raw;
}

void FeedWidget::List::remove(not_null<FeedItemWidget*> widget) {
	auto i = ranges::find_if(_entries, [&](const Entry &entry) {
		return entry.widget.data() == widget.get();
	});
	if (i == end(_entries)) {
		return;
	}
	_entries.erase(i);
	refresh();
}

void FeedWidget::List::clear() {
	_entries.clear();
	refresh();
}

void FeedWidget::List::refresh() {
	resize(width(), resizeGetHeight(width()));
}

int FeedWidget::List::resizeGetHeight(int newWidth) {
	auto top = st::agentFeedContentPadding.top();
	for (auto &entry : _entries) {
		const auto &margin = entry.margin;
		const auto available = newWidth - margin.left() - margin.right();
		if (available > 0) {
			entry.widget->resizeToNaturalWidth(available);
		}
		const auto x = (entry.align == style::al_right)
			? (newWidth - margin.right() - entry.widget->width())
			: margin.left();
		entry.widget->move(x, top + margin.top());
		top += margin.top() + entry.widget->height() + margin.bottom();
	}
	return top + st::agentFeedContentPadding.bottom();
}

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
			[](const FeedThinking &data) { return data.index; },
			[](const FeedTool &data) { return data.index; },
			[](const FeedSummary &data) { return -1; });
		result.push_back(u"%1:%2:%3"_q.arg(kind).arg(index).arg(
			counts[{ index, kind }]++));
	}
	return result;
}

} // namespace

FeedWidget::FeedWidget(QWidget *parent, FeedContext context)
: RpWidget(parent)
, _scroll(this, st::defaultScrollArea)
, _status(this)
, _context(std::move(context)) {
	if (!_context.scrollBy) {
		_context.scrollBy = [=](int pixels) {
			_scroll->scrollToY(_scroll->scrollTop() + pixels);
		};
	}
	auto list = object_ptr<List>(nullptr);
	_list = list.data();
	_scroll->setOwnedWidget(std::move(list));
	Ui::SetStickyBottomScroll(_scroll.get(), _list);
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
		[&](const FeedThinking &data) -> Result {
			return object_ptr<FeedThinkingWidget>(_list, data.text);
		},
		[&](const FeedTool &data) -> Result {
			return object_ptr<FeedToolWidget>(_list, data);
		},
		[&](const FeedSummary &data) -> Result {
			return object_ptr<FeedSummaryWidget>(_list);
		});
}

void FeedWidget::rebuild(const std::vector<FeedItem> &items) {
	dropLive();
	const auto keys = ComputeKeys(items);
	const auto keep = std::min(_rows.size(), keys.size());
	auto same = 0;
	for (; same < keep; ++same) {
		if (_rows[same].key != keys[same]) {
			break;
		}
		_rows[same].widget->updatePayload(items[same]);
	}
	for (auto j = int(_rows.size()) - 1; j >= same; --j) {
		_list->remove(_rows[j].widget);
		_rows[j].widget = nullptr;
	}
	_rows.resize(same);
	for (auto i = same; i < int(keys.size()); ++i) {
		auto widget = makeItemWidget(items[i]);
		_rows.push_back({ keys[i], widget.data() });
		_list->append(
			std::move(widget),
			st::agentFeedItemMargin,
			std::holds_alternative<FeedUser>(items[i])
				? style::al_right
				: style::al_left);
	}
	relayoutInner();
}

void FeedWidget::handleLiveEvent(const EngineEvent &event) {
	v::match(event,
		[&](const TextDelta &data) {
			if (!_liveAssistant) {
				_liveAssistant = _list->append(
					object_ptr<FeedAssistantWidget>(
						_list,
						_context,
						QString()),
					st::agentFeedItemMargin,
					style::al_left);
				relayoutInner();
			}
			_liveAssistant->streamDelta(data.text);
		},
		[&](const ThinkingDelta &data) {
			if (!_liveThinking) {
				_liveThinking = _list->append(
					object_ptr<FeedThinkingWidget>(_list, QString()),
					st::agentFeedItemMargin,
					style::al_left);
				relayoutInner();
			}
			_liveThinkingBuffer += data.text;
			_liveThinking->setText(_liveThinkingBuffer);
		},
		[&](const ToolUseStarted &data) {
			_liveAssistant = nullptr;
			_liveThinking = nullptr;
			auto widget = object_ptr<FeedToolWidget>(_list, FeedTool{
				.index = -1,
				.call = ToolUseBlock{
					.id = data.id,
					.name = data.name,
				},
				.result = std::nullopt,
			});
			_liveTools.emplace(data.id, widget.data());
			_list->append(
				std::move(widget),
				st::agentFeedItemMargin,
				style::al_left);
			relayoutInner();
		},
		[&](const ToolCallFinished &data) {
			if (const auto widget = _liveTools.take(data.result.toolUseId)) {
				(*widget)->showFinished(data.result);
			}
		},
		[](const auto &) {
		});
}

void FeedWidget::setStatus(const SessionState &state) {
	using Kind = FeedStatusWidget::Kind;
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
	relayout();
}

void FeedWidget::scrollToBottom() {
	_scroll->scrollToY(_list->height(), _list->height());
}

void FeedWidget::dropLive() {
	if (_liveAssistant) {
		_list->remove(_liveAssistant);
		_liveAssistant = nullptr;
	}
	if (_liveThinking) {
		_list->remove(_liveThinking);
		_liveThinking = nullptr;
	}
	for (const auto &[id, widget] : base::take(_liveTools)) {
		_list->remove(widget);
	}
	_liveTools.clear();
	_liveThinkingBuffer = QString();
}

void FeedWidget::relayout() {
	const auto statusHeight = _status->isHidden()
		? 0
		: _status->height();
	_status->setGeometry(
		0,
		this->height() - statusHeight,
		this->width(),
		statusHeight);
	_scroll->setGeometry(
		0,
		0,
		this->width(),
		this->height() - statusHeight);
	relayoutInner();
}

void FeedWidget::relayoutInner() {
	const auto w = _scroll->width();
	if (w <= 0) {
		return;
	}
	_list->resizeToWidth(w);
}

void FeedWidget::resizeEvent(QResizeEvent *e) {
	relayout();
}

} // namespace Agent::Ui