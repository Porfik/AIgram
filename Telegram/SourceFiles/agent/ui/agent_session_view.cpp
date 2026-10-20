/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_session_view.h"

#include "base/algorithm.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_feed.h"
#include "agent/core/agent_prompts.h"
#include "agent/core/agent_spending.h"
#include "agent/ui/agent_spending_format.h"
#include "agent/ui/agent_composer.h"
#include "agent/ui/agent_feed_widget.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/agent_claude.h"
#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/agent_settings.h"
#include "agent/agent_tasks.h"
#include "agent/tools/agent_tool_format.h"
#include "agent/tools/agent_tools.h"
#include "agent/ui/agent_section.h"
#include "agent/core/agent_media.h"
#include "api/api_common.h"
#include "base/variant.h"
#include "boxes/send_files_box.h"
#include "core/mime_type.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "iv/markdown/iv_markdown_math_renderer.h"
#include "lang/lang_keys.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "main/main_session.h"
#include "settings/sections/settings_agent.h"
#include "storage/storage_folder_archive.h"
#include "storage/storage_media_prepare.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include "styles/style_agent.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Agent::Ui {
namespace {

[[nodiscard]] QString PassedMessagesText(const HistoryItemsList &items) {
	const auto peer = items.front()->history()->peer;
	auto chat = QJsonObject();
	chat.insert("id", QJsonValue(qint64(peer->id.value)));
	chat.insert("title", peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: peer->name());
	auto messages = QJsonArray();
	for (const auto &item : items) {
		messages.push_back(ToolFormat::SerializeMessage(item));
	}
	auto json = QJsonObject();
	json.insert("chat", chat);
	json.insert("messages", messages);
	return u"Messages the user passed to you:\n"_q + QString::fromUtf8(
		QJsonDocument(json).toJson(QJsonDocument::Compact));
}

} // namespace

class PassedMessagesBar final : public RpWidget {
public:
	PassedMessagesBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setItems(HistoryItemsList items);
	[[nodiscard]] const HistoryItemsList &items() const;
	[[nodiscard]] bool empty() const;
	[[nodiscard]] rpl::producer<> changes() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const not_null<Window::SessionController*> _controller;
	HistoryView::Controls::ForwardPanel _panel;
	object_ptr<IconButton> _cancel;
	rpl::event_stream<> _changes;

};

PassedMessagesBar::PassedMessagesBar(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _panel([=] { update(); })
, _cancel(this, st::historyReplyCancel) {
	_cancel->setClickedCallback([=] {
		setItems({});
	});
	_panel.itemsUpdated(
	) | rpl::on_next([=] {
		update();
		_changes.fire({});
	}, lifetime());
}

void PassedMessagesBar::setItems(HistoryItemsList items) {
	const auto session = &_controller->session();
	auto to = (Data::Thread*)nullptr;
	if (!items.empty()) {
		to = session->data().history(session->user());
	}
	_panel.update(
		to,
		Data::ResolvedForwardDraft{ .items = std::move(items) });
	update();
	_changes.fire({});
}

const HistoryItemsList &PassedMessagesBar::items() const {
	return _panel.items();
}

bool PassedMessagesBar::empty() const {
	return _panel.empty();
}

rpl::producer<> PassedMessagesBar::changes() const {
	return _changes.events();
}

int PassedMessagesBar::resizeGetHeight(int newWidth) {
	_cancel->moveToRight(0, 0, newWidth);
	return st::historyReplyHeight;
}

void PassedMessagesBar::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(rect(), st::historyComposeAreaBg);
	p.fillRect(0, 0, width(), st::lineWidth, st::shadowFg);
	st::historyForwardIcon.paint(p, st::historyReplyIconPosition, width());
	const auto left = st::historyReplySkip;
	_panel.paint(
		p,
		left,
		0,
		width() - left - _cancel->width() - st::msgReplyPadding.right(),
		width());
}

