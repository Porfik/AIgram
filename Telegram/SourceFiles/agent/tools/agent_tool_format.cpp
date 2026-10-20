/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_format.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "ui/text/text_entity.h"

#include <QtCore/QDateTime>
#include <QtCore/QStringList>

#include <algorithm>

namespace Agent::ToolFormat {
namespace {

QString WrapEntity(const EntityInText &entity, const QString &inner) {
	switch (entity.type()) {
	case EntityType::Bold:
	case EntityType::Semibold:
		return u"**%1**"_q.arg(inner);
	case EntityType::Italic:
		return inner.isEmpty() ? inner : u"_%1_"_q.arg(inner);
	case EntityType::StrikeOut:
		return u"~~%1~~"_q.arg(inner);
	case EntityType::Code:
		return u"`%1`"_q.arg(inner);
	case EntityType::Pre: {
		const auto language = entity.data();
		return language.isEmpty()
			? u"```\n%1\n```"_q.arg(inner)
			: u"```%1\n%2\n```"_q.arg(language, inner);
	}
	case EntityType::Spoiler:
		return u"||%1||"_q.arg(inner);
	case EntityType::CustomUrl:
		return u"[%1](%2)"_q.arg(inner, entity.data());
	default:
		return inner;
	}
}

QString RenderRange(
		const QString &text,
		const EntitiesInText &entities,
		int from,
		int to) {
	auto result = QString();
	auto pos = from;
	for (auto i = 0; i != entities.size(); ++i) {
		const auto &entity = entities[i];
		const auto start = std::max(entity.offset(), from);
		const auto end = std::min(entity.offset() + entity.length(), to);
		if (start < pos || start >= end) {
			continue;
		}
		result += text.mid(pos, start - pos);
		auto children = EntitiesInText();
		for (auto j = i + 1; j != entities.size(); ++j) {
			const auto &child = entities[j];
			if (child.offset() >= start
				&& child.offset() + child.length() <= end) {
				children.push_back(child);
			}
		}
		result += WrapEntity(entity, RenderRange(text, children, start, end));
		pos = end;
	}
	result += text.mid(pos, to - pos);
	return result;
}

QString FormatDuration(crl::time duration) {
	const auto total = static_cast<int>(duration / 1000);
	return u"%1:%2"_q.arg(total / 60).arg(
		QString::number(total % 60).rightJustified(2, u'0'));
}

} // namespace

QString PeerUsername(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->username();
	} else if (const auto channel = peer->asChannel()) {
		return channel->username();
	}
	return QString();
}

QString PeerKind(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		if (user->isSelf()) {
			return u"saved messages"_q;
		}
		return user->isBot() ? u"bot"_q : u"private chat"_q;
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isBroadcast()) {
			return u"channel"_q;
		}
		return channel->isForum() ? u"forum"_q : u"supergroup"_q;
	} else if (peer->asChat()) {
		return u"group"_q;
	}
	return QString();
}

QString EntitiesToMarkdown(const TextWithEntities &text) {
	if (text.entities.isEmpty()) {
		return text.text;
	}
	auto sorted = text.entities;
	std::sort(
		sorted.begin(),
		sorted.end(),
		[](const EntityInText &a, const EntityInText &b) {
			if (a.offset() != b.offset()) {
				return a.offset() < b.offset();
			}
			return a.length() > b.length();
		});
	return RenderRange(text.text, sorted, 0, text.text.size());
}

QString MediaDescription(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	}
	if (const auto poll = media->poll()) {
		return u"poll: %1"_q.arg(poll->question.text);
	}
	if (const auto contact = media->sharedContact()) {
		return u"contact %1"_q.arg(
			QStringList{ contact->firstName, contact->lastName }
				.join(u' '));
	}
	if (media->location()) {
		return u"location"_q;
	}
	if (const auto document = media->document()) {
		if (const auto sticker = document->sticker()) {
			return sticker->alt.isEmpty()
				? u"sticker"_q
				: u"sticker %1"_q.arg(sticker->alt);
		}
		const auto duration = document->duration();
		const auto withDuration = duration
			? u" %1"_q.arg(FormatDuration(duration))
			: QString();
		if (document->isVideoMessage()) {
			return u"round video%1"_q.arg(withDuration);
		} else if (document->isVoiceMessage()) {
			return u"voice message%1"_q.arg(withDuration);
		} else if (document->isAudioFile()) {
			return u"audio %1%2"_q.arg(document->filename(), withDuration);
		} else if (document->isImage()) {
			return u"image file %1"_q.arg(document->filename());
		} else if (document->isAnimation()) {
			return u"GIF%1"_q.arg(withDuration);
		} else if (document->isVideoFile()) {
			return u"video%1"_q.arg(withDuration);
		}
		return u"file %1"_q.arg(document->filename());
	}
	if (media->photo()) {
		return u"photo"_q;
	}
	if (media->webpage()) {
		return u"web page"_q;
	}
	if (media->game()) {
		return u"game"_q;
	}
	return u"media"_q;
}

QJsonObject SerializeMessage(not_null<HistoryItem*> item) {
	auto result = QJsonObject();
	result.insert("id", QJsonValue(qint64(item->id.bare)));
	result.insert(
		"date",
		QDateTime::fromSecsSinceEpoch(item->date())
			.toString(u"yyyy-MM-dd HH:mm"_q));
	if (item->isService()) {
		result.insert("service", item->notificationText().text);
		return result;
	}
	if (item->out()) {
		result.insert("from", QJsonValue(u"me"_q));
	} else {
		const auto sender = item->from();
		auto from = QJsonObject();
		from.insert("id", QJsonValue(qint64(sender->id.value)));
		from.insert("name", sender->name());
		const auto username = PeerUsername(sender);
		if (!username.isEmpty()) {
			from.insert("username", username);
		}
		result.insert("from", from);
	}
	result.insert("text", EntitiesToMarkdown(item->originalText()));
	if (const auto replyTo = item->replyToId()) {
		result.insert("reply_to", QJsonValue(qint64(replyTo.bare)));
	}
	if (const auto topic = item->topicRootId()) {
		result.insert("topic_id", QJsonValue(qint64(topic.bare)));
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		result.insert(
			"forwarded_from",
			forwarded->originalSender
				? forwarded->originalSender->name()
				: (forwarded->originalHiddenSenderInfo
					? forwarded->originalHiddenSenderInfo->name
					: QString()));
	}
	const auto media = MediaDescription(item);
	if (!media.isEmpty()) {
		result.insert("media", media);
	}
	if (item->Get<HistoryMessageEdited>()) {
		result.insert("edited", QJsonValue(true));
	}
	return result;
}

QJsonObject SerializeMessageWithChat(not_null<HistoryItem*> item) {
	auto result = SerializeMessage(item);
	const auto peer = item->history()->peer;
	auto chat = QJsonObject();
	chat.insert("id", QJsonValue(qint64(peer->id.value)));
	chat.insert("title", peer->name());
	result.insert("chat", chat);
	return result;
}

} // namespace Agent::ToolFormat
