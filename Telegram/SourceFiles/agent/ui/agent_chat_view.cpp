/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_chat_view.h"

#include "agent/agent_attachments.h"
#include "agent/agent_chat.h"
#include "agent/agent_claude.h"
#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/agent_settings.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_card.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_media.h"
#include "agent/core/agent_prompts.h"
#include "agent/tools/agent_tool_format.h"
#include "agent/tools/agent_tools.h"
#include "agent/ui/agent_chat_items.h"
#include "agent/ui/agent_feed_cards.h"
#include "agent/ui/agent_feed_format.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_queue.h"
#include "agent/ui/agent_section.h"
#include "agent/ui/agent_spending_format.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/send_files_box.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "iv/markdown/iv_markdown_math_renderer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings/sections/settings_agent.h"
#include "storage/storage_folder_archive.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_agent.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Agent::Ui {
namespace {

constexpr auto kRebuildDelay = crl::time(50);

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

[[nodiscard]] const FeedCard *FindCard(
		const std::vector<ChatMessage> &messages,
		const QString &toolUseId) {
	for (const auto &message : messages) {
		if (message.card && message.card->card.toolUseId == toolUseId) {
			return &*message.card;
		}
	}
	return nullptr;
}

[[nodiscard]] CardDecision DefaultDecision(
		const Card &card,
		CardAction action) {
	auto result = CardDecision{ .action = action };
	result.checked.reserve(card.targets.size());
	for (const auto &target : card.targets) {
		result.checked.push_back(target.checked);
	}
	if (action != CardAction::Decline) {
		result.text = card.text.isEmpty() ? card.proposedText : card.text;
	}
	result.option = card.option;
	return result;
}

} // namespace

ChatView::ChatView(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Manager*> manager,
	Scope scope,
	MsgId draftRootId)
: RpWidget(parent)
, WindowListDelegate(controller)
, _controller(controller)
, _manager(manager)
, _scope(scope)
, _draftRootId(draftRootId ? draftRootId : DraftRootId(scope))
, _history(manager->chat().history())
, _queueStrip(this, manager, scope)
, _scroll(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll))
, _noProvider(this)
, _noProviderLabel(
	_noProvider,
	tr::lng_agent_no_provider(tr::now),
	st::agentFeedStatusLabel)
, _openSettings(
	_noProvider,
	tr::lng_agent_open_settings(),
	st::agentOverlayOpenSettings)
, _rebuildTimer([=] { rebuildMessages(); }) {
	_items = std::make_unique<ChatItems>(_history, ChatItemsDelegate{
		.canUndo = [=](const QString &id) {
			return _session && _session->canUndo(id);
		},
		.isUndone = [=](const QString &id) {
			return _session
				&& ranges::contains(_session->record().undone, id);
		},
		.isRun = [=] {
			return _session && _session->isRun();
		},
		.date = [=](int index) {
			return messageDate(index);
		},
		.passed = [=](int index) {
			return passedMessages(index);
		},
		.attachment = [=](const QString &id) -> std::optional<MediaBlock> {
			return _session
				? _manager->attachments().read(_session->id(), id)
				: std::nullopt;
		},
	});
	_items->buttons(
	) | rpl::on_next([=](const ChatButton &button) {
		handleButton(button);
	}, lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		_history->peer
	) | rpl::on_next([=](std::shared_ptr<::Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		update();
	}, lifetime());

	setupList();
	setupComposeControls();
	setupDragArea();

	_openSettings->setClickedCallback([=] {
		_controller->showSettings(Settings::AgentId());
	});
	_manager->enabledValue(
	) | rpl::on_next([=] {
		refreshProviderState();
	}, lifetime());
	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		refreshProviderState();
		refreshModel();
		scheduleRebuild();
	}, lifetime());

	_queueStrip->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateControlsGeometry();
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
	rebuildMessages();
}

ChatView::~ChatView() {
	_sessionLifetime.destroy();
	_composeControls = nullptr;
	_items = nullptr;
}

