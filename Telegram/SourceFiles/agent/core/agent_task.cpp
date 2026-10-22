/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_task.h"

#include "base/random.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>

#include <algorithm>

namespace Agent {
namespace {

[[nodiscard]] bool ContainsPeer(
		const std::vector<PeerKey> &peers,
		PeerKey peer) {
	return std::find(peers.begin(), peers.end(), peer) != peers.end();
}

[[nodiscard]] bool ContainsFolder(
		const std::vector<int> &folders,
		int folder) {
	return std::find(folders.begin(), folders.end(), folder)
		!= folders.end();
}

} // namespace

bool ChatSet::empty() const {
	return peers.empty() && !kinds && folders.empty() && !triggerChat;
}

bool ChatSetContains(
		const ChatSet &set,
		const ChatFacts &chat,
		PeerKey triggerPeer) {
	if (!chat.peer) {
		return false;
	} else if (ContainsPeer(set.peers, chat.peer)) {
		return true;
	} else if (!chat.self && (set.kinds & chat.kind)) {
		return true;
	}
	for (const auto folder : chat.folders) {
		if (ContainsFolder(set.folders, folder)) {
			return true;
		}
	}
	return set.triggerChat
		&& (triggerPeer != 0)
		&& (chat.peer == triggerPeer);
}

bool TimeWindow::always() const {
	return (from == till) && !days;
}

bool InTimeWindow(const TimeWindow &window, const QDateTime &local) {
	const auto minutes = local.time().hour() * 60 + local.time().minute();
	const auto overMidnight = (window.from > window.till);
	if (window.days != 0) {
		auto date = local.date();
		if (overMidnight && minutes < window.till) {
			date = date.addDays(-1);
		}
		const auto bit = uchar(1) << (date.dayOfWeek() - 1);
		if (!(window.days & bit)) {
			return false;
		}
	}
	if (window.from == window.till) {
		return true;
	} else if (window.from <= window.till) {
		return minutes >= window.from && minutes < window.till;
	}
	return minutes >= window.from || minutes < window.till;
}

bool MatchesMessage(
		const MessageFilter &filter,
		const MessageFacts &message) {
	if (message.fromAgent) {
		return false;
	} else if (message.outgoing
		&& (!message.chat.self
			|| !ContainsPeer(filter.chats.peers, message.chat.peer))) {
		return false;
	} else if (!ChatSetContains(filter.chats, message.chat)) {
		return false;
	} else if (!filter.senders.empty()
		&& !ContainsPeer(filter.senders, message.sender)) {
		return false;
	}
	const auto hasKeywords = !filter.keywords.isEmpty();
	const auto hasRegex = !filter.regex.isEmpty();
	if (hasKeywords || hasRegex) {
		auto matched = false;
		for (const auto &keyword : filter.keywords) {
			if (!keyword.isEmpty()
				&& message.text.contains(keyword, Qt::CaseInsensitive)) {
				matched = true;
				break;
			}
		}
		if (!matched && hasRegex) {
			auto regex = QRegularExpression(
				filter.regex,
				QRegularExpression::CaseInsensitiveOption);
			if (regex.isValid()) {
				matched = regex.match(message.text).hasMatch();
			}
		}
		if (!matched) {
			return false;
		}
	}
	if (filter.media != 0 && !(message.media & filter.media)) {
		return false;
	} else if (filter.mentionsOnly && !message.mentionsMe) {
		return false;
	}
	return InTimeWindow(filter.window, message.local);
}

qint64 NextFireTime(
		const Schedule &schedule,
		qint64 anchor,
		qint64 after,
		const QTimeZone &zone) {
	if (schedule.kind == ScheduleKind::Interval) {
		const auto minutes = std::max(
			schedule.everyMinutes,
			int(kTaskMinIntervalMinutes));
		const auto step = qint64(minutes) * 60;
		if (anchor > after) {
			return anchor;
		}
		const auto passed = (after - anchor) / step;
		return anchor + (passed + 1) * step;
	} else if (schedule.kind == ScheduleKind::Times) {
		if (schedule.times.empty()) {
			return 0;
		}
		const auto local = QDateTime::fromSecsSinceEpoch(after, zone);
		auto best = qint64(0);
		for (auto dayOffset = 0; dayOffset != 9; ++dayOffset) {
			const auto date = local.date().addDays(dayOffset);
			if (schedule.days != 0) {
				const auto bit = uchar(1) << (date.dayOfWeek() - 1);
				if (!(schedule.days & bit)) {
					continue;
				}
			}
			for (const auto time : schedule.times) {
				if (time < 0 || time > 1439) {
					continue;
				}
				const auto moment = QDateTime(
					date,
					QTime(time / 60, time % 60),
					zone);
				const auto epoch = moment.toSecsSinceEpoch();
				if (epoch > after && (!best || epoch < best)) {
					best = epoch;
				}
			}
		}
		return best;
	}
	return (schedule.at > after) ? schedule.at : 0;
}

namespace {

constexpr auto kTaskVersion = quint32(2);
constexpr auto kMaxInnerList = 1'000;
constexpr auto kMaxSerializedTasks = int(kTaskMaxTasks);
constexpr auto kMaxSerializedRuns = int(kTaskMaxRuns) * 2;

void WritePeers(QDataStream &stream, const std::vector<PeerKey> &peers) {
	stream << qint32(peers.size());
	for (const auto peer : peers) {
		stream << quint64(peer);
	}
}

void WriteInts(QDataStream &stream, const std::vector<int> &values) {
	stream << qint32(values.size());
	for (const auto value : values) {
		stream << qint32(value);
	}
}

void WriteStrings(QDataStream &stream, const std::vector<QString> &values) {
	stream << qint32(values.size());
	for (const auto &value : values) {
		stream << value;
	}
}

void WriteChatSet(QDataStream &stream, const ChatSet &set) {
	WritePeers(stream, set.peers);
	stream << uchar(set.kinds);
	WriteInts(stream, set.folders);
	stream << qint32(set.triggerChat ? 1 : 0);
}

void WriteFilter(QDataStream &stream, const MessageFilter &filter) {
	WriteChatSet(stream, filter.chats);
	WritePeers(stream, filter.senders);
	WriteStrings(stream, { filter.keywords.begin(), filter.keywords.end() });
	stream << filter.regex << qint32(filter.media);
	stream << qint32(filter.mentionsOnly ? 1 : 0);
	stream << qint32(filter.window.from) << qint32(filter.window.till)
		<< uchar(filter.window.days);
}

void WriteSchedule(QDataStream &stream, const Schedule &schedule) {
	stream << qint32(schedule.kind) << qint32(schedule.everyMinutes);
	WriteInts(stream, schedule.times);
	stream << uchar(schedule.days) << qint64(schedule.at);
}

void WriteTotals(QDataStream &stream, const SpendingTotals &totals) {
	stream
		<< qint64(totals.inputTokens)
		<< qint64(totals.outputTokens)
		<< qint64(totals.cachedInputTokens)
		<< totals.cost
		<< qint32(totals.hasCost ? 1 : 0)
		<< qint32(totals.requests);
}

[[nodiscard]] bool ReadBool(QDataStream &stream, bool &value) {
	auto raw = qint32(0);
	stream >> raw;
	if (stream.status() != QDataStream::Ok || (raw != 0 && raw != 1)) {
		return false;
	}
	value = (raw != 0);
	return true;
}

[[nodiscard]] std::optional<qint32> ReadCount(
		QDataStream &stream,
		qint32 max) {
	auto count = qint32(0);
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count < 0
		|| count > max) {
		return std::nullopt;
	}
	return count;
}

[[nodiscard]] std::optional<std::vector<PeerKey>> ReadPeers(
		QDataStream &stream) {
	const auto count = ReadCount(stream, kMaxInnerList);
	if (!count) {
		return std::nullopt;
	}
	auto peers = std::vector<PeerKey>();
	peers.reserve(*count);
	for (auto i = 0; i != *count; ++i) {
		auto peer = quint64(0);
		stream >> peer;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		peers.push_back(peer);
	}
	return peers;
}

[[nodiscard]] std::optional<std::vector<int>> ReadInts(QDataStream &stream) {
	const auto count = ReadCount(stream, kMaxInnerList);
	if (!count) {
		return std::nullopt;
	}
	auto values = std::vector<int>();
	values.reserve(*count);
	for (auto i = 0; i != *count; ++i) {
		auto value = qint32(0);
		stream >> value;
		if (stream.status() != QDataStream::Ok || value < 0) {
			return std::nullopt;
		}
		values.push_back(value);
	}
	return values;
}

[[nodiscard]] std::optional<std::vector<QString>> ReadStrings(
		QDataStream &stream) {
	const auto count = ReadCount(stream, kMaxInnerList);
	if (!count) {
		return std::nullopt;
	}
	auto values = std::vector<QString>();
	values.reserve(*count);
	for (auto i = 0; i != *count; ++i) {
		auto value = QString();
		stream >> value;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		values.push_back(std::move(value));
	}
	return values;
}

std::optional<ChatSet> ReadChatSet(QDataStream &stream) {
	auto set = ChatSet();
	const auto peers = ReadPeers(stream);
	if (!peers) {
		return std::nullopt;
	}
	set.peers = std::move(*peers);
	stream >> set.kinds;
	const auto folders = ReadInts(stream);
	if (!folders
		|| !ReadBool(stream, set.triggerChat)) {
		return std::nullopt;
	}
	set.folders = std::move(*folders);
	return set;
}

std::optional<MessageFilter> ReadFilter(QDataStream &stream) {
	auto filter = MessageFilter();
	const auto chats = ReadChatSet(stream);
	if (!chats) {
		return std::nullopt;
	}
	filter.chats = std::move(*chats);
	const auto senders = ReadPeers(stream);
	if (!senders) {
		return std::nullopt;
	}
	filter.senders = std::move(*senders);
	const auto keywords = ReadStrings(stream);
	if (!keywords) {
		return std::nullopt;
	}
	filter.keywords = QStringList(
		keywords->begin(),
		keywords->end());
	auto media = qint32(0);
	auto from = qint32(0);
	auto till = qint32(0);
	stream >> filter.regex >> media;
	if (stream.status() != QDataStream::Ok
		|| media < 0
		|| media > 0xFFFF
		|| !ReadBool(stream, filter.mentionsOnly)) {
		return std::nullopt;
	}
	filter.media = ushort(media);
	stream >> from >> till >> filter.window.days;
	if (stream.status() != QDataStream::Ok
		|| from < 0
		|| from > 1439
		|| till < 0
		|| till > 1439) {
		return std::nullopt;
	}
	filter.window.from = from;
	filter.window.till = till;
	return filter;
}

std::optional<Schedule> ReadSchedule(QDataStream &stream) {
	auto schedule = Schedule();
	auto kind = qint32(0);
	stream >> kind >> schedule.everyMinutes;
	if (stream.status() != QDataStream::Ok
		|| kind < 0
		|| kind > 2) {
		return std::nullopt;
	}
	schedule.kind = ScheduleKind(kind);
	const auto times = ReadInts(stream);
	if (!times) {
		return std::nullopt;
	}
	schedule.times = std::move(*times);
	stream >> schedule.days >> schedule.at;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return schedule;
}

[[nodiscard]] bool ReadTotals(QDataStream &stream, SpendingTotals &totals) {
	auto input = qint64(0);
	auto output = qint64(0);
	auto cached = qint64(0);
	auto hasCost = qint32(0);
	auto requests = qint32(0);
	stream >> input >> output >> cached >> totals.cost >> hasCost
		>> requests;
	if (stream.status() != QDataStream::Ok
		|| input < 0
		|| output < 0
		|| cached < 0
		|| (hasCost != 0 && hasCost != 1)
		|| requests < 0) {
		return false;
	}
	totals.inputTokens = input;
	totals.outputTokens = output;
	totals.cachedInputTokens = cached;
	totals.hasCost = (hasCost != 0);
	totals.requests = requests;
	return true;
}

} // namespace

