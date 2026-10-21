/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_drafts.h"

#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/text/text_entity.h"

#include <QtCore/QDataStream>

namespace Agent {
namespace {

constexpr auto kVersion = qint32(1);
constexpr auto kSaveDelay = crl::time(1000);
constexpr auto kMaxDrafts = 200;

} // namespace

Drafts::Drafts(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(); }) {
	load();
}

Drafts::~Drafts() {
	if (_saveTimer.isActive()) {
		save();
	}
}

ViewDraft Drafts::get(const QString &key) const {
	const auto i = _list.find(key);
	return (i != end(_list)) ? i->second : ViewDraft();
}

void Drafts::set(const QString &key, ViewDraft draft) {
	if (key.isEmpty()) {
		return;
	} else if (draft.text.text.isEmpty() && !draft.reply) {
		remove(key);
		return;
	}
	const auto i = _list.find(key);
	const auto textChanged = (i == end(_list))
		|| (i->second.text != draft.text)
		|| (i->second.cursor != draft.cursor);
	_list[key] = std::move(draft);
	if (textChanged) {
		scheduleSave();
	}
}

void Drafts::remove(const QString &key) {
	if (_list.remove(key)) {
		scheduleSave();
	}
}

void Drafts::clear() {
	if (!_list.empty()) {
		_list.clear();
		scheduleSave();
	}
}

void Drafts::load() {
	const auto bytes = _session->local().readAgentValue(u"drafts"_q);
	if (!bytes) {
		return;
	}
	auto stream = QDataStream(*bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto count = qint32();
	stream >> version >> count;
	if (stream.status() != QDataStream::Ok
		|| version != kVersion
		|| count < 0
		|| count > kMaxDrafts) {
		LOG(("Agent Error: could not parse the agent drafts."));
		return;
	}
	for (auto i = 0; i != count; ++i) {
		auto key = QString();
		auto text = QString();
		auto tags = QByteArray();
		auto position = qint32();
		auto anchor = qint32();
		auto scroll = qint32();
		stream >> key >> text >> tags >> position >> anchor >> scroll;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Agent Error: could not parse the agent drafts."));
			_list.clear();
			return;
		}
		_list.emplace(key, ViewDraft{
			.text = {
				text,
				TextUtilities::DeserializeTags(tags, text.size()),
			},
			.cursor = MessageCursor(position, anchor, scroll),
		});
	}
}

void Drafts::scheduleSave() {
	_saveTimer.callOnce(kSaveDelay);
}

void Drafts::save() {
	_saveTimer.cancel();
	auto saved = std::vector<std::pair<QString, const ViewDraft*>>();
	for (const auto &[key, draft] : _list) {
		if (!draft.text.text.isEmpty()) {
			saved.emplace_back(key, &draft);
		}
	}
	if (saved.empty()) {
		_session->local().removeAgentValue(u"drafts"_q);
		return;
	}
	if (saved.size() > kMaxDrafts) {
		saved.resize(kMaxDrafts);
	}
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kVersion << qint32(saved.size());
	for (const auto &[key, draft] : saved) {
		stream
			<< key
			<< draft->text.text
			<< TextUtilities::SerializeTags(draft->text.tags)
			<< qint32(draft->cursor.position)
			<< qint32(draft->cursor.anchor)
			<< qint32(draft->cursor.scroll);
	}
	_session->local().writeAgentValue(u"drafts"_q, bytes);
}

} // namespace Agent
