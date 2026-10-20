/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "agent/ui/agent_composer.h"
#include "agent/ui/agent_feed_widget.h"
#include "agent/ui/agent_queue.h"
#include "data/data_types.h"
#include "ui/rp_widget.h"

#include <optional>
#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

namespace Window {
class SessionController;
} // namespace Window

namespace Agent {
class Manager;
class Session;
struct AttachmentInput;
} // namespace Agent

namespace Ui {
struct PreparedBundle;
struct PreparedFile;
struct PreparedList;
} // namespace Ui

namespace Agent::Ui {

using namespace ::Ui;

class PassedMessagesBar;

struct HandoffDraft {
	QString text;
	QString untrusted;
	PeerKey from = 0;
};

class SessionView final : public RpWidget {
public:
	SessionView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Manager*> manager,
		Scope scope);
	~SessionView();

	void showSession(Session *session);
	[[nodiscard]] Session *session() const;
	[[nodiscard]] rpl::producer<Session*> sessionChanges() const;

	void setHintProvider(Fn<QString()> hint);
	void setInnerFocus();
	[[nodiscard]] QString draft() const;
	void setDraft(const QString &text);
	void setContentMaxWidth(int width);
	void startHandoff(HandoffDraft draft);
	void setComposerOnTop(bool onTop);
	void setMentionsProvider(Fn<std::vector<PeerKey>()> mentions);
	void passMessages(HistoryItemsList items);
	[[nodiscard]] rpl::producer<int> naturalHeightValue() const;
	[[nodiscard]] int naturalHeight() const;

private:
	enum class Mode {
		Normal,
		NoProvider,
	};

	void setupWidgets();
	void refreshProviderState();
	void applyMode(Mode mode);
	void relayout();
	void relayoutOverlay();
	void refreshNaturalHeight();
	void rebuildFeed();
	void applyState();
	void refreshSpending();
	void sendCurrentText(const QString &text);
	void chooseAttach();
	[[nodiscard]] bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel);
	void confirmSendingFiles(
		PreparedList &&list,
		const QString &insertTextOnCancel);
	void sendingFilesConfirmed(std::shared_ptr<PreparedBundle> bundle);
	[[nodiscard]] bool checkAttachmentLimits(
		const std::vector<const PreparedFile *> &files) const;
	void sendWithAttachments(
		const QString &text,
		std::vector<Agent::AttachmentInput> attachments);
	[[nodiscard]] Session *ensureSession();
	void handoffRequested(const QString &text);
	void refreshHandoffNote();
	[[nodiscard]] QString runAboutText() const;
	void sessionGone();
	void resizeEvent(QResizeEvent *e) override;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	const Scope _scope;
	Session *_session = nullptr;
	Mode _mode = Mode::Normal;
	int _contentMaxWidth = 0;
	bool _composerOnTop = false;
	std::optional<HandoffDraft> _handoff;
	Fn<QString()> _hint;
	Fn<std::vector<PeerKey>()> _mentions;
	object_ptr<FeedWidget> _feed;
	object_ptr<Composer> _composer;
	object_ptr<PassedMessagesBar> _passedBar;
	object_ptr<QueueStrip> _queueStrip;
	object_ptr<RpWidget> _welcome;
	object_ptr<Ui::FlatLabel> _welcomeLabel;
	object_ptr<Ui::FlatLabel> _handoffNote;
	object_ptr<RpWidget> _noProvider;
	object_ptr<Ui::FlatLabel> _noProviderLabel;
	object_ptr<Ui::RoundButton> _openSettings;
	rpl::event_stream<Session*> _sessionChanges;
	rpl::variable<int> _naturalHeight = 0;
	rpl::lifetime _sessionLifetime;

};

} // namespace Agent::Ui
