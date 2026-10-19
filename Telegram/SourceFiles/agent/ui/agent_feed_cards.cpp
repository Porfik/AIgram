/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_cards.h"

#include "agent/core/agent_prompts.h"
#include "agent/ui/agent_session_view.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QPainterPath>

#include "styles/style_agent.h"
#include "styles/style_chat_helpers.h"

namespace Agent::Ui {
namespace {

[[nodiscard]] QString CardTitleText(CardKind kind) {
	switch (kind) {
	case CardKind::Send:
		return tr::lng_agent_card_title_send(tr::now);
	case CardKind::Draft:
		return tr::lng_agent_card_title_draft(tr::now);
	case CardKind::Forward:
		return tr::lng_agent_card_title_forward(tr::now);
	case CardKind::Edit:
		return tr::lng_agent_card_title_edit(tr::now);
	case CardKind::Delete:
		return tr::lng_agent_card_title_delete(tr::now);
	case CardKind::React:
		return tr::lng_agent_card_title_react(tr::now);
	case CardKind::PinMessage:
		return tr::lng_agent_card_title_pin(tr::now);
	case CardKind::UnpinMessage:
		return tr::lng_agent_card_title_unpin(tr::now);
	case CardKind::DeleteFolder:
		return tr::lng_agent_card_title_delete_folder(tr::now);
	case CardKind::Handoff:
		return tr::lng_agent_card_title_handoff(tr::now);
	}
	return QString();
}

[[nodiscard]] QString OptionText(CardKind kind) {
	return (kind == CardKind::Delete)
		? tr::lng_agent_card_revoke(tr::now)
		: tr::lng_agent_card_notify(tr::now);
}

[[nodiscard]] QString TargetLineText(const CardTarget &target) {
	if (target.messageId && !target.preview.isEmpty()) {
		return target.preview;
	}
	return target.title;
}

[[nodiscard]] QString CheckedButtonCount(int count) {
	return tr::lng_agent_card_send_count(tr::now, lt_count, count);
}

} // namespace

class FeedCardWidget::CardSpinner final : public RpWidget {
public:
	CardSpinner(QWidget *parent, const style::InfiniteRadialAnimation &st);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::InfiniteRadialAnimation _animation;

};

class FeedCardWidget::RunningRow final : public RpWidget {
public:
	RunningRow(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::InfiniteRadialAnimation _animation;
	object_ptr<Ui::FlatLabel> _label;

};

class FeedCardWidget::CardButtonsRow final : public RpWidget {
public:
	CardButtonsRow(QWidget *parent)
	: RpWidget(parent) {
	}

	Ui::RoundButton *add(object_ptr<Ui::RoundButton> button);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	std::vector<object_ptr<Ui::RoundButton>> _buttons;

};

FeedCardWidget::CardSpinner::CardSpinner(
	QWidget *parent,
	const style::InfiniteRadialAnimation &st)
: RpWidget(parent)
, _animation([=] { update(); }, st) {
	_animation.start();
}

int FeedCardWidget::CardSpinner::resizeGetHeight(int newWidth) {
	return st::agentFeedCardSpinnerSize.height();
}

void FeedCardWidget::CardSpinner::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_animation.draw(
		p,
		QPoint(0, 0),
		st::agentFeedCardSpinnerSize,
		width());
}

FeedCardWidget::RunningRow::RunningRow(QWidget *parent)
: RpWidget(parent)
, _animation([=] { update(); }, st::agentFeedCardSpinner)
, _label(
	this,
	tr::lng_agent_card_running(tr::now),
	st::agentFeedCardDetailLabel) {
	_animation.start();
}

int FeedCardWidget::RunningRow::resizeGetHeight(int newWidth) {
	const auto size = st::agentFeedCardSpinnerSize;
	_label->resizeToNaturalWidth(
		std::max(newWidth - size.width() - st::agentFeedCardGap, 1));
	_label->moveToLeft(size.width() + st::agentFeedCardGap, 0, newWidth);
	return std::max(size.height(), _label->height());
}

void FeedCardWidget::RunningRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto size = st::agentFeedCardSpinnerSize;
	_animation.draw(
		p,
		QPoint(0, (_label->height() - size.height()) / 2),
		size,
		width());
}