QString GenerateTaskId() {
	return QString::number(base::RandomValue<uint64>(), 16)
		.rightJustified(16, u'0');
}

namespace {

void WriteTask(QDataStream &stream, const Task &task) {
	stream << task.id;
	stream << qint32(task.scope.kind == ScopeKind::Chat ? 1 : 0)
		<< quint64(task.scope.peer);
	stream << task.title << task.instruction;
	stream << qint32(task.trigger.kind);
	WriteFilter(stream, task.trigger.filter);
	WriteSchedule(stream, task.trigger.schedule);
	WriteStrings(stream, task.frame.tools);
	WriteChatSet(stream, task.frame.chats);
	stream << task.providerId;
	stream << task.limits.dailyCost
		<< qint64(task.limits.dailyTokens)
		<< qint32(task.limits.actionsPerHour);
	stream << qint32(int(task.notify))
		<< qint32(task.catchUp ? 1 : 0)
		<< qint32(int(task.state));
	stream << qint64(task.pausedUntil)
		<< qint64(task.created)
		<< qint64(task.lastFired)
		<< qint64(task.lastSeen)
		<< qint32(task.spendingDay);
	WriteTotals(stream, task.spentToday);
	stream << qint64(task.spentToday.cacheWriteTokens);
}

std::optional<Task> ReadTask(QDataStream &stream, quint32 version) {
	auto task = Task();
	auto scopeKind = qint32(0);
	stream >> task.id >> scopeKind >> task.scope.peer;
	if (stream.status() != QDataStream::Ok
		|| scopeKind < 0
		|| scopeKind > 1) {
		return std::nullopt;
	}
	task.scope.kind = scopeKind
		? ScopeKind::Chat
		: ScopeKind::Account;
	stream >> task.title >> task.instruction;
	auto triggerKind = qint32(0);
	stream >> triggerKind;
	if (stream.status() != QDataStream::Ok
		|| triggerKind < 0
		|| triggerKind > 1) {
		return std::nullopt;
	}
	task.trigger.kind = TriggerKind(triggerKind);
	const auto filter = ReadFilter(stream);
	if (!filter) {
		return std::nullopt;
	}
	task.trigger.filter = std::move(*filter);
	const auto schedule = ReadSchedule(stream);
	if (!schedule) {
		return std::nullopt;
	}
	task.trigger.schedule = std::move(*schedule);
	const auto tools = ReadStrings(stream);
	if (!tools) {
		return std::nullopt;
	}
	task.frame.tools = *tools;
	const auto chats = ReadChatSet(stream);
	if (!chats) {
		return std::nullopt;
	}
	task.frame.chats = std::move(*chats);
	auto dailyTokens = qint64(0);
	auto notify = qint32(0);
	auto catchUp = qint32(0);
	auto state = qint32(0);
	stream >> task.providerId >> task.limits.dailyCost >> dailyTokens;
	stream >> task.limits.actionsPerHour;
	stream >> notify >> catchUp >> state;
	if (stream.status() != QDataStream::Ok
		|| dailyTokens < 0
		|| notify < 0
		|| notify > 1
		|| (catchUp != 0 && catchUp != 1)
		|| state < 0
		|| state > 4) {
		return std::nullopt;
	}
	task.limits.dailyTokens = dailyTokens;
	task.notify = TaskNotify(notify);
	task.catchUp = (catchUp != 0);
	task.state = TaskState(state);
	stream >> task.pausedUntil >> task.created >> task.lastFired;
	stream >> task.lastSeen >> task.spendingDay;
	if (!ReadTotals(stream, task.spentToday)) {
		return std::nullopt;
	}
	if (version >= 2) {
		auto cacheWrites = qint64(0);
		stream >> cacheWrites;
		if (stream.status() != QDataStream::Ok || cacheWrites < 0) {
			return std::nullopt;
		}
		task.spentToday.cacheWriteTokens = cacheWrites;
	}
	return task;
}

[[nodiscard]] QJsonArray ChatTypeNames(uchar kinds) {
	auto names = QJsonArray();
	const auto append = [&](uchar flag, const QString &name) {
		if (kinds & flag) {
			names.append(name);
		}
	};
	append(kChatKindPrivate, u"private"_q);
	append(kChatKindGroup, u"group"_q);
	append(kChatKindChannel, u"channel"_q);
	append(kChatKindBot, u"bot"_q);
	return names;
}

[[nodiscard]] QJsonArray MediaNames(ushort media) {
	auto names = QJsonArray();
	const auto append = [&](ushort flag, const QString &name) {
		if (media & flag) {
			names.append(name);
		}
	};
	append(kTaskMediaText, u"text"_q);
	append(kTaskMediaPhoto, u"photo"_q);
	append(kTaskMediaVideo, u"video"_q);
	append(kTaskMediaVoice, u"voice"_q);
	append(kTaskMediaRound, u"round_video"_q);
	append(kTaskMediaAudio, u"audio"_q);
	append(kTaskMediaDocument, u"document"_q);
	append(kTaskMediaSticker, u"sticker"_q);
	append(kTaskMediaGif, u"gif"_q);
	return names;
}

[[nodiscard]] QJsonArray DayNames(uchar days) {
	auto names = QJsonArray();
	const auto append = [&](int index, const QString &name) {
		if (days & (uchar(1) << index)) {
			names.append(name);
		}
	};
	append(0, u"mon"_q);
	append(1, u"tue"_q);
	append(2, u"wed"_q);
	append(3, u"thu"_q);
	append(4, u"_qfri"_q);
	append(5, u"sat"_q);
	append(6, u"sun"_q);
	return names;
}

[[nodiscard]] uchar ParseChatTypeName(const QString &name) {
	if (name == u"private"_q) {
		return kChatKindPrivate;
	} else if (name == u"group"_q) {
		return kChatKindGroup;
	} else if (name == u"channel"_q) {
		return kChatKindChannel;
	} else if (name == u"bot"_q) {
		return kChatKindBot;
	}
	return 0;
}

[[nodiscard]] ushort ParseMediaName(const QString &name) {
	if (name == u"text"_q) {
		return kTaskMediaText;
	} else if (name == u"photo"_q) {
		return kTaskMediaPhoto;
	} else if (name == u"video"_q) {
		return kTaskMediaVideo;
	} else if (name == u"voice"_q) {
		return kTaskMediaVoice;
	} else if (name == u"round_video"_q) {
		return kTaskMediaRound;
	} else if (name == u"audio"_q) {
		return kTaskMediaAudio;
	} else if (name == u"document"_q) {
		return kTaskMediaDocument;
	} else if (name == u"sticker"_q) {
		return kTaskMediaSticker;
	} else if (name == u"gif"_q) {
		return kTaskMediaGif;
	}
	return 0;
}

[[nodiscard]] int ParseDayName(const QString &name) {
	const auto names = { u"mon"_q, u"tue"_q, u"wed"_q,
		u"thu"_q, u"_qfri"_q, u"sat"_q, u"sun"_q };
	auto index = 0;
	for (const auto &candidate : names) {
		if (name == candidate) {
			return index;
		}
		++index;
	}
	return -1;
}

[[nodiscard]] std::optional<int> ParseTimeOfDay(const QString &text) {
	const auto parts = text.split(u':');
	if (parts.size() != 2) {
		return std::nullopt;
	}
	auto hours = 0;
	auto minutes = 0;
	auto okHours = false;
	auto okMinutes = false;
	hours = parts[0].toInt(&okHours);
	minutes = parts[1].toInt(&okMinutes);
	if (!okHours
		|| !okMinutes
		|| hours < 0
		|| hours > 23
		|| minutes < 0
		|| minutes > 59) {
		return std::nullopt;
	}
	return hours * 60 + minutes;
}

[[nodiscard]] QString FormatTimeOfDay(int minutes) {
	return u"%1:%2"_q.arg(minutes / 60, 2, 10, u'0')
		.arg(minutes % 60, 2, 10, u'0');
}

[[nodiscard]] QString FormatStamp(qint64 when, const QTimeZone &zone) {
	return QDateTime::fromSecsSinceEpoch(when, zone)
		.toString(u"yyyy-MM-dd HH:mm"_q);
}

[[nodiscard]] QString ValueText(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		return QString::number(qint64(value.toDouble()));
	}
	return u"value"_q;
}

