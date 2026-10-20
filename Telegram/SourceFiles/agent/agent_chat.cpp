/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_chat.h"

#include "agent/agent_manager.h"
#include "agent/agent_settings.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_send_action.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_pinned_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/image/image_location.h"
#include "ui/painter.h"

#include <QtCore/QBuffer>
#include <QtSvg/QSvgRenderer>

namespace Agent {
namespace {

constexpr auto kPhotoSize = 320;
constexpr auto kPreviewLimit = 256;
constexpr auto kTypingInterval = crl::time(5000);
constexpr auto kAgentUserBase = (0xFEULL << 32) + (1ULL << 31) + 0xA6E7ULL;

[[nodiscard]] not_null<UserData*> CreateUser(not_null<Main::Session*> session) {
	const auto result = session->data().user(peerToUser(AgentPeerId()));
	result->setName(AgentName(QString()), QString(), QString(), QString());
	result->setBotInfoVersion(1);
	if (const auto info = result->botInfo.get()) {
		info->inited = true;
		info->cantJoinGroups = true;
	}
	result->setLoadedStatus(PeerData::LoadedStatus::Normal);
	return result;
}

[[nodiscard]] QByteArray PhotoBytes(const QImage &image) {
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	image.save(&buffer, "JPG", 90);
	return result;
}

} // namespace

PeerId AgentPeerId() {
	return peerFromUser(UserId(kAgentUserBase));
}

bool IsAgentPeer(PeerId id) {
	return (id == AgentPeerId());
}

bool IsAgentPeer(const PeerData *peer) {
	return peer && IsAgentPeer(peer->id);
}

bool IsAgentHistory(const History *history) {
	return history && IsAgentPeer(history->peer->id);
}

int CountPinnedForLimit(const std::vector<Dialogs::Key> &order) {
	return int(ranges::count_if(order, [](const Dialogs::Key &key) {
		return !IsAgentHistory(key.history());
	}));
}

QImage DefaultAgentPhoto(int size) {
	auto result = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
	auto p = QPainter(&result);
	auto bg = QLinearGradient(0, 0, 0, size);
	bg.setStops({
		{ 0., st::historyPeer5UserpicBg->c },
		{ 1., st::historyPeer5UserpicBg2->c },
	});
	p.fillRect(0, 0, size, size, bg);
	auto svg = QSvgRenderer(u":/gui/icons/menu/ai_prompt.svg"_q);
	auto icon = QImage(
		QSize(size, size) / 2,
		QImage::Format_ARGB32_Premultiplied);
	icon.fill(Qt::transparent);
	{
		auto q = QPainter(&icon);
		PainterHighQualityEnabler hq(q);
		svg.render(&q, QRectF(QPointF(), icon.size()));
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.fillRect(icon.rect(), st::historyPeerUserpicFg->c);
	}
	p.drawImage((size - icon.width()) / 2, (size - icon.height()) / 2, icon);
	return result;
}

QString AgentName(const QString &custom) {
	const auto trimmed = custom.trimmed();
	return trimmed.isEmpty() ? tr::lng_agent_row_title(tr::now) : trimmed;
}

AgentChat::AgentChat(not_null<Manager*> manager)
: _manager(manager)
, _session(&manager->session())
, _user(CreateUser(_session))
, _history(_session->data().history(_user))
, _typingTimer([=] { sendTyping(); }) {
	if (!_history->folderKnown()) {
		_history->clearFolder();
	}
	loadState();
	refreshProfile();

	Agent::ProvidersChanged(
	) | rpl::on_next([=] {
		refreshProfile();
		refreshPreview();
	}, _lifetime);

	_manager->enabledValue(
	) | rpl::on_next([=] {
		refreshShown();
	}, _lifetime);

	_manager->listChanges(
	) | rpl::filter([](const Scope &scope) {
		return (scope == Scope::Account());
	}) | rpl::on_next([=] {
		refreshPreview();
	}, _lifetime);

	_manager->activityValue(
		Scope::Account()
	) | rpl::on_next([=](ScopeActivity activity) {
		refreshActivity(activity);
	}, _lifetime);

	_session->data().pinnedDialogsOrderUpdated(
	) | rpl::on_next([=] {
		pinnedOrderChanged();
	}, _lifetime);
}

AgentChat::~AgentChat() {
	_lifetime.destroy();
	removePreview();
}

not_null<UserData*> AgentChat::user() const {
	return _user;
}

not_null<History*> AgentChat::history() const {
	return _history;
}

bool AgentChat::shown() const {
	return _shown.current();
}

rpl::producer<bool> AgentChat::shownValue() const {
	return _shown.value();
}

bool AgentChat::pinned() const {
	return _state.pinned;
}

void AgentChat::setPinned(bool pinned) {
	if (_state.pinned == pinned) {
		return;
	}
	_state.pinned = pinned;
	_state.pinnedIndex = 0;
	saveState();
	applyPinned();
}

QString AgentChat::name() const {
	return _name.current();
}

rpl::producer<QString> AgentChat::nameValue() const {
	return _name.value();
}

QImage AgentChat::photo() const {
	if (!_photoBytes.isEmpty()) {
		auto result = QImage::fromData(_photoBytes);
		if (!result.isNull()) {
			return result;
		}
	}
	return DefaultAgentPhoto(kPhotoSize);
}

void AgentChat::refreshProfile() {
	const auto &providers = _manager->providers();
	const auto name = AgentName(providers.agentName);
	if (_name.current() != name) {
		_name = name;
		_user->setName(name, QString(), QString(), QString());
	}
	if (_photoKnown && _photoBytes == providers.agentPhoto) {
		return;
	}
	_photoKnown = true;
	_photoBytes = providers.agentPhoto;
	const auto image = photo();
	const auto bytes = PhotoBytes(image);
	_user->setUserpic(
		base::RandomValue<PhotoId>(),
		ImageLocation(
			{ .data = InMemoryLocation{ .bytes = bytes } },
			image.width(),
			image.height()),
		false);
	_session->changes().peerUpdated(_user, Data::PeerUpdate::Flag::Photo);
}

void AgentChat::refreshShown() {
	const auto shown = _manager->enabled();
	if (_shown.current() == shown) {
		return;
	}
	if (shown) {
		_shown = true;
		refreshPreview();
		applyPinned();
	} else {
		_shown = false;
		applyPinned();
		removePreview();
		_history->clear(History::ClearType::DeleteChat);
		_history->updateChatListExistence();
	}
}

void AgentChat::refreshPreview() {
	if (!_shown.current()) {
		return;
	}
	const auto list = _manager->list(Scope::Account());
	const auto latest = list.empty() ? nullptr : &list.front();
	const auto mode = _manager->providers().rowPreview;
	auto text = QString();
	auto outgoing = false;
	if (latest && mode == RowPreview::LastMessage) {
		text = latest->preview;
		outgoing = latest->previewOutgoing;
	}
	if (latest && text.isEmpty() && mode != RowPreview::Hint) {
		text = latest->title;
		outgoing = false;
	}
	if (text.isEmpty()) {
		text = tr::lng_agent_row_about(tr::now);
		outgoing = false;
	}
	text = text.left(kPreviewLimit);
	if (!_state.since) {
		_state.since = base::unixtime::now();
		saveState();
	}
	const auto date = latest
		? TimeId(latest->updated)
		: TimeId(_state.since);
	const auto key = text
		+ QChar(outgoing ? '>' : '<')
		+ QString::number(date);
	if (key == _previewKey && _session->data().message(_previewId)) {
		return;
	}
	_previewKey = key;
	const auto was = _previewId;
	const auto flags = outgoing
		? (MessageFlag::HasFromId | MessageFlag::Outgoing)
		: MessageFlags(MessageFlag::HasFromId);
	const auto item = _history->addNewLocalMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = flags,
		.from = outgoing ? _session->userPeerId() : _user->id,
		.date = date,
	}, TextWithEntities{ text }, MTP_messageMediaEmpty());
	_previewId = item->fullId();
	if (const auto old = _session->data().message(was)) {
		old->destroy();
	}
}

