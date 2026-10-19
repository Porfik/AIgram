/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_cards.h"

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

FeedCardWidget::FeedCardWidget(
	QWidget *parent,
	FeedContext context,
	const FeedCard &data)
: FeedItemWidget(parent)
, _context(std::move(context))
, _card(data.card)
, _spinner([=] { update(); }, st::agentFeedCardSpinner) {
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
	return (_card.status == CardStatus::Pending)
		|| (_card.status == CardStatus::Running);
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
	if (running()) {
		_spinner.draw(
			p,
			QPoint(
				cardWidth - st::agentFeedCardSpinnerSize.width()
					- st::agentFeedCardPadding.right(),
				st::agentFeedCardPadding.top()),
			st::agentFeedCardSpinnerSize,
			width());
	}
}

void FeedCardWidget::build() {
	_inner.destroy();
	_targetCheckboxes.clear();
	_option = _allow = nullptr;
	_execute = _alternate = nullptr;
	_field = nullptr;
	_inner.create(this);
	const auto gap = st::agentFeedCardGap;
	const auto addGap = [&](int top) {
		return style::margins(0, top, 0, 0);
	};
	_inner->add(object_ptr<Ui::FlatLabel>(
		_inner,
		CardTitleText(_card.kind),
		st::agentFeedCardTitleLabel));
	const auto pendingNow = pending();
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
				st::agentFeedCardButton),
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
			resize(width(), height);
		}, _inner->lifetime());
		resize(width(), resizeGetHeight(width()));
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
			_field->heightChanges(
			) | rpl::on_next([=] {
				resize(width(), resizeGetHeight(width()));
				refreshButtons();
			}, _field->lifetime());
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
			_allow->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				_payload.allowForTurn = checked;
			}, _allow->lifetime());
		}
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
		_execute = _inner->add(
			object_ptr<Ui::RoundButton>(
				_inner,
				rpl::single(executeText()),
				st::agentFeedCardButton),
			addGap(gap));
		_execute->setClickedCallback([=] {
			decide(CardAction::Execute);
		});
		if (hasAlternate()) {
			_alternate = _inner->add(
				object_ptr<Ui::RoundButton>(
					_inner,
					(_card.kind == CardKind::Send
						? tr::lng_agent_card_to_draft()
						: tr::lng_agent_card_append()),
					st::agentFeedCardButton),
				addGap(st::agentFeedCardButtonsGap));
			_alternate->setClickedCallback([=] {
				decide(CardAction::Alternate);
			});
		}
		_inner->add(
			object_ptr<Ui::RoundButton>(
				_inner,
				tr::lng_agent_card_decline(),
				st::agentFeedCardButtonDanger),
			addGap(st::agentFeedCardButtonsGap)
		)->setClickedCallback([=] {
			decide(CardAction::Decline);
		});
		refreshButtons();
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
		resize(width(), height);
	}, _inner->lifetime());
	if (running()) {
		_spinner.start();
	} else {
		_spinner.stop(anim::type::instant);
	}
	resize(width(), resizeGetHeight(width()));
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

} // namespace Agent::Ui
