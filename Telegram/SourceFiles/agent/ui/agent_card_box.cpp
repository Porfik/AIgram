/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_card_box.h"

#include "agent/ui/agent_feed_format.h"

#include "agent/core/agent_prompts.h"
#include "agent/agent_files.h"
#include "agent/agent_manager.h"
#include "agent/core/agent_media.h"
#include "agent/ui/agent_task_format.h"
#include "core/file_utilities.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_album_preview.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_single_file_preview.h"
#include "ui/chat/attach/attach_single_media_preview.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/round_checkbox.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "boxes/peer_list_box.h"
#include "data/data_session.h"
#include "ui/layers/show.h"
#include "ui/text/text.h"
#include "ui/text/text_options.h"
#include "window/window_session_controller.h"

#include <gsl/gsl>
#include <QtGui/QPainterPath>
#include <QtCore/QDir>
#include <QtCore/QTemporaryFile>

#include "styles/style_agent.h"
#include "styles/style_agent_media.h"
#include "styles/style_agent_card_box.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {
namespace {

void AddTitle(
		not_null<VerticalLayout*> container,
		rpl::producer<QString> text) {
	Ui::AddSubsectionTitle(
		container,
		std::move(text),
		style::margins(-st::defaultSubsectionTitlePadding.left(), 0, 0, 0));
}

[[nodiscard]] QString OptionText(CardKind kind) {
	return (kind == CardKind::Delete)
		? tr::lng_agent_card_revoke(tr::now)
		: (kind == CardKind::PinMessage)
		? tr::lng_agent_card_notify(tr::now)
		: (kind == CardKind::SendFiles)
		? tr::lng_agent_card_uncompressed(tr::now)
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

[[nodiscard]] QString FileSizeText(int64 size) {
	return Ui::FormatSizeText(size);
}

struct TaskCardRow {
	QString label;
	QString value;
	QString was;
};

[[nodiscard]] std::vector<TaskCardRow> TaskCardRows(
		not_null<Main::Session*> session,
		not_null<Manager*> manager,
		CardKind kind,
		const std::optional<Task> &task,
		const std::optional<Task> &previous) {
	const auto fallback = Task();
	const auto &current = task ? *task : fallback;
	const auto triggerText = [&](const Task &forTask) {
		return FormatTaskTrigger(session, forTask);
	};
	const auto frameText = [&](const Task &forTask) {
		return FormatTaskFrame(session, forTask);
	};
	const auto modelText = [&](const Task &forTask) {
		return FormatTaskModel(manager, forTask);
	};
	const auto limitsText = [&](const Task &forTask) {
		return FormatTaskLimits(
			forTask.limits,
			TaskCostKnown(manager, forTask));
	};
	const auto notifyText = [&](const Task &forTask) {
		return FormatTaskNotify(forTask);
	};
	const auto row = [](tr::phrase<> label, QString value) {
		return TaskCardRow{ label(tr::now), std::move(value), QString() };
	};
	auto rows = std::vector<TaskCardRow>();
	if (kind == CardKind::CreateTask) {
		rows.push_back(row(tr::lng_agent_task_trigger, triggerText(current)));
		rows.push_back(row(tr::lng_agent_task_frame, frameText(current)));
		rows.push_back(row(tr::lng_agent_task_model, modelText(current)));
		rows.push_back(row(tr::lng_agent_task_limits, limitsText(current)));
		rows.push_back(row(tr::lng_agent_task_notify, notifyText(current)));
	} else if (task && previous) {
		const auto changed = [&](tr::phrase<> label, auto format, bool same) {
			if (!same) {
				rows.push_back({
					label(tr::now),
					format(current),
					format(*previous),
				});
			}
		};
		changed(
			tr::lng_agent_task_trigger,
			triggerText,
			current.trigger == previous->trigger);
		changed(
			tr::lng_agent_task_frame,
			frameText,
			current.frame == previous->frame);
		changed(
			tr::lng_agent_task_model,
			modelText,
			current.providerId == previous->providerId);
		changed(
			tr::lng_agent_task_limits,
			limitsText,
			current.limits == previous->limits);
		changed(
			tr::lng_agent_task_notify,
			notifyText,
			current.notify == previous->notify);
	}
	return rows;
}

class SendFilesPreviewBox final : public RpWidget {
public:
	SendFilesPreviewBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Ui::PreparedList list);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void addBlock(
		const Ui::Text::MarkedContext &captionContext,
		int from,
		int till);

	Ui::PreparedList _list;
	std::vector<base::unique_qptr<RpWidget>> _blocks;

};

SendFilesPreviewBox::SendFilesPreviewBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Ui::PreparedList list)
: RpWidget(parent)
, _list(std::move(list)) {
	Expects(!_list.files.empty());

	using Type = Ui::PreparedFile::Type;
	const auto captionContext = Core::TextContext(
		{ .session = &controller->session() });
	const auto single = [](Type type) {
		return (type == Type::File)
			|| (type == Type::Music)
			|| (type == Type::None);
	};
	auto albumStart = -1;
	for (auto i = 0, till = int(_list.files.size()); i != till; ++i) {
		const auto type = _list.files[i].type;
		if (albumStart >= 0) {
			if (single(type) || (i - albumStart == Ui::MaxAlbumItems())) {
				addBlock(captionContext, std::exchange(albumStart, -1), i);
			} else {
				continue;
			}
		}
		if (!single(type)) {
			albumStart = i;
			continue;
		}
		addBlock(captionContext, i, i + 1);
	}
	if (albumStart >= 0) {
		addBlock(captionContext, albumStart, int(_list.files.size()));
	}
}