SessionView::SessionView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope)
: RpWidget(parent)
, _controller(controller)
, _manager(manager)
, _scope(scope)
, _feed(this, FeedContext{
	.controller = controller,
	.renderer = std::make_shared<Iv::Markdown::MathRenderer>(),
	.delegate = {
		.decideCard = [=](
				const QString &toolUseId,
				CardDecision decision) {
			if (_session) {
				_session->decideCard(toolUseId, std::move(decision));
			}
		},
		.canUndo = [=](const QString &toolUseId) {
			return _session && _session->canUndo(toolUseId);
		},
		.isUndone = [=](const QString &toolUseId) {
			return _session && ranges::contains(
				_session->record().undone,
				toolUseId);
		},
		.undo = [=](const std::vector<QString> &toolUseIds) {
			if (_session) {
				_session->undo(toolUseIds);
			}
		},
		.startHandoff = [=](const QString &text) {
			handoffRequested(text);
		},
		.attachment = [=](const QString &id) -> std::optional<MediaBlock> {
			return _session
				? _manager->attachments().read(_session->id(), id)
				: std::nullopt;
		},
		.isRun = [=] {
			return _session && _session->isRun();
		},
	},})
, _composer(this)
, _passedBar(this, controller)
, _queueStrip(this, manager, scope)
, _welcome(this)
, _welcomeLabel(
	_welcome,
	(_scope.kind == ScopeKind::Chat
		? tr::lng_agent_chat_welcome(tr::now)
		: tr::lng_agent_general_welcome(tr::now)),
	st::agentWelcomeLabel)
, _handoffNote(
	this,
	QString(),
	st::agentWelcomeLabel)
, _noProvider(this)
, _noProviderLabel(
	_noProvider,
	tr::lng_agent_no_provider(tr::now),
	st::agentFeedStatusLabel)
, _openSettings(
	_noProvider,
	tr::lng_agent_open_settings(),
	st::agentOverlayOpenSettings) {
	_handoffNote->hide();
	_passedBar->hide();
	_passedBar->changes(
	) | rpl::on_next([=] {
		_passedBar->setVisible(!_passedBar->empty());
		relayout();
	}, lifetime());
	_feed->naturalHeightValue(
	) | rpl::on_next([=] {
		refreshNaturalHeight();
	}, lifetime());
	_composer->sends(
	) | rpl::on_next([=](const QString &text) {
		sendCurrentText(text);
	}, lifetime());

	_composer->stops(
	) | rpl::on_next([=] {
		if (_session) {
			_session->stop();
		}
	}, lifetime());

	_composer->heightChanges(
	) | rpl::on_next([=] {
		relayout();
	}, lifetime());

	_composer->attachRequests(
	) | rpl::on_next([=] {
		chooseAttach();
	}, lifetime());

	_composer->modelRequests(
	) | rpl::on_next([=] {
		showModelMenu();
	}, lifetime());

	_composer->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return Core::CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));
		}
		Unexpected("action in the agent composer mime data hook.");
	});

	const auto dragAreas = DragArea::SetupDragAreaToContainer(
		this,
		crl::guard(this, [=](not_null<const QMimeData*>) {
			return !(_session && _session->isRun());
		}),
		nullptr,
		nullptr);
	dragAreas.document->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, false, QString());
		Window::ActivateWindow(_controller);
	});
	dragAreas.photo->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, true, QString());
		Window::ActivateWindow(_controller);
	});
	dragAreas.photo->setArchiveDroppedCallback([=](const QMimeData *data) {
		const auto urls = Core::ReadMimeUrls(data);
		if (!urls.isEmpty()) {
			auto list = Ui::PreparedList();
			list.files.push_back(Storage::PrepareFilesArchive(urls));
			confirmSendingFiles(std::move(list), QString());
		}
		Window::ActivateWindow(_controller);
	});

	_feed->retryRequests(
	) | rpl::on_next([=] {
		if (_session) {
			_session->retry();
		}
	}, lifetime());
	_feed->settingsRequests(
	) | rpl::on_next([=] {
		if (_session) {
			ShowEditModel(_controller, _session->profileId());
		}
	}, lifetime());

	_manager->enabledValue(
	) | rpl::on_next([=](bool) {
		refreshProviderState();
	}, lifetime());

	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		refreshProviderState();
		refreshSpending();
		refreshModel();
	}, lifetime());

	_openSettings->setClickedCallback([=] {
		_controller->showSettings(Settings::AgentId());
	});

	_queueStrip->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		relayout();
	}, lifetime());
	_queueStrip->clicks(
	) | rpl::on_next([=] {
		ShowQueueBox(
			_controller,
			_manager,
			_scope,
			crl::guard(this, [=](QString sessionId) {
				if (const auto session = _manager->open(sessionId)) {
					showSession(session);
				}
			}));
	}, lifetime());

	refreshProviderState();
	refreshModel();
}

