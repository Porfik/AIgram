/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_palette.h"

#include "agent/agent_chat.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_manager.h"
#include "agent/agent_session.h"
#include "agent/ui/agent_chat_view.h"
#include "agent/ui/agent_section.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/layer_widget.h"
#include "ui/round_rect.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_agent_general.h"
#include "styles/style_layers.h"

#include <QtGui/QKeyEvent>

namespace Agent::Ui {
namespace {

class PaletteLayer final : public ::Ui::LayerWidget {
public:
	PaletteLayer(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		HistoryItemsList passed);

	void parentResized() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void doSetInnerFocus() override;

private:
	void updateGeometry();
	void layoutHeader();
	void refreshTitle();
	void openInAgent();
	[[nodiscard]] QString openChatHint() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	PeerData *_openPeer = nullptr;
	object_ptr<::Ui::UserpicButton> _userpic;
	object_ptr<FlatLabel> _title;
	object_ptr<IconButton> _new;
	object_ptr<IconButton> _open;
	object_ptr<IconButton> _close;
	object_ptr<ChatView> _view;
	::Ui::RoundRect _bg;
	bool _updating = false;
	bool _updateAgain = false;
	rpl::lifetime _titleLifetime;

};

PaletteLayer::PaletteLayer(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	HistoryItemsList passed)
: LayerWidget(parent)
, _controller(controller)
, _manager(&controller->session().agent())
, _userpic(
	this,
	not_null<PeerData*>(_manager->chat().user().get()),
	st::agentPaletteUserpic)
, _title(this, QString(), st::agentPaletteTitle)
, _new(this, st::agentPaletteNew)
, _open(this, st::agentPaletteOpen)
, _close(this, st::agentPaletteClose)
, _view(this, controller, &controller->session().agent(), Scope::Account())
, _bg(st::boxRadius, st::boxBg) {
	const auto peer = controller->activeChatCurrent().peer();
	if (peer
		&& !IsAgentPeer(peer)
		&& !_manager->exclusions().excluded(peer->id.value)) {
		_openPeer = peer;
	}
	_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	_view->setComposerOnTop(true);
	_view->setNewSessionDraftKey(u"palette"_q);
	_view->setBottomRadius(st::boxRadius);
	_view->setHintProvider([=] {
		return openChatHint();
	});
	_view->setMentionsProvider([=] {
		return _openPeer
			? std::vector<PeerKey>{ _openPeer->id.value }
			: std::vector<PeerKey>();
	});
	if (!passed.empty()) {
		_view->passMessages(std::move(passed));
	}
	_view->sessionChanges(
	) | rpl::on_next([=] {
		refreshTitle();
	}, lifetime());
	_view->naturalHeightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateGeometry();
	}, lifetime());
	_new->setClickedCallback([=] {
		_view->showSession(nullptr);
		_view->setInnerFocus();
	});
	_open->setClickedCallback([=] {
		openInAgent();
	});
	_close->setClickedCallback([=] {
		closeLayer();
	});
	refreshTitle();
}

QString PaletteLayer::openChatHint() const {
	if (!_openPeer) {
		return QString();
	}
	const auto id = QString::number(_openPeer->id.value);
	return _openPeer->isSelf()
		? (u"The user has Saved Messages (id %1) open; \"this chat\" and"
			" similar words mean it."_q.arg(id))
		: (u"The user has the chat with id %1 open; \"this chat\" and"
			" similar words mean it. Its title is written by others: read"
			" it with get_peer_info when you need it."_q.arg(id));
}

void PaletteLayer::refreshTitle() {
	_titleLifetime.destroy();
	const auto session = _view->session();
	_open->setVisible(session != nullptr);
	if (!session) {
		_title->setText(_manager->chat().name());
		layoutHeader();
		return;
	}
	session->recordChanges(
	) | rpl::on_next_error_done([=] {
		const auto &info = session->record().info;
		_title->setText(info.title.isEmpty()
			? _manager->chat().name()
			: info.title);
		layoutHeader();
	}, [](rpl::no_error) {
	}, [] {
	}, _titleLifetime);
	const auto &info = session->record().info;
	_title->setText(info.title.isEmpty()
		? _manager->chat().name()
		: info.title);
	layoutHeader();
}

void PaletteLayer::openInAgent() {
	const auto session = _view->session();
	if (!session) {
		return;
	}
	const auto id = session->id();
	const auto controller = _controller;
	closeLayer();
	ShowGeneralAgentSession(controller, id);
}

void PaletteLayer::parentResized() {
	updateGeometry();
}