void SendFilesPreviewBox::addBlock(
		const Ui::Text::MarkedContext &captionContext,
		int from,
		int till) {
	const auto items = gsl::make_span(_list.files).subspan(from, till - from);
	auto widget = (RpWidget*)nullptr;
	if (items.size() > 1) {
		widget = Ui::CreateChild<Ui::AlbumPreview>(
			this,
			st::defaultComposeControls,
			items,
			captionContext,
			Ui::SendFilesWay());
	} else if (const auto media = Ui::SingleMediaPreview::Create(
			this,
			st::defaultComposeControls,
			[] { return false; },
			items.front(),
			Ui::AttachControls::Type::None)) {
		widget = media;
	} else {
		widget = Ui::CreateChild<Ui::SingleFilePreview>(
			this,
			st::defaultComposeControls,
			items.front(),
			captionContext,
			Ui::AttachControls::Type::None);
	}
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	widget->show();
	_blocks.emplace_back(widget);
}

int SendFilesPreviewBox::resizeGetHeight(int newWidth) {
	auto top = 0;
	for (const auto &block : _blocks) {
		if (top) {
			top += st::sendMediaRowSkip;
		}
		block->resizeToWidth(newWidth);
		block->moveToLeft(0, top, newWidth);
		top += block->height();
	}
	return top;
}

[[nodiscard]] Ui::PreparedList SendFilesPreparedList(
		not_null<Window::SessionController*> controller,
		const std::vector<CardFile> &files,
		std::vector<std::unique_ptr<QTemporaryFile>> &tempFiles,
		const Fn<std::optional<MediaBlock>(const QString &)> &attachment) {
	const auto session = &controller->session();
	auto paths = QStringList();
	for (const auto &file : files) {
		if (Agent::AttachmentIndex(file.ref) > 0) {
			const auto block = attachment
				? attachment(file.ref)
				: std::nullopt;
			if (!block) {
				continue;
			}
			auto temp = std::make_unique<QTemporaryFile>(
				QDir::tempPath() + u"/agent-XXXXXX-"_q + file.name);
			if (!temp->open()) {
				continue;
			}
			temp->write(block->bytes);
			temp->flush();
			paths.push_back(temp->fileName());
			tempFiles.push_back(std::move(temp));
			continue;
		}
		const auto path = Agent::ResolveAgentFile(session, file.ref);
		if (!path.isEmpty()) {
			paths.push_back(path);
		}
	}
	return Storage::PrepareMediaList(paths, st::agentCardPreviewWidth, false);
}

class CodeBox final : public RpWidget {
public:
	CodeBox(QWidget *parent, const QString &text);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<FlatLabel> _label;

};

CodeBox::CodeBox(QWidget *parent, const QString &text)
: RpWidget(parent)
, _label(this, QString(), st::agentCardCodeLabel) {
	_label->setMarkedText(Monospace(text));
	_label->setSelectable(true);
}

int CodeBox::resizeGetHeight(int newWidth) {
	const auto &padding = st::agentCardCodePadding;
	_label->resizeToWidth(
		std::max(newWidth - padding.left() - padding.right(), 1));
	_label->moveToLeft(padding.left(), padding.top(), newWidth);
	return padding.top() + _label->height() + padding.bottom();
}

void CodeBox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::agentCardCodeBg);
	const auto radius = st::agentCardCodeRadius;
	p.drawRoundedRect(rect(), radius, radius);
}

[[nodiscard]] object_ptr<RpWidget> MakeCodeBlock(
		QWidget *parent,
		const QString &text) {
	return object_ptr<CodeBox>(parent, text);
}

class RunningRow final : public RpWidget {
public:
	RunningRow(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::InfiniteRadialAnimation _animation;
	object_ptr<Ui::FlatLabel> _label;

};

RunningRow::RunningRow(QWidget *parent)
: RpWidget(parent)
, _animation([=] { update(); }, st::agentCardSpinner)
, _label(
	this,
	tr::lng_agent_card_running(tr::now),
	st::agentCardDetailLabel) {
	_animation.start();
}

int RunningRow::resizeGetHeight(int newWidth) {
	const auto size = st::agentCardSpinnerSize;
	_label->resizeToNaturalWidth(
		std::max(newWidth - size.width() - st::agentCardGap, 1));
	_label->moveToLeft(size.width() + st::agentCardGap, 0, newWidth);
	return std::max(size.height(), _label->height());
}

void RunningRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto size = st::agentCardSpinnerSize;
	_animation.draw(
		p,
		QPoint(0, (_label->height() - size.height()) / 2),
		size,
		width());
}

