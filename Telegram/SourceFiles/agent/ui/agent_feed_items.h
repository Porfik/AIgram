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

[[nodiscard]] object_ptr<RpWidget> MakeCodeBlock(
	QWidget *parent,
	const QString &text);

} // namespace Agent::Ui