void AgentChat::removePreview() {
	if (const auto item = _session->data().message(_previewId)) {
		item->destroy();
	}
	_previewId = FullMsgId();
	_previewKey = QString();
}

void AgentChat::refreshActivity(const ScopeActivity &activity) {
	if (_history->folderKnown()) {
		_history->setUnreadCount(activity.pendingCards);
	}
	if (_working == activity.working) {
		return;
	}
	_working = activity.working;
	if (_working) {
		sendTyping();
		_typingTimer.callEach(kTypingInterval);
	} else {
		_typingTimer.cancel();
		_session->data().sendActionManager().registerFor(
			_history,
			MsgId(),
			_user,
			MTP_sendMessageCancelAction(),
			base::unixtime::now());
	}
}

void AgentChat::sendTyping() {
	_session->data().sendActionManager().registerFor(
		_history,
		MsgId(),
		_user,
		MTP_sendMessageTypingAction(),
		base::unixtime::now());
}

void AgentChat::applyPinned() {
	const auto list = _session->data().chatsList()->pinned();
	const auto key = Dialogs::Key(_history);
	_applyingPin = true;
	if (_state.pinned && _shown.current()) {
		list->setPinned(key, true);
		const auto target = std::min(
			_state.pinnedIndex,
			int(list->order().size()) - 1);
		for (auto i = 0; i < target; ++i) {
			list->reorder(key, list->order()[i + 1]);
		}
	} else {
		list->setPinned(key, false);
	}
	_history->updateChatListExistence();
	_session->data().notifyPinnedDialogsOrderUpdated();
	_applyingPin = false;
}

void AgentChat::pinnedOrderChanged() {
	if (_applyingPin || !_shown.current()) {
		return;
	}
	const auto &order = _session->data().chatsList()->pinned()->order();
	const auto i = ranges::find(order, Dialogs::Key(_history));
	if (i == end(order)) {
		if (_state.pinned) {
			applyPinned();
		}
		return;
	}
	const auto index = int(i - begin(order));
	if (!_state.pinned || _state.pinnedIndex != index) {
		_state.pinned = true;
		_state.pinnedIndex = index;
		saveState();
	}
}

void AgentChat::loadState() {
	const auto bytes = _session->local().readAgentValue(u"chat"_q);
	if (!bytes) {
		return;
	}
	if (const auto state = DeserializeChatState(*bytes)) {
		_state = *state;
	} else {
		LOG(("Agent Error: could not parse the agent chat state."));
	}
}

void AgentChat::saveState() {
	_session->local().writeAgentValue(
		u"chat"_q,
		SerializeChatState(_state));
}

} // namespace Agent