class TargetRow final : public RippleButton {
public:
	TargetRow(
		QWidget *parent,
		not_null<Main::Session*> session,
		const CardTarget &target,
		bool checkable);

	void setChecked(bool checked, anim::type animated);
	[[nodiscard]] bool checked() const {
		return _checked;
	}
	[[nodiscard]] rpl::producer<bool> checkedValue() const {
		return _checkedChanges.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const style::PeerListItem &_st;
	Ui::Text::String _name;
	Ui::Text::String _status;
	std::unique_ptr<Ui::RoundImageCheckbox> _checkbox;
	std::unique_ptr<Ui::EmptyUserpic> _emptyUserpic;
	PaintRoundImageCallback _paintUserpic;
	bool _checked = false;
	bool _isError = false;
	rpl::event_stream<bool> _checkedChanges;

};

TargetRow::TargetRow(
		QWidget *parent,
		not_null<Main::Session*> session,
		const CardTarget &target,
		bool checkable)
: RippleButton(parent, st::defaultRippleAnimation)
, _st(st::agentCardTargetItem)
, _checked(checkable && target.checked) {
	_name.setText(_st.nameStyle, target.title);
	_isError = !checkable && !target.error.isEmpty();
	_status.setText(
		st::defaultTextStyle,
		_isError ? target.error : target.preview);
	if (const auto peer = session->data().peerLoaded(PeerId(target.peer))) {
		_paintUserpic = PaintUserpicCallback(peer, true);
	} else {
		_emptyUserpic = std::make_unique<Ui::EmptyUserpic>(
			Ui::EmptyUserpic::UserpicColor(
				Ui::EmptyUserpic::ColorIndex(target.peer)),
			target.title);
		_paintUserpic = [raw = _emptyUserpic.get()](
				Painter &p,
				int x,
				int y,
				int outerWidth,
				int size) {
			raw->paintCircle(p, x, y, outerWidth, size);
		};
	}
	if (checkable) {
		_checkbox = std::make_unique<Ui::RoundImageCheckbox>(
			_st.checkbox,
			[=] { update(); },
			std::move(_paintUserpic));
		_checkbox->setChecked(_checked, anim::type::instant);
		setClickedCallback([=] {
			setChecked(!checked(), anim::type::normal);
		});
	}
}

void TargetRow::setChecked(bool checked, anim::type animated) {
	if (_checked == checked) {
		return;
	}
	_checked = checked;
	if (_checkbox) {
		_checkbox->setChecked(checked, animated);
	}
	_checkedChanges.fire_copy(checked);
}

void TargetRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	paintRipple(p, 0, 0);

	const auto &st = _st;
	const auto x = st.photoPosition.x();
	const auto y = st.photoPosition.y();
	if (_checkbox) {
		_checkbox->paint(p, x, y, width());
	} else {
		_paintUserpic(p, x, y, width(), st.photoSize);
	}
	p.setPen(_checked ? st.nameFgChecked : st.nameFg);
	const auto nameTop = _status.isEmpty()
		? (st.height - st.nameStyle.font->height) / 2
		: st.namePosition.y();
	_name.drawLeftElided(
		p,
		st.namePosition.x(),
		nameTop,
		width() - st.namePosition.x(),
		width());
	p.setPen(_isError ? st::boxTextFgError : st.statusFg);
	_status.drawLeftElided(
		p,
		st.statusPosition.x(),
		st.statusPosition.y(),
		width() - st.statusPosition.x(),
		width());
}

int TargetRow::resizeGetHeight(int newWidth) {
	return _st.height;
}

} // namespace

class CardBoxState final {
public:
	CardBoxState(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		CardBoxArgs args);

	void build();

private:
	struct Payload {
		QString text;
		std::vector<bool> checked;
		bool option = false;
		bool allowForTurn = false;
	};

	void buildTaskRows(bool pendingNow);
	void buildMethodHeader();
	void buildMethodParams();
	void buildSendFilesPreview();
	void buildTargets(bool pendingNow);
	void buildFiles(bool pendingNow);
	void buildField(rpl::producer<QString> placeholder);
	void buildOptions();
	void buildButtons();
	void buildRunning();
	void buildFinal();
	void refreshButtons();
	void decide(CardAction action);
	void capturePayload();