Ui::RoundButton *FeedCardWidget::CardButtonsRow::add(
		object_ptr<Ui::RoundButton> button) {
	const auto raw = button.data();
	_buttons.push_back(std::move(button));
	raw->widthValue(
	) | rpl::on_next([=] {
		if (width() > 0) {
			resize(width(), resizeGetHeight(width()));
		}
	}, raw->lifetime());
	return raw;
}

int FeedCardWidget::CardButtonsRow::resizeGetHeight(int newWidth) {
	const auto gap = st::agentFeedCardButtonsGap;
	auto x = 0;
	auto y = 0;
	auto rowHeight = 0;
	for (const auto &button : _buttons) {
		const auto w = button->width();
		if (x > 0 && x + w > newWidth) {
			x = 0;
			y += rowHeight + gap;
			rowHeight = 0;
		}
		button->moveToLeft(x, y, newWidth);
		x += w + gap;
		rowHeight = std::max(rowHeight, button->height());
	}
	return y + rowHeight;
}

FeedCardWidget::FeedCardWidget(
	QWidget *parent,
	FeedContext context,
	const FeedCard &data)
: FeedItemWidget(parent)
, _context(std::move(context))
, _card(data.card) {
	_payload.checked.reserve(_card.targets.size());
	for (const auto &target : _card.targets) {
		_payload.checked.push_back(target.checked);
	}
	_payload.option = _card.option;
	_payload.allowForTurn = _card.allowedForTurn;
	_payload.text = _card.text;
	build();
}

void FeedCardWidget::updatePayload(const FeedItem &item) {
	const auto data = std::get_if<FeedCard>(&item);
	Expects(data != nullptr);

	const auto sameTargets = [&] {
		if (_card.targets.size() != data->card.targets.size()) {
			return false;
		}
		for (auto i = 0; i != int(_card.targets.size()); ++i) {
			const auto &a = _card.targets[i];
			const auto &b = data->card.targets[i];
			if (a.peer != b.peer
				|| a.messageId != b.messageId
				|| a.title != b.title
				|| a.preview != b.preview
				|| a.error != b.error) {
				return false;
			}
		}
		return true;
	}();
	if (_card.status == data->card.status && sameTargets) {
		_card = data->card;
		return;
	}
	if (pending()) {
		capturePayload();
	}
	_card = data->card;
	build();
}

void FeedCardWidget::capturePayload() {
	if (_field) {
		_payload.text = _field->getLastText();
	}
	auto index = 0;
	for (const auto checkbox : _targetCheckboxes) {
		_payload.checked[index++] = checkbox->checked();
	}
	if (_option) {
		_payload.option = _option->checked();
	}
	if (_allow) {
		_payload.allowForTurn = _allow->checked();
	}
}

bool FeedCardWidget::pending() const {
	return _card.status == CardStatus::Pending;
}

bool FeedCardWidget::running() const {
	return _card.status == CardStatus::Running;
}

bool FeedCardWidget::editable() const {
	return CardTextEditable(_card.kind);
}

bool FeedCardWidget::hasAlternate() const {
	return CardHasAlternate(_card.kind);
}

bool FeedCardWidget::hasTargets() const {
	return !_card.targets.empty();
}

int FeedCardWidget::checkedCount() const {
	auto result = 0;
	for (const auto checked : _payload.checked) {
		if (checked) {
			++result;
		}
	}
	return result;
}

int FeedCardWidget::resizeGetHeight(int newWidth) {
	const auto cardWidth = std::min(
		newWidth,
		st::agentFeedMaxTextWidth);
	if (_inner) {
		_inner->resizeToWidth(std::max(
			cardWidth - st::agentFeedCardPadding.left()
				- st::agentFeedCardPadding.right(),
			1));
		_inner->moveToLeft(
			st::agentFeedCardPadding.left(),
			st::agentFeedCardPadding.top(),
			cardWidth);
		return st::agentFeedCardPadding.top()
			+ _inner->height()
			+ st::agentFeedCardPadding.bottom();
	}
	return 0;
}

void FeedCardWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto cardWidth = std::min(
		width(),
		st::agentFeedMaxTextWidth);
	const auto rect = QRect(0, 0, cardWidth, height());
	p.setPen(Qt::NoPen);
	p.setBrush(st::agentFeedCardBg);
	p.drawRoundedRect(
		rect,
		st::agentFeedCardRadius,
		st::agentFeedCardRadius);
}