QJsonObject SchemaText(const QString &description) {
	return QJsonObject{
		{ u"type"_q, u"string"_q },
		{ u"description"_q, description },
	};
}

QJsonObject SchemaNumber(const QString &description) {
	return QJsonObject{
		{ u"type"_q, u"number"_q },
		{ u"description"_q, description },
	};
}

QJsonObject SchemaBoolean(const QString &description) {
	return QJsonObject{
		{ u"type"_q, u"boolean"_q },
		{ u"description"_q, description },
	};
}

QJsonObject SchemaEnum(QJsonArray values, const QString &description) {
	return QJsonObject{
		{ u"type"_q, u"string"_q },
		{ u"enum"_q, std::move(values) },
		{ u"description"_q, description },
	};
}

QJsonObject SchemaArrayOf(QJsonValue items, const QString &description) {
	return QJsonObject{
		{ u"type"_q, u"array"_q },
		{ u"items"_q, std::move(items) },
		{ u"description"_q, description },
	};
}

QJsonObject SchemaObject(
		QJsonObject properties,
		QJsonArray required,
		const QString &description) {
	auto result = QJsonObject{
		{ u"type"_q, u"object"_q },
		{ u"properties"_q, std::move(properties) },
		{ u"description"_q, description },
	};
	if (!required.isEmpty()) {
		result[u"required"_q] = std::move(required);
	}
	return result;
}

[[nodiscard]] std::optional<qint64> ParseStamp(
		const QString &text,
		const QTimeZone &zone) {
	const auto moment = QDateTime::fromString(text, u"yyyy-MM-dd HH:mm"_q);
	if (!moment.isValid()) {
		return std::nullopt;
	}
	return QDateTime(moment.date(), moment.time(), zone).toSecsSinceEpoch();
}

