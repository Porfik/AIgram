/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_chats_row.h"

#include "agent/agent_manager.h"
#include "agent/ui/agent_section.h"
#include "base/flat_set.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text.h"
#include "ui/unread_badge_paint.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_general.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

#include <rpl/rpl.h>

namespace Agent::Ui {
namespace {

struct GeneralShownState {
	base::flat_set<not_null<Window::SessionController*>> shown;
	rpl::event_stream<not_null<Window::SessionController*>> changes;
};

GeneralShownState &ShownState() {
	static auto result = GeneralShownState();
	return result;
}

class RowWidget final : public ::Ui::RippleButton {
public:
	RowWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setShown(bool shown);
	void setActivity(ScopeActivity activity);
	void setScreenShown(bool shown);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void updateStatus();

	const not_null<Window::SessionController*> _controller;
	bool _shown = false;
	bool _screenShown = false;
	bool _narrow = false;
	bool _working = false;
	int _pending = 0;
	::Ui::Text::String _title;
	::Ui::Text::String _status;

};

RowWidget::RowWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: ::Ui::RippleButton(parent, st::dialogsRipple)
, _controller(controller) {
	_title.setText(st::semiboldTextStyle, tr::lng_agent_row_title(tr::now));
	updateStatus();
	resizeToWidth(width());
}

void RowWidget::updateStatus() {
	const auto count = _pending;
	_status.setText(
		st::dialogsTextStyle,
		_working
			? tr::lng_agent_row_working(tr::now)
			: count
			? tr::lng_agent_row_pending(tr::now, lt_count, count)
			: tr::lng_agent_row_about(tr::now));
}

void RowWidget::setShown(bool shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	setVisible(shown);
	resizeToWidth(width());
}

void RowWidget::setActivity(ScopeActivity activity) {
	if (_working == activity.working && _pending == activity.pendingCards) {
		return;
	}
	_working = activity.working;
	_pending = activity.pendingCards;
	updateStatus();
	update();
}

void RowWidget::setScreenShown(bool shown) {
	if (_screenShown == shown) {
		return;
	}
	_screenShown = shown;
	update();
}

void RowWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto active = _screenShown;
	const auto selected = isOver() && !active;
	p.fillRect(e->rect(), active ? st::dialogsBgActive : st::dialogsBg);
	if (!active) {
		paintRipple(p, 0, 0);
	}
	const auto &st = st::defaultDialogRow;
	const auto nameFg = active
		? st::dialogsNameFgActive
		: selected
		? st::dialogsNameFgOver
		: st::dialogsNameFg;
	const auto textFg = active
		? st::dialogsTextFgActive
		: selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg;

	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgActive);
	p.drawEllipse(
		st.padding.left(),
		st.padding.top(),
		st.photoSize,
		st.photoSize);
	st::agentChatsRowIcon.paint(
		p,
		QPoint(
			st.padding.left()
				+ (st.photoSize - st::agentChatsRowIcon.width()) / 2,
			st.padding.top()
				+ (st.photoSize - st::agentChatsRowIcon.height()) / 2),
		width());

	if (_narrow) {
		if (_pending) {
			auto style = ::Ui::UnreadBadgeStyle();
			style.active = active;
			style.align = style::al_right;
			::Ui::PaintUnreadBadge(
				p,
				QString::number(_pending),
				st.padding.left() + st.photoSize,
				st.padding.top() + st.photoSize - st::dialogsUnreadHeight,
				style);
		}
		return;
	}

	const auto nameWidth = width()
		- st.nameLeft
		- st.padding.right();
	p.setFont(st::semiboldFont);
	p.setPen(nameFg);
	_title.draw(p, {
		.position = { st.nameLeft, st.nameTop },
		.availableWidth = nameWidth,
		.elisionLines = 1,
	});

	p.setFont(st::dialogsTextFont);
	p.setPen(textFg);
	_status.draw(p, {
		.position = { st.textLeft, st.textTop },
		.availableWidth = nameWidth,
		.elisionLines = 1,
	});

	if (_pending) {
		auto style = ::Ui::UnreadBadgeStyle();
		style.active = active;
		style.align = style::al_right;
		const auto top = st.textTop
			+ st::dialogsTextFont->ascent
			- st::dialogsUnreadFont->ascent
			- (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
		::Ui::PaintUnreadBadge(
			p,
			QString::number(_pending),
			width() - st.padding.right(),
			top,
			style);
	}
}

int RowWidget::resizeGetHeight(int newWidth) {
	_narrow = (newWidth < st::columnMinimalWidthLeft / 2);
	return _shown ? st::defaultDialogRow.height : 0;
}

} // namespace

void SetupChatsListRow(
		not_null<Window::SessionController*> controller,
		not_null<::Ui::VerticalLayout*> list,
		rpl::producer<bool> shown) {
	const auto manager = &controller->session().agent();
	const auto row = list->insert(
		0,
		object_ptr<RowWidget>(list, controller));
	row->setClickedCallback([=] {
		ShowGeneralAgent(controller);
	});
	rpl::combine(
		std::move(shown),
		manager->enabledValue()
	) | rpl::map([](bool shown, bool enabled) {
		return shown && enabled;
	}) | rpl::on_next([=](bool visible) {
		row->setShown(visible);
	}, row->lifetime());
	manager->activityValue(
		Scope::Account()
	) | rpl::on_next([=](ScopeActivity activity) {
		row->setActivity(activity);
	}, row->lifetime());
	GeneralAgentShownValue(
		controller
	) | rpl::on_next([=](bool shown) {
		row->setScreenShown(shown);
	}, row->lifetime());
}

void SetGeneralAgentShown(
		not_null<Window::SessionController*> controller,
		bool shown) {
	auto &state = ShownState();
	if (shown) {
		state.shown.insert(controller);
	} else {
		state.shown.remove(controller);
	}
	state.changes.fire_copy(controller);
}

rpl::producer<bool> GeneralAgentShownValue(
		not_null<Window::SessionController*> controller) {
	return rpl::single(
		ShownState().shown.contains(controller)
	) | rpl::then(
		ShownState().changes.events(
		) | rpl::filter([=](not_null<Window::SessionController*> changed) {
			return changed == controller;
		}) | rpl::map([=] {
			return ShownState().shown.contains(controller);
		})
	) | rpl::distinct_until_changed();
}

} // namespace Agent::Ui
