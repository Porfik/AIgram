/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_chat_messages.h"
#include "base/flat_map.h"

#include <rpl/event_stream.h>

class History;
class HistoryItem;
class PhotoData;
struct HistoryMessageMarkupButton;

namespace Agent::Ui {

enum class ChatButtonAction : uchar {
	Execute,
	Alternate,
	Decline,
	Edit,
	Handoff,
	Undo,
	UndoAll,
	Retry,
	ModelSettings,
	ShowInFolder,
};

struct ChatButton {
	ChatButtonAction action = ChatButtonAction::Execute;
	QString id;
};

struct ChatExtras {
	QString note;
	QString error;
	bool errorSettings = false;
	bool compacting = false;
	QString spending;
	std::vector<QString> queued;
	bool live = false;
};

struct ChatItemsDelegate {
	Fn<bool(const QString &toolUseId)> canUndo;
	Fn<bool(const QString &toolUseId)> isUndone;
	Fn<bool()> isRun;
	Fn<TimeId(int index)> date;
	Fn<std::vector<FullMsgId>(int index)> passed;
	Fn<std::optional<MediaBlock>(const QString &attachment)> attachment;
};

class ChatItems final {
public:
	ChatItems(not_null<History*> history, ChatItemsDelegate delegate);
	~ChatItems();

	// Returns whether the list of shown items changed.
	bool apply(
		const std::vector<ChatMessage> &messages,
		const ChatExtras &extras);
	void clear();

	[[nodiscard]] const std::vector<FullMsgId> &ids() const;
	[[nodiscard]] bool contains(FullMsgId id) const;
	[[nodiscard]] rpl::producer<ChatButton> buttons() const;

	static bool Activate(
		not_null<HistoryItem*> item,
		const HistoryMessageMarkupButton &button);

private:
	struct Prepared;
	struct Entry {
		QString key;
		QString signature;
		FullMsgId id;
		bool service = false;
		bool out = false;
	};

	[[nodiscard]] HistoryItem *create(const Prepared &prepared);
	void update(not_null<HistoryItem*> item, const Prepared &prepared);
	void destroy(const Entry &entry);

	const not_null<History*> _history;
	const ChatItemsDelegate _delegate;
	std::vector<Entry> _entries;
	std::vector<FullMsgId> _ids;
	base::flat_map<QString, std::vector<QString>> _undoAll;
	base::flat_map<QString, PhotoData*> _photos;
	rpl::event_stream<ChatButton> _buttons;

};

} // namespace Agent::Ui