SessionView::~SessionView() {
	_sessionLifetime.destroy();
}

void SessionView::showSession(Session *session) {
	if (_session == session) {
		return;
	}
	_sessionLifetime.destroy();
	_session = session;
	_pendingProfileId = QString();
	if (session) {
		_handoff.reset();
		std::move(session->recordChanges()
		) | rpl::on_next_error_done(
			[=] {
				rebuildFeed();
			},
			[](rpl::no_error) {
			},
			[=] {
				sessionGone();
			},
			_sessionLifetime);
		std::move(session->stateChanges()
		) | rpl::on_next([=] {
			applyState();
		}, _sessionLifetime);
		std::move(session->live()
		) | rpl::on_next([=](const EngineEvent &event) {
			_feed->handleLiveEvent(event);
		}, _sessionLifetime);
		std::move(session->undoErrors()
		) | rpl::on_next([=](const QString &error) {
			_controller->showToast(tr::lng_agent_undo_failed(
				tr::now,
				lt_error,
				error));
		}, _sessionLifetime);
		rebuildFeed();
		_feed->scrollToBottom();
		relayout();
	} else {
		_feed->rebuild({});
		_feed->setStatus(SessionState{});
		_composer->setWorking(false);
		_composer->setQueued({});
		refreshSpending();
		relayout();
	}
	refreshModel();
	_sessionChanges.fire_copy(_session);
}

Session *SessionView::session() const {
	return _session;
}

rpl::producer<Session*> SessionView::sessionChanges() const {
	return _sessionChanges.events();
}

void SessionView::setHintProvider(Fn<QString()> hint) {
	_hint = std::move(hint);
}

void SessionView::setInnerFocus() {
	if (_mode == Mode::Normal) {
		_composer->focusField();
	} else {
		setFocus();
	}
}

QString SessionView::draft() const {
	return _composer->draft();
}

void SessionView::setDraft(const QString &text) {
	_composer->setDraft(text);
}

void SessionView::setContentMaxWidth(int width) {
	if (_contentMaxWidth == width) {
		return;
	}
	_contentMaxWidth = width;
	relayout();
}

void SessionView::setComposerOnTop(bool onTop) {
	_composerOnTop = onTop;
	_composer->setShadowAtBottom(onTop);
	relayout();
}

void SessionView::setMentionsProvider(Fn<std::vector<PeerKey>()> mentions) {
	_mentions = std::move(mentions);
}

void SessionView::passMessages(HistoryItemsList items) {
	_passedBar->setItems(std::move(items));
}

rpl::producer<int> SessionView::naturalHeightValue() const {
	return _naturalHeight.value();
}

int SessionView::naturalHeight() const {
	return _naturalHeight.current();
}

void SessionView::refreshNaturalHeight() {
	if (_mode == Mode::NoProvider) {
		_naturalHeight = 0;
		return;
	}
	auto result = _queueStrip->height();
	if (!_composer->isHidden()) {
		result += _composer->height();
	}
	if (!_passedBar->isHidden()) {
		result += _passedBar->height();
	}
	if (!_handoffNote->isHidden() && _session && _session->isRun()) {
		result += st::agentFeedItemMargin.top() + _handoffNote->height();
	}
	_naturalHeight = result + _feed->naturalHeight();
}