void FeedCardWidget::build() {
	_inner.destroy();
	_targetCheckboxes.clear();
	_option = _allow = nullptr;
	_execute = _alternate = nullptr;
	_buttonsRow = nullptr;
	_field = nullptr;
	_inner.create(this);
	_inner->show();
	const auto gap = st::agentFeedCardGap;
	const auto addGap = [&](int top) {
		return style::margins(0, top, 0, 0);
	};
	_inner->add(object_ptr<Ui::FlatLabel>(
		_inner,
		CardTitleText(_card.kind),
		st::agentFeedCardTitleLabel));
	const auto pendingNow = pending();
	if (_card.kind == CardKind::Forward && !_card.text.isEmpty()) {
		_inner->add(
			object_ptr<Ui::FlatLabel>(
				_inner,
				_card.text,
				st::agentFeedTextLabel),
			addGap(gap));
	}
	if (_card.kind == CardKind::Handoff) {
		_inner->add(
			object_ptr<Ui::FlatLabel>(
				_inner,
				_card.text,
				st::agentFeedTextLabel),
			addGap(gap));
		_inner->add(
			object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_agent_card_handoff_about(tr::now),
				st::agentFeedCardDetailLabel),
			addGap(gap));
		_execute = _inner->add(
			object_ptr<Ui::RoundButton>(
				_inner,
				tr::lng_agent_card_handoff_button(),
				st::agentFeedCardButtonMain),
			addGap(gap));
		const auto text = _card.text;
		_execute->setClickedCallback([=] {
			crl::on_main([weak = base::make_weak(this), text] {
				if (weak) {
					weak->_context.delegate.startHandoff(text);
				}
			});
		});
		_inner->heightValue(
		) | rpl::on_next([=](int height) {
			if (width() > 0) {
				resize(width(), height);
			}
		}, _inner->lifetime());
		if (width() > 0) {
			resize(width(), resizeGetHeight(width()));
		}
		update();
		return;
	}
	if (pendingNow && hasTargets() && _card.targets.size() > 1) {
		auto index = 0;
		for (const auto &target : _card.targets) {
			const auto i = index++;
			auto text = target.title;
			if (!target.preview.isEmpty()) {
				text += u"\n"_q + target.preview;
			}
			auto checkbox = _inner->add(
				object_ptr<Ui::Checkbox>(
					_inner,
					text,
					_payload.checked[i],
					st::agentFeedCardCheckbox),
				addGap(gap));
			checkbox->setAllowTextLines(2);
			checkbox->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				_payload.checked[i] = checked;
				refreshButtons();
			}, checkbox->lifetime());
			_targetCheckboxes.push_back(checkbox);
		}
	} else {
		auto first = true;
		for (const auto &target : _card.targets) {
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					TargetLineText(target),
					st::agentFeedCardTargetLabel),
				addGap(first ? gap : 0));
			first = false;
			if (!target.error.isEmpty()) {
				_inner->add(
					object_ptr<Ui::FlatLabel>(
						_inner,
						target.error,
						st::agentFeedActionsMarkBad),
					addGap(0));
			}
		}
	}
	if (!_card.detail.isEmpty()) {
		switch (_card.kind) {
		case CardKind::Send:
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					tr::lng_agent_card_reply_to(
						tr::now,
						lt_text,
						_card.detail),
					st::agentFeedCardDetailLabel),
				addGap(gap));
			break;
		case CardKind::Forward:
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					tr::lng_agent_card_from_chat(
						tr::now,
						lt_chat,
						_card.detail),
					st::agentFeedCardDetailLabel),
				addGap(gap));
			break;
		case CardKind::Edit:
		case CardKind::Draft:
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					(_card.kind == CardKind::Edit
						? tr::lng_agent_card_current_text(tr::now)
						: tr::lng_agent_card_field_text(tr::now)),
					st::agentFeedCardDetailLabel),
				addGap(gap));
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					_card.detail,
					st::agentFeedCardTargetLabel),
				addGap(0));
			break;
		default:
			break;
		}
	}
	if (_card.kind == CardKind::React) {
		_inner->add(
			object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_agent_card_reaction(
					tr::now,
					lt_emoji,
					_card.text),
				st::agentFeedCardDetailLabel),
			addGap(gap));
	} else if (_card.kind == CardKind::DeleteFolder) {
		_inner->add(
			object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_agent_card_folder(
					tr::now,
					lt_folder,
					_card.text),
				st::agentFeedCardDetailLabel),
			addGap(gap));
	}
	if (pendingNow) {
		if (editable()) {
			_field = _inner->add(
				object_ptr<Ui::InputField>(
					_inner,
					st::agentFeedCardField,
					Ui::InputField::Mode::MultiLine,
					(_card.kind == CardKind::Edit
						? tr::lng_agent_card_new_text()
						: tr::lng_agent_card_text_placeholder())),
				addGap(gap));
			_field->setText(_payload.text);
			_field->changes(
			) | rpl::on_next([=] {
				_payload.text = _field->getLastText();
				refreshButtons();
			}, _field->lifetime());
		}
		if (_card.optionAvailable) {
			_option = _inner->add(
				object_ptr<Ui::Checkbox>(
					_inner,
					OptionText(_card.kind),
					_payload.option,
					st::agentFeedCardCheckbox),
				addGap(gap));
			_option->setAllowTextLines(2);
			_option->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				_payload.option = checked;
			}, _option->lifetime());
		}
		if (_card.canAllowForTurn) {
			_allow = _inner->add(
				object_ptr<Ui::Checkbox>(
					_inner,
					tr::lng_agent_card_allow_turn(tr::now),
					_payload.allowForTurn,
					st::agentFeedCardCheckbox),
				addGap(gap));
			_allow->setAllowTextLines(2);
			_allow->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				_payload.allowForTurn = checked;
			}, _allow->lifetime());
		}
		_buttonsRow = _inner->add(
			object_ptr<CardButtonsRow>(_inner),
			addGap(gap));
		const auto executeText = [&] {
			if (_card.kind == CardKind::Send) {
				return (hasTargets() && _card.targets.size() > 1)
					? CheckedButtonCount(checkedCount())
					: tr::lng_agent_card_send(tr::now);
			} else if (_card.kind == CardKind::Draft) {
				return tr::lng_agent_card_replace(tr::now);
			}
			return (hasTargets() && _card.targets.size() > 1)
				? tr::lng_agent_card_confirm_count(
					tr::now,
					lt_count,
					checkedCount())
				: tr::lng_agent_card_confirm(tr::now);
		};
		_execute = _buttonsRow->add(
			object_ptr<Ui::RoundButton>(
				_buttonsRow,
				rpl::single(executeText()),
				st::agentFeedCardButtonMain));
		_execute->setClickedCallback([=] {
			decide(CardAction::Execute);
		});
		if (hasAlternate()) {
			_alternate = _buttonsRow->add(
				object_ptr<Ui::RoundButton>(
					_buttonsRow,
					(_card.kind == CardKind::Send
						? tr::lng_agent_card_to_draft()
						: tr::lng_agent_card_append()),
					st::agentFeedCardButton));
			_alternate->setClickedCallback([=] {
				decide(CardAction::Alternate);
			});
		}
		_buttonsRow->add(
			object_ptr<Ui::RoundButton>(
				_buttonsRow,
				(_card.kind == CardKind::Draft
					? tr::lng_agent_card_cancel()
					: tr::lng_agent_card_decline()),
				st::agentFeedCardButtonDanger)
		)->setClickedCallback([=] {
			decide(CardAction::Decline);
		});
		refreshButtons();
	} else if (running()) {
		_inner->add(
			object_ptr<RunningRow>(_inner),
			addGap(gap));
	} else {
		const auto count = int(_card.targets.size());
		auto failed = 0;
		for (const auto &target : _card.targets) {
			if (!target.error.isEmpty()) {
				++failed;
			}
		}
		const auto status = [&] {
			if (_card.status == CardStatus::Done && failed) {
				return tr::lng_agent_card_done_partial(
					tr::now,
					lt_done,
					QString::number(count - failed),
					lt_total,
					QString::number(count));
			}
			switch (_card.status) {
			case CardStatus::Done:
				return tr::lng_agent_card_done(tr::now);
			case CardStatus::Drafted:
				return tr::lng_agent_card_drafted(tr::now);
			case CardStatus::Declined:
				return tr::lng_agent_card_declined(tr::now);
			case CardStatus::Failed:
				return tr::lng_agent_card_failed(tr::now);
			case CardStatus::NotDone:
				return tr::lng_agent_card_not_done(tr::now);
			default:
				return QString();
			}
		}();
		if (!_card.text.isEmpty()
			&& (_card.kind == CardKind::Send
				|| _card.kind == CardKind::Draft)) {
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					_card.text,
					st::agentFeedTextLabel),
				addGap(gap));
		} else if (_card.kind == CardKind::Edit
			&& !_card.text.isEmpty()) {
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					tr::lng_agent_card_new_text(tr::now),
					st::agentFeedCardDetailLabel),
				addGap(gap));
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					_card.text,
					st::agentFeedTextLabel),
				addGap(0));
		}
		_inner->add(
			object_ptr<Ui::FlatLabel>(
				_inner,
				status,
				st::agentFeedCardStatusLabel),
			addGap(gap));
		if (_card.autoApproved) {
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					tr::lng_agent_card_auto(tr::now),
					st::agentFeedCardDetailLabel),
				addGap(0));
		}
		if (editable() && _card.decided != CardAction::Decline
			&& _card.text != _card.proposedText) {
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					tr::lng_agent_card_edited(tr::now),
					st::agentFeedCardDetailLabel),
				addGap(0));
		}
	}
	_inner->heightValue(
	) | rpl::on_next([=](int height) {
		if (width() > 0) {
			resize(width(), height);
		}
	}, _inner->lifetime());
	if (width() > 0) {
		resize(width(), resizeGetHeight(width()));
	}
	update();
}