void PaletteLayer::updateGeometry() {
	if (_updating) {
		_updateAgain = true;
		return;
	}
	_updating = true;
	const auto parent = parentWidget();
	do {
		_updateAgain = false;
		const auto outer = parent ? parent->size() : size();
		const auto margin = st::agentPaletteMargin;
		const auto width = std::min(
			st::agentPaletteWidth,
			std::max(outer.width() - 2 * margin, 1));
		const auto top = std::max(
			margin,
			std::min(st::agentPaletteTop, outer.height() / 8));
		const auto header = st::agentPaletteHeaderHeight;
		const auto maxBody = std::max(
			outer.height() - top - margin - header,
			st::agentPaletteMinBody);
		_view->resize(width, std::max(_view->height(), 1));
		const auto bottom = _view->hasMessages() ? 0 : st::boxRadius;
		const auto natural = std::max(_view->naturalHeight(), 1) + bottom;
		const auto body = std::min(natural, maxBody);
		setGeometry((outer.width() - width) / 2, top, width, header + body);
		_view->setGeometry(0, header, width, body - bottom);
		layoutHeader();
	} while (_updateAgain);
	_updating = false;
}

void PaletteLayer::layoutHeader() {
	const auto header = st::agentPaletteHeaderHeight;
	_userpic->move(
		st::agentPaletteUserpicLeft,
		(header - st::agentPaletteUserpicSize) / 2);
	auto right = width();
	_close->moveToRight(0, 0, width());
	right -= _close->width();
	if (!_open->isHidden()) {
		_open->moveToLeft(right - _open->width(), 0, width());
		right -= _open->width();
	}
	_new->moveToLeft(right - _new->width(), 0, width());
	right -= _new->width();
	const auto left = st::agentPaletteTitleLeft;
	_title->resizeToWidth(std::max(right - left, 1));
	_title->moveToLeft(left, (header - _title->height()) / 2, width());
}

void PaletteLayer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_bg.paint(p, rect());
}

void PaletteLayer::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		closeLayer();
	} else {
		LayerWidget::keyPressEvent(e);
	}
}

void PaletteLayer::doSetInnerFocus() {
	_view->setInnerFocus();
}

} // namespace

bool ShowPalette(
		not_null<Window::SessionController*> controller,
		HistoryItemsList passed) {
	if (!controller->session().agent().enabled()) {
		return false;
	}
	controller->showLayer(
		std::make_unique<PaletteLayer>(
			controller->widget()->bodyWidget(),
			controller,
			std::move(passed)),
		::Ui::LayerOption::KeepOther);
	return true;
}

bool CanPassChatToAgent(not_null<PeerData*> peer) {
	auto &manager = peer->session().agent();
	return manager.enabled()
		&& !manager.exclusions().excluded(peer->id.value);
}

bool CanPassToAgent(const HistoryItemsList &items) {
	if (items.empty()) {
		return false;
	}
	const auto history = items.front()->history();
	if (!CanPassChatToAgent(history->peer)) {
		return false;
	}
	for (const auto &item : items) {
		if (item->history() != history
			|| !item->isRegular()
			|| item->isService()) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] HistoryItemsList ResolvePassed(
		not_null<Main::Session*> session,
		const MessageIdsList &ids) {
	auto &owner = session->data();
	auto expanded = MessageIdsList();
	if (ids.size() == 1) {
		if (const auto item = owner.message(ids.front())) {
			expanded = owner.itemOrItsGroup(item);
		}
	} else {
		expanded = ids;
	}
	auto result = HistoryItemsList();
	for (const auto &id : expanded) {
		if (const auto item = owner.message(id)) {
			result.push_back(item);
		}
	}
	ranges::sort(result, ranges::less(), &HistoryItem::id);
	return result;
}

bool PassToAgent(
		not_null<Window::SessionController*> controller,
		const MessageIdsList &ids) {
	auto items = ResolvePassed(&controller->session(), ids);
	if (!CanPassToAgent(items)) {
		return false;
	}
	return ShowPalette(controller, std::move(items));
}

bool TakeAgentTargets(
		std::vector<not_null<Data::Thread*>> &threads,
		not_null<Main::Session*> session,
		const MessageIdsList &ids) {
	const auto was = threads.size();
	threads.erase(ranges::remove_if(threads, [](not_null<Data::Thread*> t) {
		return IsAgentPeer(t->peer());
	}), end(threads));
	if (threads.size() == was) {
		return false;
	} else if (const auto window = session->tryResolveWindow()) {
		PassToAgent(window, ids);
	}
	return true;
}

void AddPassToAgentAction(
		not_null<::Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		const MessageIdsList &ids,
		bool selected,
		Fn<void()> passed) {
	if (!CanPassToAgent(ResolvePassed(&controller->session(), ids))) {
		return;
	}
	const auto weak = base::make_weak(controller);
	menu->addAction(
		(selected
			? tr::lng_agent_pass_selected(tr::now)
			: tr::lng_agent_pass_to_agent(tr::now)),
		[=] {
			if (const auto strong = weak.get()) {
				if (PassToAgent(strong, ids) && passed) {
					passed();
				}
			}
		},
		&st::agentMenuIconPass);
}

} // namespace Agent::Ui