	[[nodiscard]] bool pending() const;
	[[nodiscard]] bool running() const;
	[[nodiscard]] bool editable() const;
	[[nodiscard]] bool hasAlternate() const;
	[[nodiscard]] bool hasTargets() const;
	[[nodiscard]] int checkedCount() const;
	[[nodiscard]] int64 filesCheckedSize() const;
	void refreshFilesTotal();

	not_null<GenericBox*> _box;
	not_null<Window::SessionController*> _controller;
	CardBoxArgs _args;
	Card _card;
	Payload _payload;
	VerticalLayout *_inner = nullptr;
	std::vector<TargetRow *> _targetRows;
	std::vector<Ui::Checkbox *> _targetCheckboxes;
	std::vector<std::unique_ptr<QTemporaryFile>> _tempFiles;
	Ui::FlatLabel *_filesTotal = nullptr;
	Ui::InputField *_field = nullptr;
	Ui::Checkbox *_option = nullptr;
	Ui::Checkbox *_allow = nullptr;
	Ui::RoundButton *_execute = nullptr;
	Ui::RoundButton *_alternate = nullptr;
	bool _paramsShown = false;

};

CardBoxState::CardBoxState(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		CardBoxArgs args)
: _box(box)
, _controller(controller)
, _args(std::move(args))
, _card(_args.card) {
	_payload.checked.reserve(_card.targets.size());
	for (const auto &target : _card.targets) {
		_payload.checked.push_back(target.checked);
	}
	_payload.option = _card.option;
	_payload.allowForTurn = _card.allowedForTurn;
	_payload.text = _card.text;
	const auto outer = box->verticalLayout();
	_inner = outer->add(
		object_ptr<VerticalLayout>(outer),
		st::boxRowPadding);
}

void CardBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		CardBoxArgs args) {
	const auto state = box->lifetime().make_state<CardBoxState>(
		box,
		controller,
		std::move(args));
	state->build();
}

bool CardBoxState::pending() const {
	return _card.status == CardStatus::Pending;
}

bool CardBoxState::running() const {
	return _card.status == CardStatus::Running;
}

bool CardBoxState::editable() const {
	return CardTextEditable(_card.kind);
}

bool CardBoxState::hasAlternate() const {
	return CardHasAlternate(_card.kind);
}

bool CardBoxState::hasTargets() const {
	return !_card.targets.empty();
}

int CardBoxState::checkedCount() const {
	auto result = 0;
	for (const auto checked : _payload.checked) {
		if (checked) {
			++result;
		}
	}
	return result;
}

int64 CardBoxState::filesCheckedSize() const {
	auto total = int64(0);
	auto index = 0;
	for (const auto &file : _card.files) {
		if (index < int(_payload.checked.size()) && _payload.checked[index]) {
			total += file.size;
		}
		++index;
	}
	return total;
}

void CardBoxState::refreshFilesTotal() {
	if (!_filesTotal) {
		return;
	}
	_filesTotal->setText(tr::lng_agent_card_download_size(
		tr::now,
		lt_size,
		FileSizeText(filesCheckedSize())));
}