void FeedCardWidget::refreshButtons() {
	if (!_execute) {
		return;
	}
	const auto text = _field
		? _field->getLastText()
		: _payload.text;
	const auto hasText = !text.trimmed().isEmpty();
	if (_card.kind == CardKind::Send && hasTargets()
		&& _card.targets.size() > 1) {
		_execute->setText(
			rpl::single(CheckedButtonCount(checkedCount())));
	} else if (_card.kind != CardKind::Draft && hasTargets()
		&& _card.targets.size() > 1) {
		_execute->setText(rpl::single(
			tr::lng_agent_card_confirm_count(
				tr::now,
				lt_count,
				checkedCount())));
	}
	const auto needTargets = hasTargets() && !checkedCount();
	const auto needText = editable() && !hasText;
	_execute->setDisabled(needTargets || needText);
	if (_alternate) {
		_alternate->setDisabled(needText);
	}
}

void FeedCardWidget::decide(CardAction action) {
	capturePayload();
	const auto decision = CardDecision{
		.action = action,
		.checked = _payload.checked,
		.text = (action == CardAction::Decline) ? QString() : _payload.text,
		.option = _payload.option,
		.allowForTurn = _payload.allowForTurn,
	};
	const auto id = _card.toolUseId;
	crl::on_main([weak = base::make_weak(this), id, decision] {
		if (weak) {
			weak->_context.delegate.decideCard(id, decision);
		}
	});
}

