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
#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"

namespace Agent::Ui {

class FeedWidget final : public RpWidget {
public:
	FeedWidget(QWidget *parent, FeedContext context);

	void rebuild(const std::vector<FeedItem> &items);
	void handleLiveEvent(const EngineEvent &event);
	void setStatus(const SessionState &state);
	void scrollToBottom();
	[[nodiscard]] rpl::producer<> retryRequests() const;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	class List;

	void relayout();
	void relayoutInner();
	void dropLive();
	[[nodiscard]] object_ptr<FeedItemWidget> makeItemWidget(
		const FeedItem &item);

	struct Row {
		QString key;
		FeedItemWidget *widget = nullptr;
	};

	object_ptr<Ui::ScrollArea> _scroll;
	List *_list = nullptr;
	object_ptr<FeedStatusWidget> _status;
	FeedContext _context;
	std::vector<Row> _rows;
	FeedAssistantWidget *_liveAssistant = nullptr;
	FeedThinkingWidget *_liveThinking = nullptr;
	QString _liveThinkingBuffer;
	base::flat_map<QString, FeedToolWidget*> _liveTools;
	rpl::event_stream<> _retryRequests;

};

} // namespace Agent::Ui