void SessionView::startHandoff(HandoffDraft draft) {
	showSession(nullptr);
	_handoff = std::move(draft);
	setDraft(_handoff->text);
	refreshHandoffNote();
	relayout();
}

void SessionView::handoffRequested(const QString &text) {
	const auto session = _session;
	if (!session || _scope.kind != ScopeKind::Chat) {
		return;
	}
	const auto peer = _controller->session().data().peer(
		PeerId(_scope.peer));
	auto draft = HandoffDraft{
		.text = text,
		.untrusted = HandoffText(session->record(), peer->name()),
		.from = _scope.peer,
	};
	ShowGeneralAgent(_controller, std::move(draft));
}

void SessionView::refreshHandoffNote() {
	_handoffNote->resizeToNaturalWidth(std::max(
		width() - st::agentOverlayMargin.left()
			- st::agentOverlayMargin.right(),
		1));
	if (!_handoff) {
		_handoffNote->hide();
		return;
	}
	const auto peer = _controller->session().data().peer(
		PeerId(_handoff->from));
	_handoffNote->setText(tr::lng_agent_handoff_from(
		tr::now,
		lt_chat,
		peer->name()));
	_handoffNote->show();
}

QString SessionView::runAboutText() const {
	const auto task = _session->record().info.task;
	const auto found = _manager->tasks().find(task);
	return tr::lng_agent_run_about(
		tr::now,
		lt_task,
		found ? found->title : _session->record().info.title);
}

void SessionView::refreshProviderState() {
	_mode = _manager->available()
		? Mode::Normal
		: Mode::NoProvider;
	relayout();
}

void SessionView::refreshModel() {
	const auto &providers = _manager->providers();
	const auto run = _session && _session->isRun();
	const auto provider = run
		? nullptr
		: _session
		? FindProvider(providers, _session->profileId())
		: SessionProvider(providers, _pendingProfileId);
	_composer->setModel(provider ? ModelTitle(*provider) : QString());
}

void SessionView::showModelMenu() {
	const auto &providers = _manager->providers();
	const auto current = _session
		? _session->profileId()
		: [&] {
			const auto provider = SessionProvider(
				providers,
				_pendingProfileId);
			return provider ? provider->id : QString();
		}();
	_modelMenu = base::make_unique_q<::Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	for (const auto &provider : providers.list) {
		const auto id = provider.id;
		_modelMenu->addAction(
			ModelTitle(provider),
			crl::guard(this, [=] { chooseModel(id); }),
			(id == current) ? &st::mediaPlayerMenuCheck : nullptr);
	}
	_modelMenu->addSeparator();
	_modelMenu->addAction(
		tr::lng_agent_model_add_menu(tr::now),
		crl::guard(this, [=] {
			ShowAddModel(_controller, crl::guard(this, [=](QString id) {
				chooseModel(id);
			}));
		}),
		&st::menuIconAdd);
	_modelMenu->popup(QCursor::pos());
}

void SessionView::chooseModel(const QString &profileId) {
	if (_session) {
		_session->setProfile(profileId);
	} else {
		_pendingProfileId = profileId;
	}
	refreshModel();
}

void SessionView::rebuildFeed() {
	const auto session = _session;
	if (!session) {
		return;
	}
	_feed->rebuild(BuildFeed(
		session->record().messages,
		session->record().summaryUntil,
		session->record().cards,
		ActionToolNames()));
	applyState();
	refreshSpending();
}

void SessionView::refreshSpending() {
	auto totals = SpendingTotals();
	if (_session && _manager->providers().showSpending) {
		totals = UsageTotals(_session->record().info.usage);
	}
	_feed->setSpending(totals);
}