struct ParsedAction {
	QStringList done;
	QStringList failed;
	QStringList failedTitles;
	QString folder;

	[[nodiscard]] QString chats() const {
		return (done.isEmpty() ? failedTitles : done).join(u", "_q);
	}
};

[[nodiscard]] ParsedAction ParseActionResult(
		const std::optional<ToolResultBlock> &result) {
	auto parsed = ParsedAction();
	if (!result || result->error) {
		return parsed;
	}
	const auto document = QJsonDocument::fromJson(
		UnwrapUntrusted(result->content).toUtf8());
	if (!document.isObject()) {
		return parsed;
	}
	const auto object = document.object();
	parsed.folder = object.value("folder").toObject()
		.value("title").toString();
	for (const auto value : object.value("done").toArray()) {
		const auto title = value.toObject().value("title").toString();
		if (!title.isEmpty()) {
			parsed.done.push_back(title);
		}
	}
	for (const auto value : object.value("failed").toArray()) {
		const auto entry = value.toObject();
		const auto title = entry.value("title").toString();
		const auto error = entry.value("error").toString();
		if (!title.isEmpty()) {
			parsed.failedTitles.push_back(title);
		}
		if (!error.isEmpty()) {
			parsed.failed.push_back(tr::lng_agent_action_failed(
				tr::now,
				lt_error,
				title.isEmpty() ? error : title + u": "_q + error));
		} else if (!title.isEmpty()) {
			parsed.failed.push_back(title);
		}
	}
	return parsed;
}

