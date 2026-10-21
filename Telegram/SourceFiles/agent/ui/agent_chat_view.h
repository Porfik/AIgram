/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_chat_messages.h"
#include "agent/core/agent_scope.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/view/history_view_list_widget.h"
#include "ui/rp_widget.h"

#include <optional>

namespace Ui {
class ChatTheme;
class ElasticScroll;
class FlatLabel;
class PopupMenu;
class RoundButton;
struct PreparedBundle;
struct PreparedFile;
struct PreparedList;
} // namespace Ui

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace Agent {
class Manager;
class Session;
struct AttachmentInput;
} // namespace Agent

namespace Agent::Ui {

using namespace ::Ui;

struct HandoffDraft {
	QString text;
	QString untrusted;
	PeerKey from = 0;
};

class ChatItems;
class QueueStrip;
struct ChatButton;

class ChatView final
	: public ::Ui::RpWidget
	, private ::HistoryView::WindowListDelegate
	, private ::HistoryView::CornerButtonsDelegate {
public:
	ChatView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);
	~ChatView();

	[[nodiscard]] static MsgId DraftRootId(Scope scope);

	void showSession(Session *session);
	[[nodiscard]] Session *session() const;
	[[nodiscard]] rpl::producer<Session*> sessionChanges() const;

	void setHintProvider(Fn<QString()> hint);
	void setMentionsProvider(Fn<std::vector<PeerKey>()> mentions);
	void setInnerFocus();
	void startHandoff(HandoffDraft draft);
	void passMessages(HistoryItemsList items);
	void setNewSessionDraftKey(const QString &key);
	void setComposerOnTop(bool onTop);
	void setBottomRadius(int radius);
	[[nodiscard]] bool hasMessages() const;
	[[nodiscard]] rpl::producer<int> naturalHeightValue() const;
	[[nodiscard]] int naturalHeight() const;
	[[nodiscard]] QRect listGeometry() const;

	void showStarted();
	void showFinished();

private:
	using Element = ::HistoryView::Element;
	using Context = ::HistoryView::Context;

	void setupList();
	void setupComposeControls();
	void setupDragArea();
	void refreshProviderState();
	void refreshModel();
	void showModelMenu();
	void chooseModel(const QString &profileId);
	void rebuildMessages();
	void scheduleRebuild();
	void handleButton(const ChatButton &button);
	void showCardBox(const QString &toolUseId);
	void send();
	void sendWithAttachments(
		const QString &text,
		std::vector<Agent::AttachmentInput> attachments);
	void chooseAttach();
	[[nodiscard]] bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel);
	void confirmSendingFiles(
		::Ui::PreparedList &&list,
		const QString &insertTextOnCancel);
	void sendingFilesConfirmed(std::shared_ptr<::Ui::PreparedBundle> bundle);
	[[nodiscard]] bool checkAttachmentLimits(
		const std::vector<const ::Ui::PreparedFile*> &files) const;
	[[nodiscard]] Session *ensureSession();
	[[nodiscard]] QString currentDraftKey() const;
	void saveDraft();
	void switchDraft();
	void handoffRequested(const QString &text);
	[[nodiscard]] QString errorText() const;
	[[nodiscard]] QString turnEndText() const;
	[[nodiscard]] QString spendingText() const;
	[[nodiscard]] QString noteText() const;
	[[nodiscard]] TimeId messageDate(int index) const;
	[[nodiscard]] std::vector<FullMsgId> passedMessages(int index);
	void updateControlsGeometry();
	void updateInnerVisibleArea();
	void refreshNaturalHeight();
	void scrollToBottomIfStuck(bool wasAtBottom);
	[[nodiscard]] bool atBottom() const;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(::HistoryView::SelectedItems &&items) override;
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	::HistoryView::MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listElementShownUnread(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(
		not_null<const Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<::Ui::ChatTheme*> listChatTheme() override;
	::HistoryView::CopyRestrictionType listCopyRestrictionType(
		HistoryItem *item) override;
	::HistoryView::CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	::HistoryView::CopyRestrictionType listSelectRestrictionType() override;
	auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	void listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) override;
	void listPaintEmpty(
		Painter &p,
		const ::Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(not_null<const Element*> view) override;
	bool listElementHideTopicButton(not_null<const Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<::HistoryView::TranslateTracker*> tracker) override;
	::Ui::ElasticScroll *listScrollArea() const override;
	bool listThanosEffectEnabled() const override;
	bool listIgnorePaintEvent(QWidget *w, QPaintEvent *e) override;

	void cornerButtonsShowAtPosition(
		Data::MessagePosition position) override;
	Data::Thread *cornerButtonsThread() override;
	FullMsgId cornerButtonsCurrentId() override;
	bool cornerButtonsIgnoreVisibility() override;
	std::optional<bool> cornerButtonsDownShown() override;
	bool cornerButtonsUnreadMayBeShown() override;
	bool cornerButtonsHas(::HistoryView::CornerButtonType type) override;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	const not_null<History*> _history;
	Session *_session = nullptr;
	QString _pendingProfileId;
	std::optional<HandoffDraft> _handoff;
	QString _draftKey;
	QString _newDraftKey;
	bool _applyingDraft = false;
	Fn<QString()> _hint;
	Fn<std::vector<PeerKey>()> _mentions;
	std::shared_ptr<::Ui::ChatTheme> _theme;
	std::unique_ptr<ChatItems> _items;
	LiveTail _tail;
	std::vector<ChatMessage> _messages;
	base::flat_set<FullMsgId> _requestedPassed;
	object_ptr<QueueStrip> _queueStrip;
	object_ptr<::Ui::ElasticScroll> _scroll;
	QPointer<::HistoryView::ListWidget> _inner;
	std::unique_ptr<::HistoryView::ComposeControls> _composeControls;
	std::unique_ptr<::HistoryView::CornerButtons> _cornerButtons;
	object_ptr<::Ui::RpWidget> _noProvider;
	object_ptr<::Ui::FlatLabel> _noProviderLabel;
	object_ptr<::Ui::RoundButton> _openSettings;
	base::unique_qptr<::Ui::PopupMenu> _modelMenu;
	base::Timer _rebuildTimer;
	rpl::variable<QString> _modelTitle;
	rpl::variable<bool> _working = false;
	rpl::variable<int> _naturalHeight = 0;
	rpl::event_stream<> _slices;
	rpl::event_stream<Session*> _sessionChanges;
	bool _composerOnTop = false;
	int _bottomRadius = 0;
	bool _noProviderMode = false;
	bool _skipScrollEvent = false;
	rpl::lifetime _sessionLifetime;

};

} // namespace Agent::Ui