void SessionView::applyState() {
	const auto session = _session;
	if (!session) {
		return;
	}
	const auto &state = session->state();
	refreshModel();
	_composer->setWorking(
		state.status == SessionStatus::Working
		|| state.status == SessionStatus::Compacting);
	_composer->setQueued(state.queued);
	auto error = QString();
	auto settings = false;
	if (state.status == SessionStatus::Failed
		&& state.error
		&& state.error->kind == ErrorKind::Auth) {
		settings = true;
		const auto provider = _manager->sessionProvider(
			session->profileId());
		if (state.error->text == ClaudeMissingText()) {
			error = tr::lng_agent_claude_error_missing(tr::now);
		} else if (provider && UsesClaudeCode(*provider)) {
			error = tr::lng_agent_claude_error_login(tr::now);
		}
	}
	_feed->setStatus(state, error, settings);
}

void SessionView::sendCurrentText(const QString &text) {
	sendWithAttachments(text, {});
}

Session *SessionView::ensureSession() {
	if (_session) {
		return _session;
	}
	const auto session = _manager->create(_scope);
	if (const auto profileId = base::take(_pendingProfileId)
		; !profileId.isEmpty()) {
		session->setProfile(profileId);
	}
	showSession(session);
	return session;
}

void SessionView::sendWithAttachments(
		const QString &text,
		std::vector<Agent::AttachmentInput> attachments) {
	const auto session = ensureSession();
	_feed->scrollToBottom();
	auto handoff = base::take(_handoff);
	auto mentions = _mentions ? _mentions() : std::vector<PeerKey>();
	auto untrusted = handoff ? handoff->untrusted : QString();
	if (!_passedBar->empty()) {
		const auto &items = _passedBar->items();
		const auto peer = items.front()->history()->peer;
		if (!ranges::contains(mentions, peer->id.value)) {
			mentions.push_back(peer->id.value);
		}
		if (!untrusted.isEmpty()) {
			untrusted += u"\n\n"_q;
		}
		untrusted += PassedMessagesText(items);
		_passedBar->setItems({});
	}
	session->send({
		.text = text,
		.mentions = std::move(mentions),
		.hint = _hint ? _hint() : QString(),
		.untrusted = std::move(untrusted),
		.attachments = std::move(attachments),
	});
	_handoffNote->hide();
}

void SessionView::chooseAttach() {
	const auto callback = crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}
		const auto showError = [=](tr::phrase<> phrase) {
			_controller->showToast(phrase(tr::now));
		};
		const auto premium = _controller->session().user()->isPremium();
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			[](const Ui::PreparedList &) { return true; },
			showError,
			st::sendMediaPreviewSize,
			premium);
		if (list) {
			confirmSendingFiles(std::move(*list), QString());
		}
	});
	FileDialog::GetOpenPaths(
		this,
		tr::lng_choose_files(tr::now),
		FileDialog::AllOrImagesFilter(),
		callback,
		nullptr);
}

bool SessionView::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (_session && _session->isRun()) {
		return false;
	}
	const auto premium = _controller->session().user()->isPremium();
	if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
			confirmSendingFiles(std::move(list), QString());
			return true;
		}
	}
	if (auto read = Core::ReadMimeImage(data)) {
		auto list = Storage::PrepareMediaFromImage(
			std::move(read.image),
			std::move(read.content),
			st::sendMediaPreviewSize);
		list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
		confirmSendingFiles(std::move(list), insertTextOnCancel);
		return true;
	}
	return false;
}

bool SessionView::checkAttachmentLimits(
		const std::vector<const PreparedFile *> &files) const {
	if (files.size() > kAttachmentsPerMessage) {
		_controller->showToast(tr::lng_agent_attach_too_many(
			tr::now,
			lt_count,
			kAttachmentsPerMessage));
		return false;
	}
	for (const auto &file : files) {
		if (file->size > kAttachmentMaxBytes) {
			_controller->showToast(tr::lng_agent_attach_too_big(
				tr::now,
				lt_size,
				Agent::FormatByteSize(kAttachmentMaxBytes)));
			return false;
		}
	}
	return true;
}