void ChatView::setupList() {
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_inner = _scroll->setOwnedWidget(object_ptr<::HistoryView::ListWidget>(
		this,
		&_controller->session(),
		static_cast<::HistoryView::ListDelegate*>(this)));
	_inner->lower();
	_scroll->setOverscrollEdges([=] {
		return _inner->loadedAtTopKnown() && _inner->loadedAtTop();
	}, [=] {
		return _inner->loadedAtBottomKnown() && _inner->loadedAtBottom();
	});
	_scroll->scrolls(
	) | rpl::on_next([=] {
		if (!_skipScrollEvent) {
			updateInnerVisibleArea();
		}
	}, lifetime());
	_controller->chatStyle()->paletteChanged(
	) | rpl::on_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());
	_inner->heightValue(
	) | rpl::skip(1) | rpl::filter([=] {
		return _composerOnTop;
	}) | rpl::on_next([=] {
		refreshNaturalHeight();
	}, _inner->lifetime());
	_cornerButtons = std::make_unique<::HistoryView::CornerButtons>(
		_scroll.data(),
		_controller->chatStyle(),
		static_cast<::HistoryView::CornerButtonsDelegate*>(this));
	_scroll->show();
}

void ChatView::setupComposeControls() {
	using namespace HistoryView;
	_composeControls = std::make_unique<ComposeControls>(
		this,
		ComposeControlsDescriptor{
			.show = _controller->uiShow(),
			.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
				listShowPremiumToast(emoji);
			},
			.mode = ComposeControls::Mode::Normal,
			.sendMenuDetails = [] { return SendMenu::Details(); },
			.regularWindow = _controller,
			.stickerOrEmojiChosen = _controller->stickerOrEmojiChosen(),
			.customPlaceholder = tr::lng_agent_input_placeholder(),
			.features = {
				.sendAs = false,
				.ttlInfo = false,
				.attachments = true,
				.botCommandSend = false,
				.silentBroadcastToggle = false,
				.attachBotsMenu = false,
				.inlineBots = false,
				.megagroupSet = false,
				.stickersSettings = false,
				.openStickerSets = false,
				.autocompleteHashtags = false,
				.autocompleteMentions = false,
				.autocompleteCommands = false,
				.suggestStickersByEmoji = false,
				.commonTabbedPanel = false,
				.recordMediaMessage = false,
				.emojiOnlyPanel = true,
				.richEditor = false,
			},
			.customMenuText = _modelTitle.value(),
			.customMenuClicked = [=] { showModelMenu(); },
			.customStopShown = _working.value(),
			.customStopClicked = [=] {
				if (_session) {
					_session->stop();
				}
			},
		});
	_composeControls->setHistory({
		.history = _history.get(),
		.topicRootId = _draftRootId,
		.writeRestriction = rpl::single(
			rpl::empty
		) | rpl::then(
			_sessionChanges.events() | rpl::to_empty
		) | rpl::map([=]() -> ::HistoryView::Controls::WriteRestriction {
			using Restriction = ::HistoryView::Controls::WriteRestriction;
			if (!_session || !_session->isRun()) {
				return Restriction();
			}
			const auto task = _session->record().info.task;
			const auto found = _manager->tasks().find(task);
			return Restriction{
				.text = tr::lng_agent_run_about(
					tr::now,
					lt_task,
					found ? found->title : _session->record().info.title),
				.type = ::HistoryView::Controls::WriteRestrictionType::Rights,
			};
		}),
	});
	_composeControls->setPasteToastParent(_scroll.data());
	_composeControls->height(
	) | rpl::on_next([=] {
		const auto wasMax = atBottom();
		updateControlsGeometry();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());
	_composeControls->sendRequests(
	) | rpl::on_next([=] {
		send();
	}, lifetime());
	_composeControls->cancelRequests(
	) | rpl::on_next([=] {
		listCancelRequest();
	}, lifetime());
	_composeControls->attachRequests(
	) | rpl::on_next([=] {
		chooseAttach();
	}, lifetime());
	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			::Ui::InputField::MimeAction action) {
		if (action == ::Ui::InputField::MimeAction::Check) {
			return Core::CanSendFiles(data);
		} else if (action == ::Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));
		}
		Unexpected("action in the agent compose mime data hook.");
	});
	rpl::merge(
		_composeControls->scrollKeyEvents(),
		_inner->scrollKeyEvents()
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());
	_composeControls->viewportEvents(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());
}

void ChatView::setupDragArea() {
	const auto dragAreas = DragArea::SetupDragAreaToContainer(
		this,
		crl::guard(this, [=](not_null<const QMimeData*>) {
			return !(_session && _session->isRun()) && !_noProviderMode;
		}),
		nullptr,
		[=] { updateControlsGeometry(); });
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
			auto list = ::Ui::PreparedList();
			list.files.push_back(Storage::PrepareFilesArchive(urls));
			confirmSendingFiles(std::move(list), QString());
		}
		Window::ActivateWindow(_controller);
	});
}

