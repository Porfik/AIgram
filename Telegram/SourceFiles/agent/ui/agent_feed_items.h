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
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

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

class ToggleRow;

struct FeedDelegate {
	Fn<void(const QString &toolUseId, CardDecision decision)> decideCard;
	Fn<bool(const QString &toolUseId)> canUndo;
	Fn<bool(const QString &toolUseId)> isUndone;
	Fn<void(const std::vector<QString> &toolUseIds)> undo;
	Fn<void(const QString &text)> startHandoff;
	Fn<std::optional<MediaBlock>(const QString &attachment)> attachment;
	Fn<bool()> isRun;

};

struct FeedContext {
	not_null<Window::SessionController*> controller;
	std::shared_ptr<Iv::Markdown::MathRenderer> renderer;
	Fn<void(int pixels)> scrollBy;
	FeedDelegate delegate;

};

class FeedItemWidget : public RpWidget {
public:
	using RpWidget::RpWidget;

	virtual void updatePayload(const FeedItem &item) {
	}
	[[nodiscard]] rpl::producer<> manualToggles() const {
		return _manualToggles.events();
	}

protected:
	void notifyManualToggle() {
		_manualToggles.fire({});
	}

private:
	rpl::event_stream<> _manualToggles;

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

class FeedStepsWidget final : public FeedItemWidget {
public:
	FeedStepsWidget(
		QWidget *parent,
		FeedContext context,
		const FeedSteps &steps);
	~FeedStepsWidget();

	void updatePayload(const FeedItem &item) override;
	void appendThinking(const QString &text);
	void appendTool(const QString &id, const QString &name);
	bool applyResult(const ToolResultBlock &result);
	void setActive(bool active);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	class Step;
	struct Entry {
		QString key;
		Step *step = nullptr;
	};

	void applySteps(const std::vector<FeedStep> &steps);
	void addStep(QString key, const FeedStep &step);
	void refreshHeader();
	void toggleExpanded();

	FeedContext _context;
	VerticalLayout *_content = nullptr;
	ToggleRow *_header = nullptr;
	SlideWrap<VerticalLayout> *_list = nullptr;
	std::vector<Entry> _entries;
	int _liveThinking = 0;
	bool _expanded = false;
	bool _active = false;

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
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::RoundButton> _retry;
	Ui::InfiniteRadialAnimation _animation;
	Kind _kind = Kind::None;
	rpl::event_stream<> _retryClicks;

};

} // namespace Agent::Ui
