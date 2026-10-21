/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/timer.h"
#include "data/data_types.h"
#include "data/data_msg_id.h"

namespace Main {
class Session;
} // namespace Main

namespace Agent {

struct ViewDraft {
	TextWithTags text;
	MessageCursor cursor;
	FullReplyTo reply;
};

class Drafts final {
public:
	explicit Drafts(not_null<Main::Session*> session);
	~Drafts();

	[[nodiscard]] ViewDraft get(const QString &key) const;
	void set(const QString &key, ViewDraft draft);
	void remove(const QString &key);
	void clear();

private:
	void load();
	void scheduleSave();
	void save();

	const not_null<Main::Session*> _session;
	base::flat_map<QString, ViewDraft> _list;
	base::Timer _saveTimer;

};

} // namespace Agent
