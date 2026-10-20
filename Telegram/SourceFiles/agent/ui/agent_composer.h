/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/ui/agent_feed_items.h"
#include "ui/rp_widget.h"
#include "ui/widgets/fields/input_field.h"

namespace Ui {
class IconButton;
class SendButton;
} // namespace Ui

namespace Agent::Ui {

class Composer final : public RpWidget {
public:
	explicit Composer(QWidget *parent);

	void setWorking(bool working);
	void setQueued(const std::vector<QString> &texts);
	[[nodiscard]] rpl::producer<QString> sends() const;
	[[nodiscard]] rpl::producer<> stops() const;
	[[nodiscard]] rpl::producer<> heightChanges() const;
	[[nodiscard]] rpl::producer<> attachRequests() const;
	void setMimeDataHook(InputField::MimeDataHook hook);
	void focusField();
	[[nodiscard]] QString draft() const;
	void setDraft(const QString &text);
	void setShadowAtBottom(bool bottom);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void submit();
	void refresh();

	object_ptr<VerticalLayout> _queued;
	object_ptr<Ui::IconButton> _attach;
	object_ptr<Ui::InputField> _field;
	object_ptr<Ui::SendButton> _send;
	bool _working = false;
	bool _shadowAtBottom = false;
	rpl::event_stream<QString> _sends;
	rpl::event_stream<> _stops;
	rpl::event_stream<> _attachRequests;
	rpl::event_stream<> _heightChanges;

};

} // namespace Agent::Ui