void SessionView::confirmSendingFiles(
		PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_session && _session->isRun()) {
		return;
	} else if (list.error != Ui::PreparedList::Error::None) {
		_controller->showToast(tr::lng_agent_attach_failed(tr::now));
		return;
	} else if (list.files.empty()) {
		return;
	}
	auto pointers = std::vector<const PreparedFile *>();
	pointers.reserve(list.files.size());
	for (const auto &file : list.files) {
		pointers.push_back(&file);
	}
	if (!checkAttachmentLimits(pointers)) {
		return;
	}
	const auto draft = _composer->draft();
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = _controller->uiShow(),
		.list = std::move(list),
		.caption = TextWithTags{ draft, TextWithTags::Tags() },
		.toPeer = _controller->session().user(),
		.limits = DefaultLimitsForPeer(_controller->session().user()),
		.sendType = Api::SendType::Normal,
		.confirmed = crl::guard(this, [=](
				std::shared_ptr<Ui::PreparedBundle> bundle,
				Api::SendOptions,
				FullReplyTo) {
			sendingFilesConfirmed(std::move(bundle));
		}),
		.cancelled = crl::guard(this, [=] {
			_composer->setDraft(draft);
		}),
	});
	box->takeTextWithTagsRequests(
	) | rpl::on_next(crl::guard(this, [=](TextWithTags &&text) {
		_composer->setDraft(text.text);
	}), box->lifetime());
	_composer->setDraft(QString());
	Window::ActivateWindow(_controller);
	_controller->show(std::move(box));
}

void SessionView::sendingFilesConfirmed(
		std::shared_ptr<PreparedBundle> bundle) {
	Expects(bundle != nullptr);

	auto files = std::vector<const PreparedFile *>();
	for (auto &group : bundle->groups) {
		for (const auto &file : group.list.files) {
			files.push_back(&file);
		}
	}
	if (files.empty()) {
		return;
	}
	if (!checkAttachmentLimits(files)) {
		return;
	}
	auto caption = QString();
	auto inputs = std::vector<Agent::AttachmentInput>();
	for (const auto file : files) {
		if (!file->caption.text.isEmpty()) {
			if (!caption.isEmpty()) {
				caption += u"\n"_q;
			}
			caption += file->caption.text;
		}
		auto bytes = file->content;
		if (bytes.isEmpty() && !file->path.isEmpty()) {
			auto f = QFile(file->path);
			if (f.open(QIODevice::ReadOnly)) {
				bytes = f.readAll();
			}
		}
		if (bytes.isEmpty()) {
			_controller->showToast(tr::lng_agent_attach_failed(tr::now));
			continue;
		}
		const auto mime = [&] {
			if (file->information && !file->information->filemime.isEmpty()) {
				return file->information->filemime;
			}
			if (!file->path.isEmpty()) {
				return Core::MimeTypeForFile(QFileInfo(file->path)).name();
			}
			return Core::MimeTypeForData(bytes).name();
		}();
		const auto name = !file->displayName.isEmpty()
			? file->displayName
			: QFileInfo(file->path).fileName();
		const auto duration = [&] {
			if (!file->information) {
				return 0;
			}
			const auto &media = file->information->media;
			const auto ms = v::match(media,
				[](const v::null_t &) {
					return crl::time(0);
				}, [](const PreparedFileInformation::Image &) {
					return crl::time(0);
				}, [](const auto &data) {
					return data.duration;
				});
			return int(ms / 1000);
		}();
		inputs.push_back({
			.name = name,
			.mimeType = mime,
			.bytes = bytes,
			.duration = duration,
		});
	}
	if (inputs.empty()) {
		return;
	}
	sendWithAttachments(caption, std::move(inputs));
}

void SessionView::sessionGone() {
	showSession(nullptr);
}