[[nodiscard]] QString ActionMainText(
		const QString &name,
		const QJsonObject &input,
		const QString &chats,
		const QString &folder) {
	if (name == u"mark_read"_q) {
		return tr::lng_agent_action_mark_read(tr::now, lt_chats, chats);
	} else if (name == u"set_mute"_q) {
		return input.value("muted").toBool(true)
			? tr::lng_agent_action_mute(tr::now, lt_chats, chats)
			: tr::lng_agent_action_unmute(tr::now, lt_chats, chats);
	} else if (name == u"set_archived"_q) {
		return input.value("archived").toBool(true)
			? tr::lng_agent_action_archive(tr::now, lt_chats, chats)
			: tr::lng_agent_action_unarchive(tr::now, lt_chats, chats);
	} else if (name == u"set_pinned"_q) {
		return input.value("pinned").toBool(true)
			? tr::lng_agent_action_pin_chat(tr::now, lt_chats, chats)
			: tr::lng_agent_action_unpin_chat(tr::now, lt_chats, chats);
	} else if (name == u"set_draft"_q) {
		return tr::lng_agent_action_draft(tr::now, lt_chats, chats);
	} else if (name == u"create_folder"_q) {
		return tr::lng_agent_action_folder_create(
			tr::now,
			lt_folder,
			folder);
	} else if (name == u"rename_folder"_q) {
		return tr::lng_agent_action_folder_rename(
			tr::now,
			lt_folder,
			folder);
	} else if (name == u"set_folder_chats"_q) {
		return input.value("included").toBool(true)
			? tr::lng_agent_action_folder_add(
				tr::now,
				lt_chats,
				chats,
				lt_folder,
				folder)
			: tr::lng_agent_action_folder_remove(
				tr::now,
				lt_chats,
				chats,
				lt_folder,
				folder);
	} else if (name == u"reorder_folders"_q) {
		return tr::lng_agent_action_folder_order(tr::now);
	}
	return name;
}

class FeedActionsWidget::ActionRow final : public RpWidget {
public:
	ActionRow(QWidget *parent, const FeedTool &tool);

	void refreshUndo(bool undoable, bool undone);
	[[nodiscard]] rpl::producer<> undoClicks() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _mark = { nullptr };
	object_ptr<Ui::FlatLabel> _text;
	object_ptr<Ui::FlatLabel> _failedText;
	object_ptr<Ui::LinkButton> _undo;
	object_ptr<Ui::FlatLabel> _undone;
	rpl::event_stream<> _undoClicks;

};

FeedActionsWidget::ActionRow::ActionRow(
	QWidget *parent,
	const FeedTool &tool)
: RpWidget(parent)
, _text(this, QString(), st::agentFeedTextLabel)
, _failedText(this, QString(), st::agentFeedActionsMarkBad)
, _undo(this, tr::lng_agent_undo(tr::now))
, _undone(
	this,
	tr::lng_agent_undone(tr::now),
	st::agentFeedCardDetailLabel) {
	const auto parsed = ParseActionResult(tool.result);
	_mark.create(
		this,
		QString(QChar(parsed.failed.isEmpty() ? 0x2713 : 0x2717)),
		parsed.failed.isEmpty()
			? st::agentFeedActionsMarkGood
			: st::agentFeedActionsMarkBad);
	_text->setText(ActionMainText(
		tool.call.name,
		tool.call.input,
		parsed.chats(),
		parsed.folder));
	if (!parsed.failed.isEmpty()) {
		_failedText->setText(parsed.failed.join(u"\n"_q));
	} else {
		_failedText->hide();
	}
	_undo->setClickedCallback([=] {
		_undoClicks.fire({});
	});
	_undo->show();
	_undone->hide();
}

void FeedActionsWidget::ActionRow::refreshUndo(
		bool undoable,
		bool undone) {
	_undo->setVisible(undoable && !undone);
	_undone->setVisible(undone);
}

rpl::producer<> FeedActionsWidget::ActionRow::undoClicks() const {
	return _undoClicks.events();
}

