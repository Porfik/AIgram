/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_card.h"
#include "agent/core/agent_feed.h"
#include "agent/ui/agent_feed_items.h"
#include "ui/effects/radial_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"

#include <QtCore/QTemporaryFile>
#include <memory>
#include <vector>

namespace Agent::Ui {

class FeedCardWidget final : public FeedItemWidget {
public:
	FeedCardWidget(
		QWidget *parent,
		FeedContext context,
		const FeedCard &data);

	void updatePayload(const FeedItem &item) override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Payload {
		QString text;
		std::vector<bool> checked;
		bool option = false;
		bool allowForTurn = false;
	};
	class CardSpinner;
	class CardButtonsRow;
	class RunningRow;

	void capturePayload();
	void build();
	void buildSendFilesPreview();
	void refreshButtons();
	void decide(CardAction action);

	[[nodiscard]] bool pending() const;
	[[nodiscard]] bool running() const;
	[[nodiscard]] bool editable() const;
	[[nodiscard]] bool hasAlternate() const;
	[[nodiscard]] bool hasTargets() const;
	[[nodiscard]] int checkedCount() const;
	[[nodiscard]] int64 filesCheckedSize() const;
	void refreshFilesTotal();

	FeedContext _context;
	Card _card;
	Payload _payload;
	object_ptr<VerticalLayout> _inner = { nullptr };
	std::vector<::Ui::Checkbox*> _targetCheckboxes;
	std::vector<std::unique_ptr<QTemporaryFile>> _tempFiles;
	::Ui::FlatLabel *_filesTotal = nullptr;
	::Ui::InputField *_field = nullptr;
	::Ui::Checkbox *_option = nullptr;
	::Ui::Checkbox *_allow = nullptr;
	::Ui::RoundButton *_execute = nullptr;
	::Ui::RoundButton *_alternate = nullptr;
	CardButtonsRow *_buttonsRow = nullptr;

};

class FeedActionsWidget final : public FeedItemWidget {
public:
	FeedActionsWidget(
		QWidget *parent,
		FeedContext context,
		const FeedActions &data);

	void updatePayload(const FeedItem &item) override;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	class ActionRow;

	[[nodiscard]] bool undoable(int index) const;
	[[nodiscard]] int undoableCount() const;
	void build();
	void refreshRows();
	void undoOne(int index);
	void undoAll();

	FeedContext _context;
	FeedActions _actions;
	object_ptr<VerticalLayout> _inner = { nullptr };
	std::vector<ActionRow*> _rows;
	::Ui::LinkButton *_undoAll = nullptr;

};

} // namespace Agent::Ui