void CardBoxState::capturePayload() {
	if (_field) {
		_payload.text = _field->getLastText();
	}
	auto index = 0;
	for (const auto row : _targetRows) {
		_payload.checked[index++] = row->checked();
	}
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

void CardBoxState::build() {
	_box->setWidth(st::boxWideWidth);
	_box->setTitle((_card.kind == CardKind::Change)
		? _card.text
		: CardTitleText(_card.kind));
	const auto pendingNow = pending();
	if (_card.kind == CardKind::CreateTask
		|| _card.kind == CardKind::UpdateTask) {
		buildTaskRows(pendingNow);
	}
	if (_card.kind == CardKind::Method) {
		buildMethodHeader();
	}
	if (_card.kind == CardKind::Change || _card.kind == CardKind::Method) {
		const auto wasText = [](const QString &text) {
			return tr::lng_agent_card_task_was(tr::now, lt_text, text);
		};
		for (const auto &row : _card.rows) {
			Ui::AddSkip(_inner, st::agentCardGap);
			_inner->add(object_ptr<Ui::FlatLabel>(
				_inner,
				row.label,
				st::agentCardDetailLabel));
			_inner->add(object_ptr<Ui::FlatLabel>(
				_inner,
				row.value,
				st::agentCardTextLabel));
			if (!row.previous.isEmpty()) {
				_inner->add(object_ptr<Ui::FlatLabel>(
					_inner,
					wasText(row.previous),
					st::agentCardDetailLabel));
			}
		}
	}
	if (_card.kind == CardKind::Method) {
		buildMethodParams();
	}
	if ((_card.kind == CardKind::Forward || _card.kind == CardKind::Action)
		&& !_card.text.isEmpty()) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			_card.text,
			st::agentCardTextLabel));
	}
	if (_card.kind == CardKind::Handoff) {
		Expects(_args.handoff != nullptr);
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			_card.text,
			st::agentCardTextLabel));
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_handoff_about(tr::now),
			st::agentCardDetailLabel));
		_box->addButton(tr::lng_agent_card_handoff_button(), [=] {
			const auto weak = base::make_weak(_box);
			_args.handoff(_card.text);
			if (weak) {
				weak->closeBox();
			}
		});
		_box->addButton(tr::lng_close(), [=] { _box->closeBox(); });
		return;
	}
	if (pendingNow && _card.kind == CardKind::Download
		&& !_card.files.empty()) {
		buildFiles(pendingNow);
	} else if (!_card.targets.empty()) {
		buildTargets(pendingNow);
	}
	if (_card.kind == CardKind::Transcribe) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_transcribe_left(
				tr::now,
				lt_done,
				QString::number(_card.amount),
				lt_total,
				QString::number(_card.quota)),
			st::agentCardDetailLabel));
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_transcribe_about(tr::now),
			st::agentCardDetailLabel));
	} else if (_card.kind == CardKind::Download
		&& !_card.detail.isEmpty()) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_download_to(
				tr::now,
				lt_path,
				_card.detail),
			st::agentCardDetailLabel));
	}
	if (!_card.detail.isEmpty()) {
		switch (_card.kind) {
		case CardKind::Send:
			Ui::AddSkip(_inner, st::agentCardGap);
			_inner->add(object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_agent_card_reply_to(
					tr::now,
					lt_text,
					_card.detail),
				st::agentCardDetailLabel));
			break;
		case CardKind::Forward:
			Ui::AddSkip(_inner, st::agentCardGap);
			_inner->add(object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_agent_card_from_chat(
					tr::now,
					lt_chat,
					_card.detail),
				st::agentCardDetailLabel));
			break;
		case CardKind::Edit:
		case CardKind::Draft:
			Ui::AddSkip(_inner, st::agentCardGap);
			_inner->add(object_ptr<Ui::FlatLabel>(
				_inner,
				(_card.kind == CardKind::Edit
					? tr::lng_agent_card_current_text(tr::now)
					: tr::lng_agent_card_field_text(tr::now)),
				st::agentCardDetailLabel));
			_inner->add(object_ptr<Ui::FlatLabel>(
				_inner,
				_card.detail,
				st::agentCardTextLabel));
			break;
		default:
			break;
		}
	}
	if (_card.kind == CardKind::React) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_reaction(
				tr::now,
				lt_emoji,
				_card.text),
			st::agentCardDetailLabel));
	} else if (_card.kind == CardKind::DeleteFolder) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_folder(
				tr::now,
				lt_folder,
				_card.text),
			st::agentCardDetailLabel));
	}
	if (pendingNow) {
		if (_card.kind == CardKind::SendFiles) {
			buildSendFilesPreview();
		} else if (_card.kind == CardKind::Change
			&& !_card.files.empty()) {
			buildSendFilesPreview();
		}
		if (editable()
			&& _card.kind != CardKind::CreateTask
			&& _card.kind != CardKind::UpdateTask) {
			buildField((_card.kind == CardKind::Edit)
				? tr::lng_agent_card_new_text()
				: (_card.kind == CardKind::SendFiles)
				? tr::lng_photo_caption()
				: tr::lng_agent_card_text_placeholder());
		}
		buildOptions();
		buildButtons();
	} else if (running()) {
		buildRunning();
	} else {
		buildFinal();
	}
}

void CardBoxState::buildTaskRows(bool pendingNow) {
	const auto session = &_controller->session();
	auto task = Agent::DeserializeTask(_card.task);
	auto previous = Agent::DeserializeTask(_card.previousTask);
	const auto addLabel = [&](
			QString text,
			int top,
			const style::FlatLabel &st) {
		if (top > 0) {
			Ui::AddSkip(_inner, top);
		}
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			std::move(text),
			st));
	};
	const auto wasText = [](const QString &text) {
		return tr::lng_agent_card_task_was(tr::now, lt_text, text);
	};
	addLabel(
		tr::lng_agent_task_instruction(tr::now),
		st::agentCardGap,
		st::agentCardDetailLabel);
	if (pendingNow) {
		_field = _inner->add(
			object_ptr<Ui::InputField>(
				_inner,
				st::agentCardField,
				Ui::InputField::Mode::MultiLine,
				tr::lng_agent_card_instruction_placeholder()),
			style::margins(0, st::agentCardGap, 0, 0));
		_field->setText(_payload.text);
		_field->changes() | rpl::on_next([=] {
			_payload.text = _field->getLastText();
			refreshButtons();
		}, _field->lifetime());
		_box->setFocusCallback([=] {
			_field->setFocusFast();
		});
	} else if (!_card.text.isEmpty()) {
		addLabel(_card.text, 0, st::agentCardTextLabel);
	}
	if (task && previous
		&& !previous->instruction.isEmpty()
		&& task->instruction != previous->instruction) {
		addLabel(wasText(previous->instruction), 0, st::agentCardDetailLabel);
	}
	for (const auto &row : TaskCardRows(
			session,
			&session->agent(),
			_card.kind,
			task,
			previous)) {
		addLabel(row.label, st::agentCardGap, st::agentCardDetailLabel);
		addLabel(row.value, 0, st::agentCardTextLabel);
		if (!row.was.isEmpty()) {
			addLabel(wasText(row.was), 0, st::agentCardDetailLabel);
		}
	}
	const auto newCatchUp = task.has_value() && task->catchUp;
	const auto wasCatchUp = previous.has_value() && previous->catchUp;
	if (_card.kind == CardKind::CreateTask) {
		if (newCatchUp) {
			addLabel(
				tr::lng_agent_task_catch_up(tr::now),
				st::agentCardGap,
				st::agentCardDetailLabel);
		}
	} else if (task && previous && newCatchUp != wasCatchUp) {
		if (newCatchUp) {
			addLabel(
				tr::lng_agent_task_catch_up(tr::now),
				st::agentCardGap,
				st::agentCardDetailLabel);
		} else {
			addLabel(
				wasText(tr::lng_agent_task_catch_up(tr::now)),
				st::agentCardGap,
				st::agentCardDetailLabel);
		}
	}
}