void ChatView::showSession(Session *session) {
	if (_session == session) {
		return;
	}
	_sessionLifetime.destroy();
	_session = session;
	_tail.clear();
	_pendingProfileId = QString();
	_queueStrip->setSuppressed(session && session->isRun());
	if (session) {
		_handoff.reset();
		std::move(session->recordChanges()
		) | rpl::on_next_error_done([=] {
			_tail.clear();
			rebuildMessages();
		}, [](rpl::no_error) {
		}, [=] {
			showSession(nullptr);
		}, _sessionLifetime);
		rpl::merge(
			session->stateChanges(),
			session->undoChanges()
		) | rpl::on_next([=] {
			const auto &state = _session->state();
			_working = (state.status == SessionStatus::Working)
				|| (state.status == SessionStatus::Compacting);
			refreshModel();
			scheduleRebuild();
		}, _sessionLifetime);
		std::move(session->live()
		) | rpl::on_next([=](const EngineEvent &event) {
			_tail.apply(event);
			scheduleRebuild();
		}, _sessionLifetime);
		std::move(session->undoErrors()
		) | rpl::on_next([=](const QString &error) {
			_controller->showToast(tr::lng_agent_undo_failed(
				tr::now,
				lt_error,
				error));
		}, _sessionLifetime);
		const auto &state = session->state();
		_working = (state.status == SessionStatus::Working)
			|| (state.status == SessionStatus::Compacting);
	} else {
		_working = false;
	}
	_items->clear();
	_slices.fire({});
	rebuildMessages();
	refreshModel();
	_composeControls->updateForwarding();
	_inner->showAtPosition(Data::MaxMessagePosition, {});
	_sessionChanges.fire_copy(_session);
}

Session *ChatView::session() const {
	return _session;
}

rpl::producer<Session*> ChatView::sessionChanges() const {
	return _sessionChanges.events();
}

void ChatView::setHintProvider(Fn<QString()> hint) {
	_hint = std::move(hint);
}

void ChatView::setMentionsProvider(Fn<std::vector<PeerKey>()> mentions) {
	_mentions = std::move(mentions);
}

void ChatView::setInnerFocus() {
	if (_noProviderMode || !_composeControls->focus()) {
		_inner->setFocus();
	}
}

void ChatView::startHandoff(HandoffDraft draft) {
	showSession(nullptr);
	_handoff = std::move(draft);
	_composeControls->setText({ _handoff->text, {} });
	rebuildMessages();
}

void ChatView::passMessages(HistoryItemsList items) {
	if (items.empty()) {
		return;
	}
	auto ids = MessageIdsList();
	ids.reserve(items.size());
	for (const auto &item : items) {
		ids.push_back(item->fullId());
	}
	_history->setForwardDraft(MsgId(), PeerId(), { .ids = std::move(ids) });
	_composeControls->updateForwarding();
}

void ChatView::setComposerOnTop(bool onTop) {
	_composerOnTop = onTop;
	updateControlsGeometry();
}

void ChatView::setBottomRadius(int radius) {
	_bottomRadius = radius;
	update();
}

bool ChatView::hasMessages() const {
	return !_items->ids().empty();
}

rpl::producer<int> ChatView::naturalHeightValue() const {
	return _naturalHeight.value();
}

int ChatView::naturalHeight() const {
	return _naturalHeight.current();
}

MsgId ChatView::DraftRootId(Scope scope) {
	return (scope.kind == ScopeKind::Chat)
		? MsgId(1 + int64(scope.peer % 0x3FFFFFFFULL))
		: MsgId();
}

MsgId ChatView::PaletteDraftRootId() {
	return MsgId(0x40000000);
}

QRect ChatView::listGeometry() const {
	return _scroll->geometry();
}

void ChatView::showStarted() {
	_composeControls->showStarted();
}

void ChatView::showFinished() {
	_composeControls->showFinished();
	_inner->showFinished();
}

void ChatView::refreshProviderState() {
	_noProviderMode = !_manager->available();
	updateControlsGeometry();
}

void ChatView::refreshModel() {
	const auto &providers = _manager->providers();
	const auto run = _session && _session->isRun();
	const auto provider = run
		? nullptr
		: _session
		? FindProvider(providers, _session->profileId())
		: SessionProvider(providers, _pendingProfileId);
	_modelTitle = provider ? ModelTitle(*provider) : QString();
}