std::optional<ChatSet> ParseChatSet(
		const QJsonObject &object,
		const QString &field,
		const TaskRefs &refs,
		QString &error) {
	auto set = ChatSet();
	if (object.contains(u"chats"_q)) {
		const auto value = object.value(u"chats"_q);
		if (!value.isArray()) {
			error = field + u".chats must be an array of chat ids."_q;
			return std::nullopt;
		}
		for (const auto &item : value.toArray()) {
			const auto peer = refs.chat(item);
			if (!peer) {
				error = u"Unknown chat: "_q + ValueText(item) + u"."_q;
				return std::nullopt;
			}
			set.peers.push_back(peer);
		}
	}
	if (object.contains(u"chat_types"_q)) {
		const auto value = object.value(u"chat_types"_q);
		if (!value.isArray()) {
			error = field + u".chat_types must be an array of names."_q;
			return std::nullopt;
		}
		for (const auto &item : value.toArray()) {
			const auto kind = ParseChatTypeName(item.toString());
			if (!kind) {
				error = field + u".chat_types has an unknown type: "_q
					+ ValueText(item) + u"."_q;
				return std::nullopt;
			}
			set.kinds |= kind;
		}
	}
	if (object.contains(u"folders"_q)) {
		const auto value = object.value(u"folders"_q);
		if (!value.isArray()) {
			error = field + u".folders must be an array of folder ids."_q;
			return std::nullopt;
		}
		for (const auto &item : value.toArray()) {
			const auto folder = refs.folder(item);
			if (!folder) {
				error = field + u".folders contains an unknown folder."_q;
				return std::nullopt;
			}
			set.folders.push_back(*folder);
		}
	}
	return set;
}

std::optional<std::vector<PeerKey>> ParseSenders(
		const QJsonObject &object,
		const TaskRefs &refs,
		QString &error) {
	const auto value = object.value(u"senders"_q);
	if (!value.isArray()) {
		error = u"trigger.senders must be an array of chat ids."_q;
		return std::nullopt;
	}
	auto senders = std::vector<PeerKey>();
	for (const auto &item : value.toArray()) {
		const auto peer = refs.chat(item);
		if (!peer) {
			error = u"Unknown chat: "_q + ValueText(item) + u"."_q;
			return std::nullopt;
		}
		senders.push_back(peer);
	}
	return senders;
}

std::optional<QStringList> ParseKeywords(
		const QJsonObject &object,
		QString &error) {
	const auto value = object.value(u"keywords"_q);
	if (!value.isArray()) {
		error = u"trigger.keywords must be an array of words."_q;
		return std::nullopt;
	}
	auto keywords = QStringList();
	for (const auto &item : value.toArray()) {
		if (!item.isString()) {
			error = u"trigger.keywords must be an array of words."_q;
			return std::nullopt;
		}
		auto keyword = item.toString().trimmed();
		if (!keyword.isEmpty()) {
			keywords.push_back(std::move(keyword));
		}
	}
	if (keywords.size() > kTaskMaxKeywords) {
		error = u"trigger.keywords must have at most "_q
			+ QString::number(kTaskMaxKeywords) + u" entries."_q;
		return std::nullopt;
	}
	return keywords;
}

std::optional<QString> ParseRegex(
		const QJsonObject &object,
		QString &error) {
	const auto value = object.value(u"regex"_q);
	if (!value.isString()) {
		error = u"trigger.regex must be a string."_q;
		return std::nullopt;
	}
	const auto regex = QRegularExpression(
		value.toString(),
		QRegularExpression::CaseInsensitiveOption);
	if (!regex.isValid()) {
		error = u"trigger.regex is not a valid regular expression: "_q
			+ regex.errorString() + u"."_q;
		return std::nullopt;
	}
	return value.toString();
}

std::optional<ushort> ParseMediaField(
		const QJsonObject &object,
		QString &error) {
	const auto value = object.value(u"media"_q);
	if (!value.isArray()) {
		error = u"trigger.media must be an array of media type names."_q;
		return std::nullopt;
	}
	auto media = ushort(0);
	for (const auto &item : value.toArray()) {
		const auto flag = ParseMediaName(item.toString());
		if (!flag) {
			error = u"trigger.media has an unknown media type: "_q
				+ ValueText(item) + u"."_q;
			return std::nullopt;
		}
		media |= flag;
	}
	return media;
}

std::optional<uchar> ParseDaysField(
		const QJsonObject &object,
		const QString &field,
		QString &error) {
	const auto value = object.value(u"days"_q);
	if (!value.isArray()) {
		error = field + u".days must be an array of weekday names."_q;
		return std::nullopt;
	}
	auto days = uchar(0);
	for (const auto &item : value.toArray()) {
		const auto index = ParseDayName(item.toString());
		if (index < 0) {
			error = field + u".days has an unknown day: "_q
				+ ValueText(item) + u"."_q;
			return std::nullopt;
		}
		days |= uchar(1) << index;
	}
	return days;
}

std::optional<TimeWindow> ParseTimeWindow(
		const QJsonObject &object,
		QString &error) {
	const auto value = object.value(u"time_window"_q);
	if (!value.isObject()) {
		error = u"trigger.time_window must be a JSON object."_q;
		return std::nullopt;
	}
	const auto window = value.toObject();
	auto result = TimeWindow();
	for (const auto key : { u"from"_q, u"till"_q }) {
		if (!window.contains(key)) {
			continue;
		}
		const auto text = window.value(key);
		const auto minutes = text.isString()
			? ParseTimeOfDay(text.toString())
			: std::nullopt;
		if (!minutes) {
			error = u"trigger.time_window."_q + key
				+ u" must be an HH:MM string."_q;
			return std::nullopt;
		}
		(key == u"from"_q ? result.from : result.till) = *minutes;
	}
	if (window.contains(u"days"_q)) {
		const auto days = ParseDaysField(
			window,
			u"trigger.time_window"_q,
			error);
		if (!days) {
			return std::nullopt;
		}
		result.days = *days;
	}
	return result;
}

struct TriggerResult {
	std::optional<TaskTrigger> trigger;
	QString error;
};

