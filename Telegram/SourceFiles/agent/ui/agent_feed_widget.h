/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/agent_session.h"
#include "agent/core/agent_engine.h"
#include "agent/core/agent_feed.h"
#include "agent/ui/agent_feed_items.h"
#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"

namespace Agent::Ui {

class FeedWidget final : public RpWidget {
public:
	FeedWidget(QWidget *parent, FeedContext context);

	void rebuild(const std::vector<FeedItem> &items);
	void handleLiveEvent(const EngineEvent &event);
	void setStatus(const SessionState &state);
	void setContentMaxWidth(int width);
	void scrollToBottom();
	[[nodiscard]] bool empty() const;
	[[nodiscard]] rpl::producer<> retryRequests() const;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Row {
		QString key;
		FeedItemWidget *widget = nullptr;
	};

	void relayout();
	void dropLive();
	void refreshActive();
	void updateStick();
	void applyStick();
	void freezeStick();
	[[nodiscard]] FeedItemWidget *tail() const;
	[[nodiscard]] FeedStepsWidget *liveSteps();
	[[nodiscard]] object_ptr<FeedItemWidget> makeItemWidget(
		const FeedItem &item);
	FeedItemWidget *addWidget(
		object_ptr<FeedItemWidget> widget,
		const FeedItem &item);

	object_ptr<ScrollArea> _scroll;
	not_null<VerticalLayout*> _list;
	object_ptr<FeedStatusWidget> _status;
	FeedContext _context;
	std::vector<Row> _rows;
	std::vector<FeedItemWidget*> _live;
	FeedAssistantWidget *_liveAssistant = nullptr;
	base::Timer _unfreezeStick;
	int _contentMaxWidth = 0;
	bool _working = false;
	bool _stick = true;
	bool _stickFrozen = false;
	rpl::event_stream<> _retryRequests;

};

} // namespace Agent::Ui