void ChatView::showModelMenu() {
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

void ChatView::chooseModel(const QString &profileId) {
	if (_session) {
		_session->setProfile(profileId);
	} else {
		_pendingProfileId = profileId;
	}
	refreshModel();
}

void ChatView::scheduleRebuild() {
	if (!_rebuildTimer.isActive()) {
		_rebuildTimer.callOnce(kRebuildDelay);
	}
}

TimeId ChatView::messageDate(int index) const {
	if (!_session || index < 0) {
		return base::unixtime::now();
	}
	const auto &record = _session->record();
	if (index < int(record.messages.size())
		&& record.messages[index].date > 0) {
		return TimeId(record.messages[index].date);
	} else if (index < int(record.messages.size())) {
		return TimeId(record.info.created);
	}
	return base::unixtime::now();
}

std::vector<FullMsgId> ChatView::passedMessages(int index) {
	auto result = std::vector<FullMsgId>();
	if (!_session || index < 0) {
		return result;
	}
	const auto &record = _session->record();
	if (index >= int(record.messages.size())) {
		return result;
	}
	static const auto marker = u"Messages the user passed to you:\n"_q;
	for (const auto &block : record.messages[index].blocks) {
		const auto text = std::get_if<TextBlock>(&block);
		if (!text || !text->hidden) {
			continue;
		}
		const auto unwrapped = UnwrapUntrusted(text->text);
		const auto position = unwrapped.indexOf(marker);
		if (position < 0) {
			continue;
		}
		const auto json = QJsonDocument::fromJson(
			unwrapped.mid(position + marker.size()).toUtf8()).object();
		const auto peerId = PeerId(uint64(
			json.value(u"chat"_q).toObject().value(u"id"_q).toDouble()));
		if (!peerId) {
			continue;
		}
		for (const auto value : json.value(u"messages"_q).toArray()) {
			const auto id = FullMsgId(
				peerId,
				MsgId(int64(value.toObject().value(u"id"_q).toDouble())));
			result.push_back(id);
			const auto owner = &_controller->session().data();
			if (!owner->message(id) && _requestedPassed.emplace(id).second) {
				_controller->session().api().requestMessageData(
					owner->peer(peerId),
					id.msg,
					crl::guard(this, [=] { scheduleRebuild(); }));
			}
		}
	}
	return result;
}

QString ChatView::errorText() const {
	if (!_session) {
		return QString();
	}
	const auto &state = _session->state();
	if (state.status != SessionStatus::Failed || !state.error) {
		return QString();
	} else if (state.error->kind == ErrorKind::Auth) {
		const auto provider = _manager->sessionProvider(
			_session->profileId());
		if (state.error->text == ClaudeMissingText()) {
			return tr::lng_agent_claude_error_missing(tr::now);
		} else if (provider && UsesClaudeCode(*provider)) {
			return tr::lng_agent_claude_error_login(tr::now);
		}
		return tr::lng_agent_failed_auth(
			tr::now,
			lt_error,
			state.error->text);
	}
	return tr::lng_agent_failed(tr::now, lt_error, state.error->text);
}

QString ChatView::turnEndText() const {
	if (!_session) {
		return QString();
	}
	const auto &state = _session->state();
	if (state.status != SessionStatus::Idle || !state.lastEnd) {
		return QString();
	}
	switch (*state.lastEnd) {
	case TurnEnd::Interrupted:
		return tr::lng_agent_turn_interrupted(tr::now);
	case TurnEnd::StepLimit:
		return tr::lng_agent_turn_step_limit(tr::now);
	case TurnEnd::MaxTokens:
		return tr::lng_agent_turn_max_tokens(tr::now);
	case TurnEnd::Refusal:
		return tr::lng_agent_turn_refusal(tr::now);
	case TurnEnd::Completed:
	case TurnEnd::Failed:
		break;
	}
	return QString();
}

QString ChatView::spendingText() const {
	if (!_session || !_manager->providers().showSpending) {
		return QString();
	}
	const auto totals = UsageTotals(_session->record().info.usage);
	const auto text = FormatSpending(totals, true);
	return text.isEmpty()
		? QString()
		: tr::lng_agent_spending_session(tr::now, lt_text, text);
}

QString ChatView::noteText() const {
	if (_handoff) {
		const auto peer = _controller->session().data().peer(
			PeerId(_handoff->from));
		return tr::lng_agent_handoff_from(tr::now, lt_chat, peer->name());
	}
	return QString();
}

void ChatView::rebuildMessages() {
	_rebuildTimer.cancel();
	const auto wasAtBottom = atBottom();
	auto messages = std::vector<Message>();
	auto summaryUntil = 0;
	auto cards = std::vector<Card>();
	if (_session) {
		const auto &record = _session->record();
		messages = record.messages;
		summaryUntil = record.summaryUntil;
		cards = record.cards;
		const auto tail = _tail.messages();
		messages.insert(end(messages), begin(tail), end(tail));
	}
	_messages = BuildChatMessages(BuildFeed(
		messages,
		summaryUntil,
		cards,
		ActionToolNames()));
	for (auto i = 0; i != int(messages.size()); ++i) {
		const auto user = (messages[i].role == Role::User);
		if (!user || passedMessages(i).empty()) {
			continue;
		}
		const auto has = ranges::any_of(_messages, [&](const ChatMessage &m) {
			return (m.kind == ChatMessageKind::User) && (m.index == i);
		});
		if (!has) {
			const auto position = ranges::find_if(_messages, [&](
					const ChatMessage &m) {
				return m.index > i;
			});
			_messages.insert(position, ChatMessage{
				.kind = ChatMessageKind::User,
				.key = u"u"_q + QString::number(i),
				.index = i,
			});
		}
	}
	auto extras = ChatExtras{
		.note = noteText(),
		.spending = spendingText(),
	};
	if (_session) {
		const auto &state = _session->state();
		extras.compacting = (state.status == SessionStatus::Compacting);
		extras.queued = state.queued;
		extras.live = (state.status == SessionStatus::Working);
		extras.error = errorText();
		extras.turnEnd = turnEndText();
		extras.errorSettings = !extras.error.isEmpty()
			&& state.error
			&& (state.error->kind == ErrorKind::Auth);
	}
	if (_items->apply(_messages, extras)) {
		_slices.fire({});
	}
	refreshNaturalHeight();
	scrollToBottomIfStuck(wasAtBottom);
}

bool ChatView::atBottom() const {
	return _scroll->scrollTop() >= _scroll->scrollTopMax();
}

void ChatView::scrollToBottomIfStuck(bool wasAtBottom) {
	if (!wasAtBottom) {
		return;
	}
	crl::on_main(this, [=] {
		listScrollTo(_scroll->scrollTopMax());
	});
}

void ChatView::handleButton(const ChatButton &button) {
	if (!_session) {
		return;
	}
	const auto session = _session;
	const auto decide = [&](CardAction action) {
		if (const auto card = FindCard(_messages, button.id)) {
			session->decideCard(
				button.id,
				DefaultDecision(card->card, action));
		}
	};
	switch (button.action) {
	case ChatButtonAction::Execute: decide(CardAction::Execute); break;
	case ChatButtonAction::Alternate: decide(CardAction::Alternate); break;
	case ChatButtonAction::Decline: decide(CardAction::Decline); break;
	case ChatButtonAction::Edit: showCardBox(button.id); break;
	case ChatButtonAction::Handoff:
		if (const auto card = FindCard(_messages, button.id)) {
			handoffRequested(card->card.text.isEmpty()
				? card->card.proposedText
				: card->card.text);
		}
		break;
	case ChatButtonAction::Undo:
		session->undo({ button.id });
		break;
	case ChatButtonAction::UndoAll: {
		auto ids = std::vector<QString>();
		for (const auto &id : button.id.split(',')) {
			ids.push_back(id);
		}
		session->undo(ids);
	} break;
	case ChatButtonAction::Retry:
		session->retry();
		break;
	case ChatButtonAction::ModelSettings:
		ShowEditModel(_controller, session->profileId());
		break;
	case ChatButtonAction::ShowInFolder:
		File::ShowInFolder(button.id);
		break;
	}
}

void ChatView::showCardBox(const QString &toolUseId) {
	const auto card = FindCard(_messages, toolUseId);
	if (!card || !_session) {
		return;
	}
	const auto data = *card;
	const auto weak = base::make_weak(_session);
	_controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		const auto close = crl::guard(box, [=] { box->closeBox(); });
		box->addRow(
			object_ptr<FeedCardWidget>(box, FeedContext{
				.controller = _controller,
				.renderer = std::make_shared<
					Iv::Markdown::MathRenderer>(),
				.scrollBy = [](int) {},
				.delegate = {
					.decideCard = [=](
							const QString &id,
							CardDecision decision) {
						if (const auto strong = weak.get()) {
							strong->decideCard(id, std::move(decision));
						}
						close();
					},
					.canUndo = [](const QString &) { return false; },
					.isUndone = [](const QString &) { return false; },
					.undo = [](const std::vector<QString> &) {},
					.startHandoff = [=](const QString &text) {
						close();
						handoffRequested(text);
					},
					.isRun = [=] {
						const auto strong = weak.get();
						return strong && strong->isRun();
					},
				},
			}, data),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				0));
		box->addButton(tr::lng_close(), close);
	}));
}