TriggerResult ParseTrigger(
		const QJsonObject &object,
		const Scope &scope,
		const TaskRefs &refs) {
	auto result = TriggerResult();
	const auto type = object.value(u"type"_q);
	if (!type.isString()) {
		result.error = u"trigger.type must be a string."_q;
		return result;
	}
	const auto typeName = type.toString();
	const auto chatScope = (scope.kind == ScopeKind::Chat);
	auto trigger = TaskTrigger();
	if (typeName == u"message"_q) {
		trigger.kind = TriggerKind::Message;
		if (chatScope) {
			for (const auto key : { u"chats"_q, u"chat_types"_q, u"folders"_q }) {
				if (object.contains(key)) {
					result.error = u"trigger."_q + key
						+ u" is only available in the account scope."_q;
					return result;
				}
			}
			trigger.filter.chats.peers = { scope.peer };
		} else {
			if (!object.contains(u"chats"_q)
				&& !object.contains(u"chat_types"_q)
				&& !object.contains(u"folders"_q)) {
				result.error = u"A message trigger must set chats, "
					u"chat_types or folders."_q;
				return result;
			}
			auto chats = ParseChatSet(object, u"trigger"_q, refs, result.error);
			if (!chats) {
				return result;
			}
			trigger.filter.chats = std::move(*chats);
		}
		if (object.contains(u"senders"_q)) {
			auto senders = ParseSenders(object, refs, result.error);
			if (!senders) {
				return result;
			}
			trigger.filter.senders = std::move(*senders);
		}
		if (object.contains(u"keywords"_q)) {
			auto keywords = ParseKeywords(object, result.error);
			if (!keywords) {
				return result;
			}
			trigger.filter.keywords = std::move(*keywords);
		}
		if (object.contains(u"regex"_q)) {
			auto regex = ParseRegex(object, result.error);
			if (!regex) {
				return result;
			}
			trigger.filter.regex = *regex;
		}
		if (object.contains(u"media"_q)) {
			auto media = ParseMediaField(object, result.error);
			if (!media) {
				return result;
			}
			trigger.filter.media = *media;
		}
		if (object.contains(u"mentions_only"_q)) {
			const auto value = object.value(u"mentions_only"_q);
			if (!value.isBool()) {
				result.error = u"trigger.mentions_only must be a boolean."_q;
				return result;
			}
			trigger.filter.mentionsOnly = value.toBool();
		}
		if (object.contains(u"time_window"_q)) {
			auto window = ParseTimeWindow(object, result.error);
			if (!window) {
				return result;
			}
			trigger.filter.window = *window;
		}
	} else if (typeName == u"schedule"_q) {
		const auto hasEvery = object.contains(u"every_minutes"_q);
		const auto hasTimes = object.contains(u"times"_q);
		if (hasEvery && hasTimes) {
			result.error = u"A schedule trigger must not set both "
				u"every_minutes and times."_q;
			return result;
		} else if (!hasEvery && !hasTimes) {
			result.error = u"A schedule trigger must set every_minutes or times."_q;
			return result;
		}
		trigger.kind = TriggerKind::Schedule;
		if (hasEvery) {
			const auto value = object.value(u"every_minutes"_q);
			if (!value.isDouble()) {
				result.error = u"trigger.every_minutes must be a number."_q;
				return result;
			}
			const auto minutes = int(value.toDouble());
			if (minutes < kTaskMinIntervalMinutes) {
				result.error = u"trigger.every_minutes must be at least "_q
					+ QString::number(kTaskMinIntervalMinutes) + u"."_q;
				return result;
			}
			trigger.schedule.kind = ScheduleKind::Interval;
			trigger.schedule.everyMinutes = minutes;
		} else {
			const auto value = object.value(u"times"_q);
			if (!value.isArray()) {
				result.error = u"trigger.times must be an array of HH:MM strings."_q;
				return result;
			}
			auto times = std::vector<int>();
			for (const auto &item : value.toArray()) {
				if (!item.isString()) {
					result.error = u"trigger.times must be an array of HH:MM strings."_q;
					return result;
				}
				const auto minutes = ParseTimeOfDay(item.toString());
				if (!minutes) {
					result.error = u"trigger.times entries must be HH:MM strings."_q;
					return result;
				}
				times.push_back(*minutes);
			}
			if (times.size() > kTaskMaxTimes) {
				result.error = u"trigger.times must have at most "_q
					+ QString::number(kTaskMaxTimes) + u" entries."_q;
				return result;
			}
			std::sort(times.begin(), times.end());
			times.erase(
				std::unique(times.begin(), times.end()),
				times.end());
			trigger.schedule.kind = ScheduleKind::Times;
			trigger.schedule.times = std::move(times);
		}
		if (object.contains(u"days"_q)) {
			auto days = ParseDaysField(object, u"trigger"_q, result.error);
			if (!days) {
				return result;
			}
			trigger.schedule.days = *days;
		}
	} else if (typeName == u"once"_q) {
		trigger.kind = TriggerKind::Schedule;
		trigger.schedule.kind = ScheduleKind::Once;
		const auto value = object.value(u"at"_q);
		if (!value.isString()) {
			result.error = u"trigger.at must be a YYYY-MM-DD HH:MM string."_q;
			return result;
		}
		const auto stamp = ParseStamp(value.toString(), refs.zone);
		if (!stamp) {
			result.error = u"trigger.at must be a YYYY-MM-DD HH:MM string."_q;
			return result;
		} else if (*stamp <= refs.now) {
			result.error = u"trigger.at must be in the future."_q;
			return result;
		}
		trigger.schedule.at = *stamp;
	} else {
		result.error = u"trigger.type must be message, schedule or once."_q;
		return result;
	}
	result.trigger = std::move(trigger);
	return result;
}

QString ParseActions(
		const QJsonObject &object,
		const TaskRefs &refs,
		std::vector<QString> &tools) {
	if (!object.contains(u"allowed_actions"_q)) {
		return QString();
	}
	const auto value = object.value(u"allowed_actions"_q);
	if (!value.isArray()) {
		return u"allowed_actions must be an array of tool names."_q;
	}
	for (const auto &item : value.toArray()) {
		if (!item.isString()) {
			return u"allowed_actions must be an array of tool names."_q;
		}
		const auto name = item.toString();
		if (!refs.actionTool(name)) {
			return u"Unknown action in allowed_actions: "_q + name + u"."_q;
		}
		tools.push_back(name);
	}
	return QString();
}

std::optional<ChatSet> ParseAllowedChats(
		const QJsonObject &object,
		const TaskRefs &refs,
		QString &error) {
	const auto value = object.value(u"allowed_chats"_q);
	if (!value.isObject()) {
		error = u"allowed_chats must be a JSON object."_q;
		return std::nullopt;
	}
	const auto allowed = value.toObject();
	auto set = ParseChatSet(allowed, u"allowed_chats"_q, refs, error);
	if (!set) {
		return std::nullopt;
	}
	if (allowed.contains(u"trigger_chat"_q)) {
		const auto flag = allowed.value(u"trigger_chat"_q);
		if (!flag.isBool()) {
			error = u"allowed_chats.trigger_chat must be a boolean."_q;
			return std::nullopt;
		}
		set->triggerChat = flag.toBool();
	}
	return set;
}

QString ParseLimits(const QJsonObject &object, TaskLimits &into) {
	if (object.contains(u"daily_cost"_q)) {
		const auto value = object.value(u"daily_cost"_q);
		if (!value.isDouble()) {
			return u"limits.daily_cost must be a number."_q;
		}
		const auto cost = value.toDouble();
		if (cost < 0.01 || cost > 100.) {
			return u"limits.daily_cost must be between 0.01 and 100 dollars."_q;
		}
		into.dailyCost = cost;
	}
	if (object.contains(u"daily_tokens"_q)) {
		const auto value = object.value(u"daily_tokens"_q);
		if (!value.isDouble()) {
			return u"limits.daily_tokens must be a number."_q;
		}
		const auto tokens = qint64(value.toDouble());
		if (tokens < 1000 || tokens > 100'000'000) {
			return u"limits.daily_tokens must be between 1,000 and 100,000,000."_q;
		}
		into.dailyTokens = tokens;
	}
	if (object.contains(u"actions_per_hour"_q)) {
		const auto value = object.value(u"actions_per_hour"_q);
		if (!value.isDouble()) {
			return u"limits.actions_per_hour must be a number."_q;
		}
		const auto perHour = int(value.toDouble());
		if (perHour < 1 || perHour > 1000) {
			return u"limits.actions_per_hour must be between 1 and 1,000."_q;
		}
		into.actionsPerHour = perHour;
	}
	return QString();
}

QString ParseNotify(const QJsonObject &object, TaskNotify &into) {
	if (!object.contains(u"notify"_q)) {
		return QString();
	}
	const auto value = object.value(u"notify"_q);
	if (value.toString() == u"actions"_q) {
		into = TaskNotify::Actions;
	} else if (value.toString() == u"log_only"_q) {
		into = TaskNotify::LogOnly;
	} else {
		return u"notify must be actions or log_only."_q;
	}
	return QString();
}

QString ParseCatchUp(const QJsonObject &object, bool &into) {
	if (!object.contains(u"catch_up"_q)) {
		return QString();
	}
	const auto value = object.value(u"catch_up"_q);
	if (!value.isBool()) {
		return u"catch_up must be a boolean."_q;
	}
	into = value.toBool();
	return QString();
}