void CardBoxState::buildMethodHeader() {
	if (!_card.text.isEmpty()) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_method_description(tr::now),
			st::agentCardDetailLabel));
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			_card.text,
			st::agentCardTextLabel));
	}
	Ui::AddSkip(_inner, st::agentCardGap);
	_inner->add(object_ptr<Ui::FlatLabel>(
		_inner,
		tr::lng_agent_card_method_name(tr::now),
		st::agentCardDetailLabel));
	const auto name = _inner->add(object_ptr<Ui::FlatLabel>(
		_inner,
		QString(),
		st::agentCardTextLabel));
	name->setMarkedText(TextWithEntities{
		_card.detail,
		{ EntityInText(EntityType::Code, 0, int(_card.detail.size())) },
	});
}

void CardBoxState::buildMethodParams() {
	if (_card.proposedText.isEmpty()) {
		return;
	}
	const auto toggle = _inner->add(
		object_ptr<Ui::LinkButton>(
			_inner,
			(_paramsShown
				? tr::lng_agent_card_method_hide(tr::now)
				: tr::lng_agent_card_method_show(tr::now))),
		style::margins(0, st::agentCardGap, 0, 0));
	const auto wrap = _inner->add(
		object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
			_inner,
			MakeCodeBlock(_inner, _card.proposedText)),
		style::margins(0, st::agentCardGap, 0, 0));
	wrap->toggle(_paramsShown, anim::type::instant);
	toggle->setClickedCallback([=] {
		_paramsShown = !_paramsShown;
		toggle->setText(_paramsShown
			? tr::lng_agent_card_method_hide(tr::now)
			: tr::lng_agent_card_method_show(tr::now));
		wrap->toggle(_paramsShown, anim::type::normal);
	});
}

void CardBoxState::buildSendFilesPreview() {
	if (_card.files.empty()) {
		return;
	}
	auto list = SendFilesPreparedList(
		_controller,
		_card.files,
		_tempFiles,
		_args.attachment);
	if (list.error != Ui::PreparedList::Error::None
		|| list.files.empty()) {
		for (const auto &file : _card.files) {
			_inner->add(
				object_ptr<Ui::FlatLabel>(
					_inner,
					file.name + u" \u00b7 "_q + FileSizeText(file.size),
					st::agentCardTextLabel),
				style::margins(0, st::agentCardGap, 0, 0));
		}
		return;
	}
	_inner->add(
		object_ptr<SendFilesPreviewBox>(
			_inner,
			_controller,
			std::move(list)));
}

void CardBoxState::buildTargets(bool pendingNow) {
	const auto session = &_controller->session();
	const auto checkable = pendingNow && _card.targets.size() > 1;
	if (checkable) {
		AddTitle(_inner, tr::lng_agent_card_targets());
	}
	auto index = 0;
	for (const auto &target : _card.targets) {
		const auto i = index++;
		const auto row = _inner->add(
			object_ptr<TargetRow>(
				_inner,
				session,
				target,
				checkable),
			style::margins(0, checkable ? 0 : st::agentCardGap, 0, 0));
		_targetRows.push_back(row);
		if (checkable) {
			row->checkedValue() | rpl::on_next([=](bool checked) {
				_payload.checked[i] = checked;
				refreshButtons();
			}, row->lifetime());
		}
	}
}