void ChatView::send() {
	const auto text = _composeControls->getTextWithAppliedMarkdown().text;
	const auto passed = _history->resolveForwardDraft(MsgId(), PeerId());
	if (text.trimmed().isEmpty() && passed.items.empty()) {
		return;
	}
	sendWithAttachments(text.trimmed(), {});
	_composeControls->clear();
}

Session *ChatView::ensureSession() {
	if (_session) {
		return _session;
	}
	const auto session = _manager->create(_scope);
	if (const auto profileId = base::take(_pendingProfileId)
		; !profileId.isEmpty()) {
		session->setProfile(profileId);
	}
	auto handoff = base::take(_handoff);
	showSession(session);
	_handoff = std::move(handoff);
	return session;
}

void ChatView::sendWithAttachments(
		const QString &text,
		std::vector<Agent::AttachmentInput> attachments) {
	const auto session = ensureSession();
	auto handoff = base::take(_handoff);
	auto mentions = _mentions ? _mentions() : std::vector<PeerKey>();
	auto untrusted = handoff ? handoff->untrusted : QString();
	const auto passed = _history->resolveForwardDraft(MsgId(), PeerId());
	if (!passed.items.empty()) {
		const auto peer = passed.items.front()->history()->peer;
		if (!ranges::contains(mentions, peer->id.value)) {
			mentions.push_back(peer->id.value);
		}
		if (!untrusted.isEmpty()) {
			untrusted += u"\n\n"_q;
		}
		untrusted += PassedMessagesText(passed.items);
		_history->setForwardDraft(MsgId(), PeerId(), {});
		_composeControls->updateForwarding();
	}
	session->send({
		.text = text,
		.mentions = std::move(mentions),
		.hint = _hint ? _hint() : QString(),
		.untrusted = std::move(untrusted),
		.attachments = std::move(attachments),
	});
	listScrollTo(_scroll->scrollTopMax());
}