QString ParseText(
		const QJsonObject &object,
		const QString &field,
		int maxChars,
		QString &into) {
	if (!object.contains(field)) {
		return QString();
	}
	const auto value = object.value(field);
	if (!value.isString()) {
		return field + u" must be a string."_q;
	}
	const auto text = value.toString();
	if (text.isEmpty()) {
		return field + u" must not be empty."_q;
	} else if (text.size() > maxChars) {
		return field + u" must be at most "_q
			+ QString::number(maxChars) + u" characters."_q;
	}
	into = text;
	return QString();
}

} // namespace

QByteArray SerializeTask(const Task &task) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kTaskVersion;
	WriteTask(stream, task);
	return result;
}

std::optional<Task> DeserializeTask(const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kTaskVersion) {
		return std::nullopt;
	}
	auto result = ReadTask(stream, version);
	if (!result || !stream.atEnd()) {
		return std::nullopt;
	}
	return result;
}

QByteArray SerializeTasks(const std::vector<Task> &tasks) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kTaskVersion << qint32(tasks.size());
	for (const auto &task : tasks) {
		WriteTask(stream, task);
	}
	return result;
}

std::optional<std::vector<Task>> DeserializeTasks(
		const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kTaskVersion) {
		return std::nullopt;
	}
	const auto count = ReadCount(stream, kMaxSerializedTasks);
	if (!count) {
		return std::nullopt;
	}
	auto tasks = std::vector<Task>();
	tasks.reserve(*count);
	for (auto i = 0; i != *count; ++i) {
		auto task = ReadTask(stream, version);
		if (!task) {
			return std::nullopt;
		}
		tasks.push_back(std::move(*task));
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return tasks;
}

void WriteRun(QDataStream &stream, const TaskRun &run) {
	stream << run.sessionId << qint32(run.kind) << quint64(run.chat);
	stream << quint64(run.sender) << qint64(run.messageId);
	stream << run.preview << qint32(run.missed);
	stream << qint64(run.started) << qint64(run.finished);
	stream << qint32(int(run.outcome)) << qint32(run.actions);
	stream
		<< qint32(run.usage.inputTokens)
		<< qint32(run.usage.outputTokens)
		<< qint32(run.usage.cachedInputTokens)
		<< run.usage.cost
		<< qint32(run.usage.hasCost ? 1 : 0);
	stream << run.error;
	stream << qint32(run.usage.cacheWriteTokens);
}

std::optional<TaskRun> ReadRun(QDataStream &stream, quint32 version) {
	auto run = TaskRun();
	auto kind = qint32(0);
	auto missed = qint32(0);
	auto outcome = qint32(0);
	auto actions = qint32(0);
	auto input = qint32(0);
	auto output = qint32(0);
	auto cached = qint32(0);
	auto cost = 0.;
	auto hasCost = qint32(0);
	stream >> run.sessionId >> kind >> run.chat >> run.sender;
	stream >> run.messageId >> run.preview >> missed >> run.started;
	stream >> run.finished >> outcome >> actions;
	stream >> input >> output >> cached >> cost >> hasCost;
	stream >> run.error;
	if (stream.status() != QDataStream::Ok
		|| kind < 0
		|| kind > 1
		|| missed < 0
		|| outcome < 0
		|| outcome > 4
		|| actions < 0
		|| input < 0
		|| output < 0
		|| cached < 0
		|| (hasCost != 0 && hasCost != 1)) {
		return std::nullopt;
	}
	run.kind = TriggerKind(kind);
	run.missed = missed;
	run.outcome = RunOutcome(outcome);
	run.actions = actions;
	run.usage = Usage{ input, output, cached };
	run.usage.cost = cost;
	run.usage.hasCost = (hasCost != 0);
	if (version >= 2) {
		auto cacheWrites = qint32(0);
		stream >> cacheWrites;
		if (stream.status() != QDataStream::Ok || cacheWrites < 0) {
			return std::nullopt;
		}
		run.usage.cacheWriteTokens = cacheWrites;
	}
	return run;
}

QByteArray SerializeRuns(const std::vector<TaskRun> &runs) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kTaskVersion << qint32(runs.size());
	for (const auto &run : runs) {
		WriteRun(stream, run);
	}
	return result;
}

std::optional<std::vector<TaskRun>> DeserializeRuns(
		const QByteArray &bytes) {
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32(0);
	stream >> version;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kTaskVersion) {
		return std::nullopt;
	}
	const auto count = ReadCount(stream, kMaxSerializedRuns);
	if (!count) {
		return std::nullopt;
	}
	auto runs = std::vector<TaskRun>();
	runs.reserve(*count);
	for (auto i = 0; i != *count; ++i) {
		auto run = ReadRun(stream, version);
		if (!run) {
			return std::nullopt;
		}
		runs.push_back(std::move(*run));
	}
	if (!stream.atEnd()) {
		return std::nullopt;
	}
	return runs;
}