int FeedActionsWidget::ActionRow::resizeGetHeight(int newWidth) {
	const auto gap = st::agentFeedActionsRowGap;
	_mark->resizeToNaturalWidth(newWidth);
	const auto left = _mark->width() + gap;
	const auto undoWidth = _undo->width();
	_text->resizeToWidth(std::max(newWidth - left - undoWidth - gap, 1));
	_text->moveToLeft(left, 0, newWidth);
	_mark->moveToLeft(0, 0, newWidth);
	_undo->moveToRight(0, 0, newWidth);
	_undone->moveToRight(0, 0, newWidth);
	auto height = std::max(
		_text->height(),
		std::max(_mark->height(), _undo->height()));
	if (!_failedText->isHidden()) {
		_failedText->resizeToWidth(std::max(newWidth - left, 1));
		_failedText->moveToLeft(left, height + gap, newWidth);
		height += gap + _failedText->height();
	}
	return height;
}

FeedActionsWidget::FeedActionsWidget(
	QWidget *parent,
	FeedContext context,
	const FeedActions &data)
: FeedItemWidget(parent)
, _context(std::move(context))
, _actions(data) {
	build();
}

void FeedActionsWidget::updatePayload(const FeedItem &item) {
	const auto actions = std::get_if<FeedActions>(&item);
	Expects(actions != nullptr);

	_actions = *actions;
	build();
}

bool FeedActionsWidget::undoable(int index) const {
	const auto &result = _actions.actions[index].result;
	return result && !result->error
		&& _context.delegate.canUndo(
			_actions.actions[index].call.id);
}

void FeedActionsWidget::build() {
	_inner.destroy();
	_rows.clear();
	_undoAll = nullptr;
	_inner.create(this);
	_inner->show();
	auto undoableCount = 0;
	for (auto i = 0; i != int(_actions.actions.size()); ++i) {
		const auto row = _inner->add(
			object_ptr<ActionRow>(
				_inner,
				_actions.actions[i]),
			style::margins(0, i ? st::agentFeedActionsRowGap : 0, 0, 0));
		row->undoClicks(
		) | rpl::on_next([=] {
			undoOne(i);
		}, row->lifetime());
		_rows.push_back(row);
		if (undoable(i)
			&& !_context.delegate.isUndone(
				_actions.actions[i].call.id)) {
			++undoableCount;
		}
	}
	if (undoableCount >= 2) {
		_undoAll = _inner->add(
			object_ptr<Ui::LinkButton>(
				_inner,
				tr::lng_agent_undo_all(tr::now)),
			st::agentFeedActionsUndoAllMargin);
		_undoAll->setClickedCallback([=] {
			undoAll();
		});
	}
	_inner->heightValue(
	) | rpl::on_next([=](int height) {
		resize(width(), height);
	}, _inner->lifetime());
	refreshRows();
	resize(width(), resizeGetHeight(width()));
}

int FeedActionsWidget::undoableCount() const {
	auto result = 0;
	for (auto i = 0; i != int(_actions.actions.size()); ++i) {
		if (undoable(i)
			&& !_context.delegate.isUndone(
				_actions.actions[i].call.id)) {
			++result;
		}
	}
	return result;
}

void FeedActionsWidget::refreshRows() {
	for (auto i = 0; i != int(_rows.size()); ++i) {
		const auto id = _actions.actions[i].call.id;
		_rows[i]->refreshUndo(
			undoable(i),
			_context.delegate.isUndone(id));
	}
	if (_undoAll) {
		_undoAll->setVisible(undoableCount() >= 2);
	}
	resize(width(), resizeGetHeight(width()));
}

void FeedActionsWidget::undoOne(int index) {
	const auto id = _actions.actions[index].call.id;
	crl::on_main([weak = base::make_weak(this), id] {
		if (weak) {
			weak->_context.delegate.undo({ id });
		}
	});
}

void FeedActionsWidget::undoAll() {
	auto ids = std::vector<QString>();
	for (auto i = 0; i != int(_actions.actions.size()); ++i) {
		if (undoable(i)) {
			ids.push_back(_actions.actions[i].call.id);
		}
	}
	crl::on_main([weak = base::make_weak(this), ids] {
		if (weak) {
			weak->_context.delegate.undo(ids);
		}
	});
}

int FeedActionsWidget::resizeGetHeight(int newWidth) {
	if (_inner) {
		_inner->resizeToWidth(newWidth);
		_inner->moveToLeft(0, 0, newWidth);
		return _inner->height();
	}
	return 0;
}

} // namespace Agent::Ui