void CardBoxState::buildFiles(bool pendingNow) {
	if (_payload.checked.size() < _card.files.size()) {
		_payload.checked.resize(_card.files.size(), true);
	}
	AddTitle(_inner, tr::lng_agent_card_files());
	auto index = 0;
	for (const auto &file : _card.files) {
		const auto i = index++;
		auto checkbox = _inner->add(
			object_ptr<Ui::Checkbox>(
				_inner,
				file.name + u" \u00b7 "_q + FileSizeText(file.size),
				_payload.checked[i],
				st::defaultBoxCheckbox),
			style::margins(0, st::agentCardGap, 0, 0));
		checkbox->setAllowTextLines(2);
		checkbox->checkedChanges() | rpl::on_next([=](bool checked) {
			_payload.checked[i] = checked;
			refreshFilesTotal();
			refreshButtons();
		}, checkbox->lifetime());
		_targetCheckboxes.push_back(checkbox);
	}
	_filesTotal = _inner->add(
		object_ptr<Ui::FlatLabel>(
			_inner,
			QString(),
			st::agentCardDetailLabel),
		style::margins(0, st::agentCardGap, 0, 0));
	refreshFilesTotal();
}

void CardBoxState::buildField(rpl::producer<QString> placeholder) {
	AddTitle(_inner, std::move(placeholder));
	_field = _inner->add(
		object_ptr<Ui::InputField>(
			_inner,
			st::agentCardField,
			Ui::InputField::Mode::MultiLine),
		style::margins(0, st::agentCardGap, 0, 0));
	_field->setText(_payload.text);
	_field->changes() | rpl::on_next([=] {
		_payload.text = _field->getLastText();
		refreshButtons();
	}, _field->lifetime());
	_box->setFocusCallback([=] {
		_field->setFocusFast();
	});
}

void CardBoxState::buildOptions() {
	if (_card.optionAvailable) {
		_option = _inner->add(
			object_ptr<Ui::Checkbox>(
				_inner,
				OptionText(_card.kind),
				_payload.option,
				st::defaultBoxCheckbox),
			style::margins(0, st::agentCardGap, 0, 0));
		_option->setAllowTextLines(2);
		_option->checkedChanges() | rpl::on_next([=](bool checked) {
			_payload.option = checked;
		}, _option->lifetime());
	}
	if (_card.canAllowForTurn) {
		_allow = _inner->add(
			object_ptr<Ui::Checkbox>(
				_inner,
				tr::lng_agent_card_allow_turn(tr::now),
				_payload.allowForTurn,
				st::defaultBoxCheckbox),
			style::margins(0, st::agentCardGap, 0, 0));
		_allow->setAllowTextLines(2);
		_allow->checkedChanges() | rpl::on_next([=](bool checked) {
			_payload.allowForTurn = checked;
		}, _allow->lifetime());
	}
}

void CardBoxState::buildButtons() {
	const auto executeText = [&] {
		if (_card.kind == CardKind::CreateTask) {
			return tr::lng_agent_card_create(tr::now);
		} else if (_card.kind == CardKind::UpdateTask) {
			return tr::lng_agent_card_save(tr::now);
		} else if (_card.kind == CardKind::Transcribe) {
			return tr::lng_agent_card_transcribe(tr::now);
		} else if (_card.kind == CardKind::Download) {
			return tr::lng_agent_card_download_count(
				tr::now,
				lt_count,
				checkedCount());
		} else if (_card.kind == CardKind::Send) {
			return (hasTargets() && _card.targets.size() > 1)
				? CheckedButtonCount(checkedCount())
				: tr::lng_agent_card_send(tr::now);
		} else if (_card.kind == CardKind::Draft) {
			return tr::lng_agent_card_replace(tr::now);
		} else if (_card.kind == CardKind::SendFiles) {
			return (hasTargets() && _card.targets.size() > 1)
				? CheckedButtonCount(checkedCount())
				: tr::lng_agent_card_send(tr::now);
		}
		return (hasTargets() && _card.targets.size() > 1)
			? tr::lng_agent_card_confirm_count(
				tr::now,
				lt_count,
				checkedCount())
			: tr::lng_agent_card_confirm(tr::now);
	};
	Expects(_args.decide != nullptr);
	_execute = _box->addButton(
		rpl::single(executeText()),
		[=] {
			capturePayload();
			const auto hasText = !_payload.text.trimmed().isEmpty();
			const auto needTargets = hasTargets() && !checkedCount();
			const auto needText = editable() && !hasText
				&& _card.kind != CardKind::SendFiles;
			if (needTargets) {
				_box->uiShow()->showToast(
					tr::lng_agent_card_choose_chats(tr::now));
			} else if (needText) {
				_field->showError();
			} else {
				decide(CardAction::Execute);
			}
		});
	if (hasAlternate()) {
		_alternate = _box->addButton(
			(_card.kind == CardKind::Send)
				? tr::lng_agent_card_to_draft()
				: tr::lng_agent_card_append(),
			[=] { decide(CardAction::Alternate); });
	}
	_box->addLeftButton(
		(_card.kind == CardKind::Draft)
			? tr::lng_agent_card_cancel()
			: tr::lng_agent_card_decline(),
		[=] { decide(CardAction::Decline); },
		st::attentionBoxButton);
	refreshButtons();
}