QJsonObject TaskInputSchema(bool chatScope, bool update) {
	auto properties = QJsonObject();
	if (update) {
		properties[u"task"_q] = SchemaText(
			u"The id of the task to change."_q);
	}
	properties[u"title"_q] = SchemaText(
		u"Short task name shown to the user."_q);
	properties[u"instruction"_q] = SchemaText(
		u"What the agent should do on every run. "
		u"Write it for the agent, not for the user."_q);
	auto trigger = QJsonObject();
	trigger[u"type"_q] = SchemaEnum(
		QJsonArray{ u"message"_q, u"schedule"_q, u"once"_q },
		u"message: a matching new message (your own messages trigger "
		u"only inside Saved Messages); schedule: recurring runs; "
		u"once: a single run."_q);
	trigger[u"senders"_q] = SchemaArrayOf(
		SchemaText(u"A user id or an @username."_q),
		u"Match messages only from these senders."_q);
	trigger[u"keywords"_q] = SchemaArrayOf(
		SchemaText(u"Keyword."_q),
		u"Match messages containing any of these words, "
		u"case-insensitive."_q);
	trigger[u"regex"_q] = SchemaText(
		u"Match messages against this regular expression, case-insensitive."_q);
	trigger[u"media"_q] = SchemaArrayOf(
		SchemaEnum(MediaNames(0xFFFF), QString()),
		u"Match only messages that have one of these media types."_q);
	trigger[u"mentions_only"_q] = SchemaBoolean(
		u"Match only messages that mention you."_q);
	trigger[u"time_window"_q] = SchemaObject(
		QJsonObject{
			{ u"from"_q, SchemaText(
				u"Start of the window, HH:MM local time."_q) },
			{ u"till"_q, SchemaText(
				u"End of the window, HH:MM local time, exclusive."_q) },
			{ u"days"_q, SchemaArrayOf(
				SchemaEnum(DayNames(0x7F), QString()),
				u"Weekdays when the window applies."_q) },
		},
		QJsonArray(),
		u"Match messages only inside this local time window."_q);
	trigger[u"every_minutes"_q] = SchemaNumber(
		u"Run every this many minutes (schedule trigger)."_q);
	trigger[u"times"_q] = SchemaArrayOf(
		SchemaText(u"Time of day, HH:MM."_q),
		u"Run at these local times of day (schedule trigger)."_q);
	trigger[u"days"_q] = SchemaArrayOf(
		SchemaEnum(DayNames(0x7F), QString()),
		u"Limit schedule runs to these weekdays."_q);
	trigger[u"at"_q] = SchemaText(
		u"One run at this local time, YYYY-MM-DD HH:MM (once trigger)."_q);
	if (!chatScope) {
		trigger[u"chats"_q] = SchemaArrayOf(
			SchemaText(u"A chat id or an @username."_q),
			u"Match messages only in these chats."_q);
		trigger[u"chat_types"_q] = SchemaArrayOf(
			SchemaEnum(ChatTypeNames(0x0F), QString()),
			u"Match messages in all chats of these types."_q);
		trigger[u"folders"_q] = SchemaArrayOf(
			SchemaText(u"A folder id or its title."_q),
			u"Match messages in all chats of these folders."_q);
	}
	properties[u"trigger"_q] = SchemaObject(
		std::move(trigger),
		QJsonArray{ u"type"_q },
		u"When the task runs."_q);
	properties[u"allowed_actions"_q] = SchemaArrayOf(
		SchemaText(u"Action tool name."_q),
		u"Action tools the task may use without asking, like "
		u"send_message, react, forward_messages, mark_read, "
		u"set_mute or set_archived. Everything else waits for the user."_q);
	if (!chatScope) {
		auto allowed = QJsonObject();
		allowed[u"chats"_q] = SchemaArrayOf(
			SchemaText(u"A chat id or an @username."_q),
			u"Chat the task may work with."_q);
		allowed[u"chat_types"_q] = SchemaArrayOf(
			SchemaEnum(ChatTypeNames(0x0F), QString()),
			u"Chats of these types the task may work with."_q);
		allowed[u"folders"_q] = SchemaArrayOf(
			SchemaText(u"A folder id or its title."_q),
			u"Chats of these folders the task may work with."_q);
		allowed[u"trigger_chat"_q] = SchemaBoolean(
			u"Let the task work in the chat whose message triggered the run."_q);
		properties[u"allowed_chats"_q] = SchemaObject(
			std::move(allowed),
			QJsonArray(),
			u"Chats the task may work with without asking. Without it "
			u"a message task works in the trigger chat and a scheduled "
			u"task in none."_q);
	}
	properties[u"notify"_q] = SchemaEnum(
		QJsonArray{ u"actions"_q, u"log_only"_q },
		u"actions: notify the user about every visible action; "
		u"log_only: keep actions in the log."_q);
	properties[u"catch_up"_q] = SchemaBoolean(
		u"Process missed messages or schedules from while the app was closed."_q);
	auto limits = QJsonObject();
	limits[u"daily_cost"_q] = SchemaNumber(
		u"Maximum dollars the task may spend per day."_q);
	limits[u"daily_tokens"_q] = SchemaNumber(
		u"Maximum tokens the task may use per day."_q);
	limits[u"actions_per_hour"_q] = SchemaNumber(
		u"Maximum actions the task may take per hour."_q);
	properties[u"limits"_q] = SchemaObject(
		std::move(limits),
		QJsonArray(),
		u"Spending and activity limits of the task."_q);
	auto required = QJsonArray{ u"title"_q, u"instruction"_q, u"trigger"_q };
	if (update) {
		required = QJsonArray{ u"task"_q };
	}
	return SchemaObject(
		std::move(properties),
		std::move(required),
		u"Input for creating a background task."_q);
}

TaskParse ParseTaskInput(
		const QJsonObject &input,
		const Scope &scope,
		const TaskRefs &refs) {
	const auto chatScope = (scope.kind == ScopeKind::Chat);
	if (chatScope && input.contains(u"allowed_chats"_q)) {
		return { .error
			= u"allowed_chats is only available in the account scope."_q };
	}
	auto task = Task();
	task.scope = scope;
	if (const auto error = ParseText(
			input,
			u"title"_q,
			kTaskMaxTitleChars,
			task.title);
		!error.isEmpty()) {
		return { .error = error };
	} else if (task.title.isEmpty()) {
		return { .error = u"title must not be empty."_q };
	}
	if (const auto error = ParseText(
			input,
			u"instruction"_q,
			kTaskMaxInstructionChars,
			task.instruction);
		!error.isEmpty()) {
		return { .error = error };
	} else if (task.instruction.isEmpty()) {
		return { .error = u"instruction must not be empty."_q };
	}
	const auto triggerValue = input.value(u"trigger"_q);
	if (!triggerValue.isObject()) {
		return { .error = u"trigger must be a JSON object."_q };
	}
	auto trigger = ParseTrigger(triggerValue.toObject(), scope, refs);
	if (!trigger.error.isEmpty()) {
		return { .error = trigger.error };
	}
	task.trigger = std::move(*trigger.trigger);
	if (const auto error = ParseActions(input, refs, task.frame.tools);
		!error.isEmpty()) {
		return { .error = error };
	}
	if (input.contains(u"allowed_chats"_q)) {
		auto error = QString();
		auto chats = ParseAllowedChats(input, refs, error);
		if (!chats) {
			return { .error = error };
		}
		task.frame.chats = std::move(*chats);
		if (task.frame.chats.triggerChat
			&& task.trigger.kind != TriggerKind::Message) {
			return { .error
				= u"allowed_chats.trigger_chat requires a message trigger."_q };
		}
	} else if (chatScope) {
		task.frame.chats.peers = { scope.peer };
	} else if (task.trigger.kind == TriggerKind::Message) {
		task.frame.chats.triggerChat = true;
	}
	if (const auto error = ParseNotify(input, task.notify); !error.isEmpty()) {
		return { .error = error };
	}
	if (const auto error = ParseCatchUp(input, task.catchUp); !error.isEmpty()) {
		return { .error = error };
	}
	if (input.contains(u"limits"_q)) {
		const auto limits = input.value(u"limits"_q);
		if (!limits.isObject()) {
			return { .error = u"limits must be a JSON object."_q };
		}
		if (const auto error = ParseLimits(limits.toObject(), task.limits);
			!error.isEmpty()) {
			return { .error = error };
		}
	}
	return { .task = std::move(task) };
}

TaskParse ParseTaskUpdate(
		const Task &base,
		const QJsonObject &input,
		const TaskRefs &refs) {
	const auto id = input.value(u"task"_q);
	if (!id.isString() || id.toString().isEmpty()) {
		return { .error = u"task must be the id of the task to change."_q };
	}
	auto task = base;
	if (const auto error = ParseText(
			input,
			u"title"_q,
			kTaskMaxTitleChars,
			task.title);
		!error.isEmpty()) {
		return { .error = error };
	}
	if (const auto error = ParseText(
			input,
			u"instruction"_q,
			kTaskMaxInstructionChars,
			task.instruction);
		!error.isEmpty()) {
		return { .error = error };
	}
	if (input.contains(u"trigger"_q)) {
		const auto triggerValue = input.value(u"trigger"_q);
		if (!triggerValue.isObject()) {
			return { .error = u"trigger must be a JSON object."_q };
		}
		auto trigger = ParseTrigger(
			triggerValue.toObject(),
			base.scope,
			refs);
		if (!trigger.error.isEmpty()) {
			return { .error = trigger.error };
		}
		task.trigger = std::move(*trigger.trigger);
	}
	if (const auto error = ParseActions(input, refs, task.frame.tools);
		!error.isEmpty()) {
		return { .error = error };
	}
	if (base.scope.kind == ScopeKind::Chat
		&& input.contains(u"allowed_chats"_q)) {
		return { .error
			= u"allowed_chats is only available in the account scope."_q };
	}
	if (input.contains(u"allowed_chats"_q)) {
		auto error = QString();
		auto chats = ParseAllowedChats(input, refs, error);
		if (!chats) {
			return { .error = error };
		}
		task.frame.chats = std::move(*chats);
		if (task.frame.chats.triggerChat
			&& task.trigger.kind != TriggerKind::Message) {
			return { .error
				= u"allowed_chats.trigger_chat requires a message trigger."_q };
		}
	}
	if (const auto error = ParseNotify(input, task.notify); !error.isEmpty()) {
		return { .error = error };
	}
	if (const auto error = ParseCatchUp(input, task.catchUp); !error.isEmpty()) {
		return { .error = error };
	}
	if (input.contains(u"limits"_q)) {
		const auto limits = input.value(u"limits"_q);
		if (!limits.isObject()) {
			return { .error = u"limits must be a JSON object."_q };
		}
		if (const auto error = ParseLimits(limits.toObject(), task.limits);
			!error.isEmpty()) {
			return { .error = error };
		}
	}
	return { .task = std::move(task) };
}

