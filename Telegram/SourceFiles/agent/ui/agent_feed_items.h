/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_feed.h"
#include "base/timer.h"
#include "ui/effects/radial_animation.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

#include <memory>
#include <optional>

namespace Iv::Markdown {
class MathRenderer;
class MarkdownArticle;
class MarkdownDocumentWidget;
} // namespace Iv::Markdown

namespace Window {
class SessionController;
} // namespace Window

namespace Agent::Ui {

using namespace ::Ui;

struct FeedContext {
	not_null<Window::SessionController*> controller;
	std::shared_ptr<Iv::Markdown::MathRenderer> renderer;
	Fn<void(int pixels)> scrollBy;

};

class FeedItemWidget : public RpWidget {
public:
	using RpWidget::RpWidget;

	virtual void updatePayload(const FeedItem &item) {
	}

};

class BubbleWidget final : public FeedItemWidget {
public:
	BubbleWidget(
		QWidget *parent,
		const style::FlatLabel &st,
		style::color bg,
		int radius,
		const style::margins &padding,
		int maxWidth);

	BubbleWidget *setText(const QString &text);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void refreshBubble();

	const style::FlatLabel &_st;
	style::color _bg;
	int _radius = 0;
	style::margins _padding;
	int _maxWidth = 0;
	object_ptr<Ui::FlatLabel> _label;

};

class FeedAssistantWidget final : public FeedItemWidget {
public:
	FeedAssistantWidget(QWidget *parent, FeedContext context, QString text);
	~FeedAssistantWidget();

	void streamDelta(QString delta);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void renderNow();
	void showFallback(const QString &text);

	FeedContext _context;
	QString _source;
	std::shared_ptr<Iv::Markdown::MarkdownArticle> _article;
	object_ptr<Iv::Markdown::MarkdownDocumentWidget> _body;
	object_ptr<Ui::FlatLabel> _fallback;
	base::Timer _renderTimer;
	bool _renderScheduled = false;

};

class FeedThinkingWidget final : public FeedItemWidget {
public:
	FeedThinkingWidget(QWidget *parent, QString text);

	void setText(QString text);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void refresh(bool toggle = false);

	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _text;
	bool _expanded = false;

};

class FeedToolWidget final : public FeedItemWidget {
public:
	FeedToolWidget(QWidget *parent, const FeedTool &tool);

	void updatePayload(const FeedItem &item) override;
	void showFinished(ToolResultBlock result);

protected:
	int resizeGetHeight(int newWidth) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void applyResult(std::optional<ToolResultBlock> result);
	void refresh();

	QString _callId;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _state;
	object_ptr<Ui::FlatLabel> _argumentsHeader;
	object_ptr<Ui::FlatLabel> _arguments;
	object_ptr<Ui::FlatLabel> _resultHeader;
	object_ptr<Ui::FlatLabel> _result;
	bool _expanded = false;
	bool _running = true;
	bool _failed = false;

};

class FeedSummaryWidget final : public FeedItemWidget {
public:
	FeedSummaryWidget(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _label;

};

class FeedStatusWidget final : public RpWidget {
public:
	enum class Kind {
		None,
		Working,
		Compacting,
		Failed,
		TurnEnd,
	};

	FeedStatusWidget(QWidget *parent);

	void showStatus(Kind kind, QString text);
	[[nodiscard]] rpl::producer<> retryClicks() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void relayout();

	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::RoundButton> _retry;
	Ui::InfiniteRadialAnimation _animation;
	Kind _kind = Kind::None;
	rpl::event_stream<> _retryClicks;

};

} // namespace Agent::Ui