void ChatView::handoffRequested(const QString &text) {
	const auto session = _session;
	if (!session || _scope.kind != ScopeKind::Chat) {
		return;
	}
	const auto peer = _controller->session().data().peer(
		PeerId(_scope.peer));
	ShowGeneralAgent(_controller, HandoffDraft{
		.text = text,
		.untrusted = HandoffText(session->record(), peer->name()),
		.from = _scope.peer,
	});
}

void ChatView::chooseAttach() {
	if (_session && _session->isRun()) {
		return;
	}
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
			[](const ::Ui::PreparedList &) { return true; },
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

bool ChatView::confirmSendingFiles(
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
		if (list.error != ::Ui::PreparedList::Error::NonLocalUrl) {
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

bool ChatView::checkAttachmentLimits(
		const std::vector<const ::Ui::PreparedFile*> &files) const {
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

void ChatView::confirmSendingFiles(
		::Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_session && _session->isRun()) {
		return;
	} else if (list.error != ::Ui::PreparedList::Error::None) {
		_controller->showToast(tr::lng_agent_attach_failed(tr::now));
		return;
	} else if (list.files.empty()) {
		return;
	}
	auto pointers = std::vector<const ::Ui::PreparedFile*>();
	pointers.reserve(list.files.size());
	for (const auto &file : list.files) {
		pointers.push_back(&file);
	}
	if (!checkAttachmentLimits(pointers)) {
		return;
	}
	const auto draft = _composeControls->getTextWithAppliedMarkdown();
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = _controller->uiShow(),
		.list = std::move(list),
		.caption = draft,
		.toPeer = _controller->session().user(),
		.limits = DefaultLimitsForPeer(_controller->session().user()),
		.sendType = Api::SendType::Normal,
		.confirmed = crl::guard(this, [=](
				std::shared_ptr<::Ui::PreparedBundle> bundle,
				Api::SendOptions,
				FullReplyTo) {
			sendingFilesConfirmed(std::move(bundle));
		}),
		.cancelled = crl::guard(this, [=] {
			_composeControls->setText(draft);
		}),
	});
	box->takeTextWithTagsRequests(
	) | rpl::on_next(crl::guard(this, [=](TextWithTags &&text) {
		_composeControls->setText(text);
	}), box->lifetime());
	_composeControls->clear();
	Window::ActivateWindow(_controller);
	_controller->show(std::move(box));
}

void ChatView::sendingFilesConfirmed(
		std::shared_ptr<::Ui::PreparedBundle> bundle) {
	Expects(bundle != nullptr);

	auto files = std::vector<const ::Ui::PreparedFile*>();
	for (auto &group : bundle->groups) {
		for (const auto &file : group.list.files) {
			files.push_back(&file);
		}
	}
	if (files.empty() || !checkAttachmentLimits(files)) {
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
			if (file->information
				&& !file->information->filemime.isEmpty()) {
				return file->information->filemime;
			} else if (!file->path.isEmpty()) {
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
			const auto ms = v::match(file->information->media, [](
					const v::null_t &) {
				return crl::time(0);
			}, [](const ::Ui::PreparedFileInformation::Image &) {
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
	if (!inputs.empty()) {
		sendWithAttachments(caption, std::move(inputs));
	}
}

void ChatView::refreshNaturalHeight() {
	if (!_composeControls) {
		return;
	}
	auto result = _queueStrip->height() + _composeControls->heightCurrent();
	if (!_items->ids().empty()) {
		result += _composerOnTop
			? std::min(_inner->height(), st::agentPaletteNaturalList)
			: st::agentPaletteNaturalList;
	}
	_naturalHeight = result;
}

void ChatView::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void ChatView::updateControlsGeometry() {
	if (!_composeControls || !width() || !height()) {
		return;
	}
	const auto w = width();
	const auto h = height();
	_queueStrip->resizeToWidth(w);
	_queueStrip->moveToLeft(0, 0, w);
	const auto stripHeight = _queueStrip->height();
	if (_noProviderMode) {
		_scroll->hide();
		_composeControls->hide();
		_noProvider->setGeometry(0, stripHeight, w, h - stripHeight);
		_noProvider->show();
		const auto avail = std::max(
			w - st::agentOverlayMargin.left() - st::agentOverlayMargin.right(),
			0);
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
	_noProvider->hide();
	_scroll->show();
	_composeControls->show();
	_composeControls->resizeToWidth(w);
	const auto controlsHeight = _composeControls->heightCurrent();
	const auto scrollTop = _composerOnTop
		? (stripHeight + controlsHeight)
		: stripHeight;
	const auto scrollHeight = std::max(h - stripHeight - controlsHeight, 0);
	const auto scrollSize = QSize(w, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(
			scrollSize.width(),
			_composerOnTop ? 0 : _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, scrollTop);
	_composeControls->move(
		0,
		_composerOnTop ? stripHeight : (h - controlsHeight));
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());
	updateInnerVisibleArea();
	_cornerButtons->updatePositions();
	refreshNaturalHeight();
}

void ChatView::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_cornerButtons->updateJumpDownVisibility();
}

void ChatView::paintEvent(QPaintEvent *e) {
	const auto clip = e->rect();
	if (!_theme) {
		QPainter(this).fillRect(clip, st::windowBg);
		return;
	} else if (_bottomRadius <= 0) {
		Window::SectionWidget::PaintBackground(
			_controller,
			_theme.get(),
			this,
			clip);
		return;
	}
	auto p = QPainter(this);
	const auto strip = QRect(
		0,
		height() - _bottomRadius,
		width(),
		_bottomRadius);
	const auto top = clip.intersected(QRect(0, 0, width(), strip.y()));
	if (!top.isEmpty()) {
		p.setClipRect(top);
		Window::SectionWidget::PaintBackground(p, _theme.get(), size(), top);
		p.setClipping(false);
	}
	if (!clip.intersects(strip)) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		strip.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		q.translate(0, -strip.y());
		Window::SectionWidget::PaintBackground(q, _theme.get(), size(), strip);
	}
	p.drawImage(strip.topLeft(), Images::Round(
		std::move(image),
		Images::CornersMask(_bottomRadius),
		RectPart::BottomLeft | RectPart::BottomRight));
}

::HistoryView::Context ChatView::listContext() {
	return Context::Replies;
}

bool ChatView::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void ChatView::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		_inner->cancelSelection();
		return;
	}
	_composeControls->handleCancelRequest();
}

void ChatView::listDeleteRequest() {
}

void ChatView::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

rpl::producer<Data::MessagesSlice> ChatView::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return rpl::single(rpl::empty) | rpl::then(
		_slices.events()
	) | rpl::map([=] {
		auto result = Data::MessagesSlice();
		result.ids = _items->ids();
		result.skippedBefore = 0;
		result.skippedAfter = 0;
		result.fullCount = int(result.ids.size());
		return result;
	});
}

bool ChatView::listAllowsMultiSelect() {
	return false;
}

bool ChatView::listIsItemGoodForSelection(not_null<HistoryItem*> item) {
	return false;
}

bool ChatView::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void ChatView::listSelectionChanged(::HistoryView::SelectedItems &&items) {
}

void ChatView::listMarkReadTill(not_null<HistoryItem*> item) {
}

void ChatView::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

::HistoryView::MessagesBarData ChatView::listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void ChatView::listContentRefreshed() {
}

void ChatView::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool ChatView::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool ChatView::listElementShownUnread(not_null<const Element*> view) {
	return view->data()->isSending();
}

bool ChatView::listIsGoodForAroundPosition(not_null<const Element*> view) {
	return true;
}

void ChatView::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void ChatView::listSearch(const QString &query, const FullMsgId &context) {
}

void ChatView::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<::Ui::ChatTheme*> ChatView::listChatTheme() {
	if (!_theme) {
		_theme = std::make_shared<::Ui::ChatTheme>();
	}
	return _theme.get();
}

::HistoryView::CopyRestrictionType ChatView::listCopyRestrictionType(
		HistoryItem *item) {
	return ::HistoryView::CopyRestrictionType::None;
}

::HistoryView::CopyRestrictionType ChatView::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return ::HistoryView::CopyRestrictionType::None;
}