QJsonObject TaskToJson(const Task &task, const QTimeZone &zone) {
	auto json = QJsonObject();
	json[u"id"_q] = task.id;
	json[u"title"_q] = task.title;
	json[u"instruction"_q] = task.instruction;
	const auto accountScope = (task.scope.kind == ScopeKind::Account);
	auto trigger = QJsonObject();
	if (task.trigger.kind == TriggerKind::Message) {
		trigger[u"type"_q] = u"message"_q;
		const auto &filter = task.trigger.filter;
		if (accountScope) {
			auto chats = QJsonArray();
			for (const auto peer : filter.chats.peers) {
				chats.append(double(peer));
			}
			trigger[u"chats"_q] = chats;
			trigger[u"chat_types"_q] = ChatTypeNames(filter.chats.kinds);
			auto folders = QJsonArray();
			for (const auto folder : filter.chats.folders) {
				folders.append(folder);
			}
			trigger[u"folders"_q] = folders;
		}
		if (!filter.senders.empty()) {
			auto senders = QJsonArray();
			for (const auto sender : filter.senders) {
				senders.append(double(sender));
			}
			trigger[u"senders"_q] = senders;
		}
		if (!filter.keywords.isEmpty()) {
			auto keywords = QJsonArray();
			for (const auto &keyword : filter.keywords) {
				keywords.append(keyword);
			}
			trigger[u"keywords"_q] = keywords;
		}
		if (!filter.regex.isEmpty()) {
			trigger[u"regex"_q] = filter.regex;
		}
		trigger[u"media"_q] = MediaNames(filter.media);
		trigger[u"mentions_only"_q] = filter.mentionsOnly;
		trigger[u"time_window"_q] = QJsonObject{
			{ u"from"_q, FormatTimeOfDay(filter.window.from) },
			{ u"till"_q, FormatTimeOfDay(filter.window.till) },
			{ u"days"_q, DayNames(filter.window.days) },
		};
	} else if (task.trigger.kind == TriggerKind::Schedule) {
		const auto &schedule = task.trigger.schedule;
		if (schedule.kind == ScheduleKind::Once) {
			trigger[u"type"_q] = u"once"_q;
			trigger[u"at"_q] = FormatStamp(schedule.at, zone);
		} else {
			trigger[u"type"_q] = u"schedule"_q;
			if (schedule.kind == ScheduleKind::Interval) {
				trigger[u"every_minutes"_q] = schedule.everyMinutes;
			} else {
				auto times = QJsonArray();
				for (const auto time : schedule.times) {
					times.append(FormatTimeOfDay(time));
				}
				trigger[u"times"_q] = times;
			}
			if (schedule.days) {
				trigger[u"days"_q] = DayNames(schedule.days);
			}
		}
	}
	json[u"trigger"_q] = trigger;
	auto actions = QJsonArray();
	for (const auto &tool : task.frame.tools) {
		actions.append(tool);
	}
	json[u"allowed_actions"_q] = actions;
	if (accountScope) {
		const auto &chats = task.frame.chats;
		auto allowed = QJsonObject();
		auto peers = QJsonArray();
		for (const auto peer : chats.peers) {
			peers.append(double(peer));
		}
		auto folders = QJsonArray();
		for (const auto folder : chats.folders) {
			folders.append(folder);
		}
		allowed[u"chats"_q] = peers;
		allowed[u"chat_types"_q] = ChatTypeNames(chats.kinds);
		allowed[u"folders"_q] = folders;
		allowed[u"trigger_chat"_q] = chats.triggerChat;
		json[u"allowed_chats"_q] = allowed;
	}
	json[u"notify"_q] = (task.notify == TaskNotify::Actions)
		? u"actions"_q
		: u"log_only"_q;
	json[u"catch_up"_q] = task.catchUp;
	json[u"limits"_q] = QJsonObject{
		{ u"daily_cost"_q, task.limits.dailyCost },
		{ u"daily_tokens"_q, double(task.limits.dailyTokens) },
		{ u"actions_per_hour"_q, task.limits.actionsPerHour },
	};
	switch (task.state) {
	case TaskState::Active: json[u"state"_q] = u"active"_q; break;
	case TaskState::Paused: json[u"state"_q] = u"paused"_q; break;
	case TaskState::DailyLimit: json[u"state"_q] = u"daily_limit"_q; break;
	case TaskState::HourlyLimit: json[u"state"_q] = u"hourly_limit"_q; break;
	case TaskState::Finished: json[u"state"_q] = u"finished"_q; break;
	}
	if ((task.state == TaskState::DailyLimit
		|| task.state == TaskState::HourlyLimit)
		&& task.pausedUntil) {
		json[u"paused_until"_q] = FormatStamp(task.pausedUntil, zone);
	}
	return json;
}

LimitHit CheckSpendingLimits(
		const TaskLimits &limits,
		const SpendingTotals &taskToday,
		double backgroundCost,
		qint64 backgroundTokens,
		const SpendingTotals &backgroundToday) {
	const auto hit = [](
			const SpendingTotals &spent,
			double costLimit,
			qint64 tokenLimit) {
		if (spent.hasCost && costLimit > 0.) {
			return spent.cost >= costLimit;
		} else if (tokenLimit > 0) {
			return spent.tokens() >= tokenLimit;
		}
		return false;
	};
	if (hit(taskToday, limits.dailyCost, limits.dailyTokens)) {
		return LimitHit::Task;
	} else if (hit(backgroundToday, backgroundCost, backgroundTokens)) {
		return LimitHit::Background;
	}
	return LimitHit::None;
}

int CountRecentActions(const std::vector<qint64> &times, qint64 now) {
	auto count = 0;
	for (const auto time : times) {
		if (time > now - 3600 && time <= now) {
			++count;
		}
	}
	return count;
}

qint64 HourlyResumeTime(
		const std::vector<qint64> &times,
		int perHour,
		qint64 now) {
	const auto recent = CountRecentActions(times, now);
	if (perHour <= 0 || recent < perHour) {
		return now;
	}
	auto oldest = std::vector<qint64>();
	oldest.reserve(recent);
	for (const auto time : times) {
		if (time > now - 3600 && time <= now) {
			oldest.push_back(time);
		}
	}
	std::sort(oldest.begin(), oldest.end());
	return oldest[recent - perHour] + 3600;
}

int CallActionCount(const QJsonObject &input) {
	const auto chats = input.value(u"chats"_q).toArray().size();
	const auto users = input.value(u"users"_q).toArray().size();
	return int(std::max({ qsizetype(1), chats, users }));
}

std::vector<qint64> RunActionTimes(
		const std::vector<TaskRun> &runs,
		qint64 now) {
	auto result = std::vector<qint64>();
	for (const auto &run : runs) {
		const auto time = run.finished ? run.finished : run.started;
		if (time > now - 3600 && time <= now && run.actions > 0) {
			result.insert(result.end(), run.actions, time);
		}
	}
	return result;
}

} // namespace Agent