void CardBoxState::buildRunning() {
	Ui::AddSkip(_inner, st::agentCardGap);
	_inner->add(object_ptr<RunningRow>(_inner));
	_box->addButton(tr::lng_close(), [=] { _box->closeBox(); });
}

void CardBoxState::buildFinal() {
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
			return (_card.kind == CardKind::Download)
				? (_card.detail.isEmpty()
					? tr::lng_agent_card_downloaded(tr::now)
					: tr::lng_agent_card_saved(tr::now))
				: (_card.kind == CardKind::CreateTask)
				? tr::lng_agent_card_task_created(tr::now)
				: (_card.kind == CardKind::UpdateTask)
				? tr::lng_agent_card_task_updated(tr::now)
				: tr::lng_agent_card_done(tr::now);
		case CardStatus::Drafted:
			return tr::lng_agent_card_drafted(tr::now);
		case CardStatus::Declined:
			return tr::lng_agent_card_declined(tr::now);
		case CardStatus::Failed:
			return tr::lng_agent_card_failed(tr::now);
		case CardStatus::NotDone:
			return tr::lng_agent_card_not_done(tr::now);
		case CardStatus::Expired:
			return tr::lng_agent_card_expired(tr::now);
		default:
			return QString();
		}
	}();
	if (!_card.text.isEmpty()
		&& (_card.kind == CardKind::Send || _card.kind == CardKind::Draft)) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			_card.text,
			st::agentCardTextLabel));
	} else if (_card.kind == CardKind::Edit && !_card.text.isEmpty()) {
		Ui::AddSkip(_inner, st::agentCardGap);
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_new_text(tr::now),
			st::agentCardDetailLabel));
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			_card.text,
			st::agentCardTextLabel));
	}
	Ui::AddSkip(_inner, st::agentCardGap);
	_inner->add(object_ptr<Ui::FlatLabel>(
		_inner,
		status,
		st::agentCardStatusLabel));
	if (_card.kind == CardKind::Download
		&& _card.status == CardStatus::Done
		&& !_card.detail.isEmpty()) {
		const auto show = _inner->add(
			object_ptr<Ui::LinkButton>(
				_inner,
				tr::lng_context_show_in_folder(tr::now)));
		show->setClickedCallback([=] {
			File::ShowInFolder(_card.detail);
		});
	}
	if (_card.autoApproved) {
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			(_args.run
				? tr::lng_agent_card_auto_task(tr::now)
				: tr::lng_agent_card_auto(tr::now)),
			st::agentCardDetailLabel));
	}
	if (editable() && _card.decided != CardAction::Decline
		&& _card.text != _card.proposedText) {
		_inner->add(object_ptr<Ui::FlatLabel>(
			_inner,
			tr::lng_agent_card_edited(tr::now),
			st::agentCardDetailLabel));
	}
	_box->addButton(tr::lng_close(), [=] { _box->closeBox(); });
}

void CardBoxState::refreshButtons() {
	if (!_execute) {
		return;
	}
	const auto text = _field
		? _field->getLastText()
		: _payload.text;
	const auto hasText = !text.trimmed().isEmpty();
	if (_card.kind == CardKind::Send && hasTargets()
		&& _card.targets.size() > 1) {
		_execute->setText(rpl::single(CheckedButtonCount(checkedCount())));
	} else if (_card.kind == CardKind::Download) {
		_execute->setText(rpl::single(
			tr::lng_agent_card_download_count(
				tr::now,
				lt_count,
				checkedCount())));
	} else if (_card.kind == CardKind::SendFiles) {
		_execute->setText(rpl::single(
			hasTargets() && _card.targets.size() > 1
				? CheckedButtonCount(checkedCount())
				: tr::lng_agent_card_send(tr::now)));
	} else if (_card.kind != CardKind::Draft && hasTargets()
		&& _card.targets.size() > 1) {
		_execute->setText(rpl::single(
			tr::lng_agent_card_confirm_count(
				tr::now,
				lt_count,
				checkedCount())));
	}
	const auto needText = editable() && !hasText
		&& _card.kind != CardKind::SendFiles;
	if (_alternate) {
		_alternate->setDisabled(needText);
	}
}

void CardBoxState::decide(CardAction action) {
	capturePayload();
	const auto decision = CardDecision{
		.action = action,
		.checked = _payload.checked,
		.text = (action == CardAction::Decline) ? QString() : _payload.text,
		.option = _payload.option,
		.allowForTurn = _payload.allowForTurn,
	};
	const auto weak = base::make_weak(_box);
	_args.decide(decision);
	if (weak) {
		weak->closeBox();
	}
}

void ShowCardBox(
		not_null<Window::SessionController*> controller,
		CardBoxArgs args) {
	controller->show(Box(CardBox, controller, std::move(args)));
}

} // namespace Agent::Ui