::HistoryView::CopyRestrictionType ChatView::listSelectRestrictionType() {
	return ::HistoryView::CopyRestrictionType::None;
}

auto ChatView::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void ChatView::listShowPremiumToast(not_null<DocumentData*> document) {
}

void ChatView::listOpenPhoto(not_null<PhotoData*> photo, FullMsgId context) {
	_controller->openPhoto(photo, { .id = context });
}

void ChatView::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_controller->openDocument(document, showInMediaView, { .id = context });
}

void ChatView::listPaintEmpty(
		Painter &p,
		const ::Ui::ChatPaintContext &context) {
	if (_composerOnTop) {
		return;
	}
	const auto text = (_scope.kind == ScopeKind::Chat)
		? tr::lng_agent_chat_welcome(tr::now)
		: tr::lng_agent_general_welcome(tr::now);
	const auto padding = st::msgServicePadding;
	const auto maxWidth = std::max(
		std::min(
			st::agentChatEmptyWidth,
			_inner->width() - 2 * padding.left()),
		1);
	auto string = ::Ui::Text::String(st::serviceTextStyle, text);
	const auto textWidth = std::min(string.maxWidth(), maxWidth);
	const auto textHeight = string.countHeight(textWidth);
	const auto width = textWidth + padding.left() + padding.right();
	const auto height = textHeight + padding.top() + padding.bottom();
	const auto rect = QRect(
		(_inner->width() - width) / 2,
		(_inner->height() - height) / 2,
		width,
		height);
	::HistoryView::ServiceMessagePainter::PaintBubble(p, context.st, rect);
	p.setPen(context.st->msgServiceFg());
	string.draw(
		p,
		rect.x() + padding.left(),
		rect.y() + padding.top(),
		textWidth,
		style::al_top);
}