void SessionView::relayout() {
	const auto w = width();
	const auto h = height();
	if (_mode == Mode::NoProvider) {
		_feed->hide();
		_composer->hide();
		_welcome->hide();
		_noProvider->setGeometry(rect());
		_noProvider->show();
		relayoutOverlay();
		return;
	}
	_noProvider->hide();
	const auto run = _session && _session->isRun();
	if (run) {
		_composer->hide();
		_passedBar->hide();
	} else {
		_composer->show();
		_passedBar->setVisible(!_passedBar->empty());
	}
	_queueStrip->resizeToWidth(w);
	_queueStrip->setGeometry(0, 0, w, _queueStrip->height());
	const auto stripHeight = _queueStrip->height();
	const auto content = (_contentMaxWidth > 0 && w > _contentMaxWidth)
		? _contentMaxWidth
		: w;
	if (run) {
		const auto margin = st::agentFeedItemMargin;
		_handoffNote->setText(runAboutText());
		_handoffNote->resizeToNaturalWidth(std::max(
			content - margin.left() - margin.right(),
			1));
		_handoffNote->moveToLeft(
			(w - content) / 2 + margin.left(),
			stripHeight + margin.top(),
			w);
		_handoffNote->show();
		const auto top = stripHeight + margin.top() + _handoffNote->height();
		_feed->setGeometry(0, top, w, std::max(h - top, 0));
	} else {
		const auto left = (w - content) / 2;
		_composer->resizeToWidth(content);
		const auto composerHeight = _composer->height();
		const auto passed = !_passedBar->isHidden();
		if (passed) {
			_passedBar->resizeToWidth(content);
		}
		const auto passedHeight = passed ? _passedBar->height() : 0;
		if (_composerOnTop) {
			auto top = stripHeight;
			_passedBar->moveToLeft(left, top, w);
			top += passedHeight;
			_composer->setGeometry(left, top, content, composerHeight);
			top += composerHeight;
			_feed->setGeometry(0, top, w, std::max(h - top, 0));
		} else {
			_composer->setGeometry(
				left,
				h - composerHeight,
				content,
				composerHeight);
			_passedBar->moveToLeft(
				left,
				h - composerHeight - passedHeight,
				w);
			_feed->setGeometry(
				0,
				stripHeight,
				w,
				std::max(h - stripHeight - composerHeight - passedHeight, 0));
		}
	}
	_feed->setContentMaxWidth(_contentMaxWidth);
	_feed->show();
	relayoutOverlay();
	refreshNaturalHeight();
}

void SessionView::relayoutOverlay() {
	const auto inset = st::agentOverlayMargin.left()
		+ st::agentOverlayMargin.right();
	if (_mode == Mode::NoProvider) {
		const auto w = _noProvider->width();
		const auto avail = std::max(w - inset, 0);
		_noProviderLabel->resizeToNaturalWidth(avail);
		const auto total = _noProviderLabel->height()
			+ st::agentOverlayGap
			+ _openSettings->height();
		const auto top = std::max((_noProvider->height() - total) / 2, 0);
		_noProviderLabel->move((w - _noProviderLabel->width()) / 2, top);
		_openSettings->move(
			(w - _openSettings->width()) / 2,
			top + _noProviderLabel->height() + st::agentOverlayGap);
		return;
	}
	if (_session || !_feed->empty() || _composerOnTop) {
		_welcome->hide();
		if (!_session || !_session->isRun()) {
			_handoffNote->hide();
		}
		return;
	}
	_welcome->setGeometry(_feed->geometry());
	_welcome->show();
	const auto w = _welcome->width();
	const auto avail = std::max(w - inset, 0);
	_welcomeLabel->resizeToNaturalWidth(avail);
	_handoffNote->resizeToNaturalWidth(avail);
	const auto note = _handoffNote->isHidden() ? 0
		: _handoffNote->height() + st::agentOverlayGap;
	const auto total = _welcomeLabel->height() + note;
	const auto top = std::max((_welcome->height() - total) / 2, 0);
	if (note) {
		_handoffNote->move(
			(w - _handoffNote->width()) / 2,
			top);
		_handoffNote->show();
	}
	_welcomeLabel->move(
		(w - _welcomeLabel->width()) / 2,
		top + note);
}

void SessionView::resizeEvent(QResizeEvent *e) {
	relayout();
}

} // namespace Agent::Ui