QString ChatView::listElementAuthorRank(not_null<const Element*> view) {
	return QString();
}

bool ChatView::listElementHideTopicButton(not_null<const Element*> view) {
	return true;
}

History *ChatView::listTranslateHistory() {
	return nullptr;
}

void ChatView::listAddTranslatedItems(
	not_null<::HistoryView::TranslateTracker*> tracker) {
}

::Ui::ElasticScroll *ChatView::listScrollArea() const {
	return _scroll.data();
}

bool ChatView::listThanosEffectEnabled() const {
	return false;
}

bool ChatView::listIgnorePaintEvent(QWidget *w, QPaintEvent *e) {
	// WHY: the layer that holds the view counts as covering it.
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (dynamic_cast<::Ui::LayerWidget*>(parent)) {
			return false;
		}
	}
	return WindowListDelegate::listIgnorePaintEvent(w, e);
}

void ChatView::cornerButtonsShowAtPosition(Data::MessagePosition position) {
	_inner->showAtPosition(position, {});
}

Data::Thread *ChatView::cornerButtonsThread() {
	return _history;
}

FullMsgId ChatView::cornerButtonsCurrentId() {
	return {};
}

bool ChatView::cornerButtonsIgnoreVisibility() {
	return false;
}

std::optional<bool> ChatView::cornerButtonsDownShown() {
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	return (top < _scroll->scrollTopMax());
}

bool ChatView::cornerButtonsUnreadMayBeShown() {
	return false;
}

bool ChatView::cornerButtonsHas(::HistoryView::CornerButtonType type) {
	return (type == ::HistoryView::CornerButtonType::Down);
}

} // namespace Agent::Ui
