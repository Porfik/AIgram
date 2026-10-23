/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_files.h"
#include "api/api_peer_photo.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/image/image.h"

#include <QtCore/QJsonArray>
#include <QtCore/QTimer>

namespace Agent {
namespace {

constexpr auto kEmojiLoadTimeout = crl::time(10 * 1000);
constexpr auto kMaxEmojiStatusMinutes = 525600;

[[nodiscard]] bool Present(const QJsonValue &value) {
	return !value.isUndefined() && !value.isNull();
}

[[nodiscard]] bool Given(const QJsonValue &value) {
	return Present(value)
		&& !(value.isString() && value.toString().isEmpty());
}

[[nodiscard]] QString FormatUntil(TimeId until) {
	return until
		? QDateTime::fromSecsSinceEpoch(until).toString(u"yyyy-MM-dd HH:mm"_q)
		: QString();
}

[[nodiscard]] QString StatusEmoji(
		not_null<Main::Session*> session,
		const EmojiStatusId &id) {
	if (id.collectible || !id.documentId) {
		return QString();
	}
	const auto document = session->data().document(id.documentId);
	const auto sticker = document ? document->sticker() : nullptr;
	return sticker ? sticker->alt : QString();
}

void RunUpdateProfileLoaded(not_null<PendingCall*> call);

void RunUpdateProfile(not_null<PendingCall*> call) {
	WhenFullLoaded(call, call->session->user(), [=] {
		RunUpdateProfileLoaded(call);
	});
}

void RunUpdateProfileLoaded(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto user = session->user();
	const auto firstValue = call->input.value("first_name");
	const auto lastValue = call->input.value("last_name");
	const auto bioValue = call->input.value("bio");
	const auto hasFirst = Present(firstValue);
	const auto hasLast = Present(lastValue);
	const auto hasBio = Present(bioValue);
	if (!hasFirst && !hasLast && !hasBio) {
		call->fail(u"Provide first_name, last_name or bio to change."_q);
		return;
	}
	const auto first = firstValue.toString().trimmed();
	const auto last = lastValue.toString().trimmed();
	const auto bio = bioValue.toString().trimmed();
	if (hasFirst && first.isEmpty()) {
		call->fail(u"first_name must not be empty."_q);
		return;
	}
	if (hasBio) {
		const auto limits = Data::PremiumLimits(session);
		const auto limit = user->isPremium()
			? limits.aboutLengthPremium()
			: limits.aboutLengthDefault();
		if (bio.size() > limit) {
			call->fail(u"The bio must be at most %1 characters."_q.arg(limit));
			return;
		}
	}
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_update_profile(tr::now);
	card.detail = u"update_profile"_q;
	if (hasFirst) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_first_name(tr::now),
			.value = first,
			.previous = user->firstName,
		});
	}
	if (hasLast) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_last_name(tr::now),
			.value = last,
			.previous = user->lastName,
		});
	}
	if (hasBio) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_bio(tr::now),
			.value = bio,
			.previous = user->about(),
		});
	}
	using Flag = MTPaccount_UpdateProfile::Flag;
	const auto flags = Flag(0)
		| (hasFirst ? Flag::f_first_name : Flag(0))
		| (hasLast ? Flag::f_last_name : Flag(0))
		| (hasBio ? Flag::f_about : Flag(0));
	call->confirm(std::move(card), [=](Card decided) {
		call->api.request(MTPaccount_UpdateProfile(
			MTP_flags(flags),
			hasFirst ? MTP_string(first) : MTPstring(),
			hasLast ? MTP_string(last) : MTPstring(),
			hasBio ? MTP_string(bio) : MTPstring()
		)).done([=](const MTPUser &result) mutable {
			session->data().processUser(result);
			if (hasBio) {
				session->user()->setAbout(bio);
			}
			decided.status = CardStatus::Done;
			call->finishCard(std::move(decided));
		}).fail([=](const MTP::Error &error) {
			call->fail(MtpErrorText(error));
		}).send();
	});
}

struct LoadedImage {
	QString ref;
	QString name;
	QString mimeType;
	int64 size = 0;
	QImage image;
};

[[nodiscard]] std::optional<LoadedImage> ReadImage(
		not_null<PendingCall*> call,
		const QJsonValue &attachment,
		const QJsonValue &file) {
	const auto session = call->session;
	if (Given(attachment)) {
		const auto ref = attachment.toString().trimmed();
		const auto block = call->readAttachment(ref);
		if (!block) {
			call->fail(u"There is no attachment %1 in this session."_q.arg(ref));
			return std::nullopt;
		}
		auto read = Images::Read({ .content = block->bytes });
		if (read.image.isNull()) {
			call->fail(u"The attachment %1 is not an image."_q.arg(ref));
			return std::nullopt;
		}
		return LoadedImage{
			.ref = ref,
			.name = block->name,
			.mimeType = block->mimeType,
			.size = block->size,
			.image = std::move(read.image),
		};
	}
	const auto name = file.toString().trimmed();
	const auto path = ResolveAgentFile(session, name);
	if (path.isEmpty()) {
		call->fail(u"%1 is neither an attachment of this session nor a"
			" file in the agent folder. Call list_agent_files."_q
			.arg(name));
		return std::nullopt;
	}
	auto content = QByteArray();
	{
		auto f = QFile(path);
		if (!f.open(QIODevice::ReadOnly)) {
			call->fail(u"Could not read file %1."_q.arg(name));
			return std::nullopt;
		}
		content = f.readAll();
	}
	auto read = Images::Read({ .content = content });
	if (read.image.isNull()) {
		call->fail(u"The file %1 is not an image."_q.arg(name));
		return std::nullopt;
	}
	const auto info = QFileInfo(path);
	return LoadedImage{
		.ref = name,
		.name = info.fileName(),
		.mimeType = Core::MimeTypeForFile(info).name(),
		.size = info.size(),
		.image = std::move(read.image),
	};
}

void RunSetProfilePhoto(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto attachment = call->input.value("attachment");
	const auto file = call->input.value("file");
	const auto removeValue = call->input.value("remove");
	const auto given = int(Given(attachment))
		+ int(Given(file))
		+ int(removeValue.isBool() && removeValue.toBool());
	if (given != 1) {
		call->fail(u"Provide exactly one of attachment, file or"
			" remove: true."_q);
		return;
	}
	auto card = Card{ .kind = CardKind::Change };
	card.detail = u"set_profile_photo"_q;
	if (removeValue.isBool() && removeValue.toBool()) {
		const auto photoId = session->user()->userpicPhotoId();
		if (!photoId || photoId == UserData::kUnknownPhotoId) {
			call->fail(u"The user has no profile photo to remove."_q);
			return;
		}
		const auto photo = session->data().photo(photoId);
		card.text = tr::lng_agent_card_title_remove_profile_photo(tr::now);
		card.rows.push_back({
			.label = tr::lng_agent_card_row_photo(tr::now),
			.value = tr::lng_agent_card_value_none(tr::now),
		});
		call->confirm(std::move(card), [=](Card decided) {
			session->api().peerPhoto().clear(photo);
			decided.status = CardStatus::Done;
			call->finishCard(std::move(decided));
		});
		return;
	}
	const auto loaded = ReadImage(call, attachment, file);
	if (!loaded) {
		return;
	}
	const auto user = session->user();
	card.text = tr::lng_agent_card_title_profile_photo(tr::now);
	card.rows.push_back({
		.label = tr::lng_agent_card_row_photo(tr::now),
		.value = loaded->name,
	});
	card.files.push_back(CardFile{
		.ref = loaded->ref,
		.name = loaded->name,
		.mimeType = loaded->mimeType,
		.size = loaded->size,
	});
	const auto image = loaded->image;
	call->confirm(std::move(card), [=](Card decided) {
		session->api().peerPhoto().uploadFailed(
		) | rpl::filter([=](not_null<PeerData*> failed) {
			return failed == user;
		}) | rpl::on_next([=] {
			call->fail(u"Uploading the profile photo failed."_q);
		}, call->lifetime);
		session->api().peerPhoto().upload(
			user,
			{ .image = base::duplicate(image) },
			crl::guard(call.get(), [=]() mutable {
				decided.status = CardStatus::Done;
				call->finishCard(std::move(decided));
			}));
	});
}

void DeliverEmojiStatuses(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto user = session->user();
	const auto &statuses = session->data().emojiStatuses();
	auto json = QJsonObject();
	const auto current = user->emojiStatusId();
	if (current) {
		auto entry = QJsonObject();
		entry.insert("id", QString::number(current.documentId));
		const auto emoji = StatusEmoji(session, current);
		if (!emoji.isEmpty()) {
			entry.insert("emoji", emoji);
		}
		const auto until = statuses.automaticClearAt(user);
		if (until > 0) {
			entry.insert("until", FormatUntil(until));
		}
		json.insert("current", entry);
	} else {
		json.insert("current", QJsonValue());
	}
	auto list = QJsonArray();
	for (const auto type : { Data::EmojiStatuses::Type::Recent,
			Data::EmojiStatuses::Type::Default }) {
		for (const auto &id : statuses.list(type)) {
			const auto emoji = StatusEmoji(session, id);
			if (emoji.isEmpty()) {
				continue;
			}
			auto entry = QJsonObject();
			entry.insert("id", QString::number(id.documentId));
			entry.insert("emoji", emoji);
			list.push_back(entry);
		}
	}
	json.insert("statuses", list);
	call->done(std::move(json));
}

void RunListEmojiStatuses(not_null<PendingCall*> call) {
	const auto session = call->session;
	auto &statuses = session->data().emojiStatuses();
	const auto recentEmpty = statuses
		.list(Data::EmojiStatuses::Type::Recent)
		.empty();
	const auto defaultEmpty = statuses
		.list(Data::EmojiStatuses::Type::Default)
		.empty();
	if (!recentEmpty && !defaultEmpty) {
		DeliverEmojiStatuses(call);
		return;
	}
	if (recentEmpty) {
		statuses.refreshRecent();
	}
	if (defaultEmpty) {
		statuses.refreshDefault();
	}
	const auto remaining = std::make_shared<int>(
		int(recentEmpty) + int(defaultEmpty));
	const auto delivered = std::make_shared<bool>(false);
	const auto deliver = [=] {
		if (*delivered) {
			return;
		}
		*delivered = true;
		DeliverEmojiStatuses(call);
	};
	const auto count = [=] {
		if (--*remaining == 0) {
			deliver();
		}
	};
	if (recentEmpty) {
		statuses.recentUpdates() | rpl::on_next(count, call->lifetime);
	}
	if (defaultEmpty) {
		statuses.defaultUpdates() | rpl::on_next(count, call->lifetime);
	}
	QTimer::singleShot(
		kEmojiLoadTimeout,
		crl::guard(call.get(),
		[=] { deliver(); }));
}

void RunSetEmojiStatus(not_null<PendingCall*> call) {
	const auto session = call->session;
	if (!session->premium()) {
		call->fail(u"Emoji statuses need Telegram Premium."_q);
		return;
	}
	const auto clearValue = call->input.value("clear");
	const auto idValue = call->input.value("id");
	const auto emojiValue = call->input.value("emoji");
	const auto untilValue = call->input.value("until");
	const auto durationValue = call->input.value("duration_minutes");
	const auto clearing = clearValue.isBool() && clearValue.toBool();
	const auto givenId = Given(idValue);
	const auto givenEmoji = Given(emojiValue);
	const auto given = int(clearing) + int(givenId) + int(givenEmoji);
	if (given != 1) {
		call->fail(u"Provide exactly one of id, emoji or clear: true."_q);
		return;
	}
	auto id = EmojiStatusId();
	auto statusEmoji = QString();
	if (clearing) {
		id = EmojiStatusId();
	} else if (givenId) {
		const auto text = idValue.toString().trimmed();
		auto ok = false;
		const auto documentId = text.toULongLong(&ok);
		if (!ok || !documentId) {
			call->fail(u"id must be a document id from"
				" list_emoji_statuses."_q);
			return;
		}
		id = EmojiStatusId{ .documentId = documentId };
		statusEmoji = StatusEmoji(session, id);
	} else {
		const auto emoji = emojiValue.toString();
		const auto &statuses = session->data().emojiStatuses();
		for (const auto type : { Data::EmojiStatuses::Type::Recent,
				Data::EmojiStatuses::Type::Default }) {
			const auto i = ranges::find_if(statuses.list(type), [&](
					const EmojiStatusId &listed) {
				return StatusEmoji(session, listed) == emoji;
			});
			if (i != end(statuses.list(type))) {
				id = *i;
				statusEmoji = emoji;
				break;
			}
		}
		if (!id) {
			call->fail(u"Unknown emoji: %1. Use one from"
				" list_emoji_statuses."_q.arg(emoji.left(64)));
			return;
		}
	}
	auto error = QString();
	TimeId until = 0;
	if (Given(untilValue) && Given(durationValue)) {
		call->fail(u"Use either until or duration_minutes, not both."_q);
		return;
	} else if (Given(untilValue)) {
		const auto parsed = ParseLocalTime(untilValue, error);
		if (!error.isEmpty()) {
			call->fail(error);
			return;
		}
		until = *parsed;
	} else if (Given(durationValue)) {
		if (!durationValue.isDouble()) {
			call->fail(u"duration_minutes must be a number."_q);
			return;
		}
		const auto minutes = durationValue.toInt();
		if (minutes < 1 || minutes > kMaxEmojiStatusMinutes) {
			call->fail(u"duration_minutes must be between 1 and %1."_q
				.arg(kMaxEmojiStatusMinutes));
			return;
		}
		until = base::unixtime::now() + TimeId(minutes) * 60;
	}
	const auto user = session->user();
	const auto oldEmoji = StatusEmoji(session, user->emojiStatusId());
	const auto oldUntil = session->data().emojiStatuses().automaticClearAt(
		user);
	auto card = Card{ .kind = CardKind::Change };
	card.detail = u"set_emoji_status"_q;
	card.text = clearing
		? tr::lng_agent_card_title_clear_emoji_status(tr::now)
		: tr::lng_agent_card_title_emoji_status(tr::now);
	card.rows.push_back({
		.label = tr::lng_agent_card_row_status(tr::now),
		.value = clearing
			? tr::lng_agent_card_value_none(tr::now)
			: statusEmoji,
		.previous = oldEmoji,
	});
	card.rows.push_back({
		.label = tr::lng_agent_card_row_until(tr::now),
		.value = until
			? FormatUntil(until)
			: tr::lng_agent_card_value_forever(tr::now),
		.previous = FormatUntil(oldUntil),
	});
	call->confirm(std::move(card), [=](Card decided) {
		session->data().emojiStatuses().set(id, until);
		decided.status = CardStatus::Done;
		call->finishCard(std::move(decided));
	});
}

} // namespace

namespace ProfileTools {

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	const auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	push(ToolEntry{
		.name = u"update_profile"_q,
		.description = u"Change the user's own first name, last name or bio."
			" The user confirms it on a card that shows the old and new"
			" values. For a group's or channel's title use edit_chat_info;"
			" the username and birthday are changed in Settings"
			" (find_setting)."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("first_name", StringProperty(
					u"New first name, must not be empty."_q));
				properties.insert("last_name", StringProperty(
					u"New last name, empty to remove it."_q));
				properties.insert("bio", StringProperty(
					u"New bio under the name, within the account's"
						" limit."_q));
				return properties;
			}(),
			QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = false,
			.outbound = true,
		},
		.group = u"profile"_q,
		.run = RunUpdateProfile,
	});
	push(ToolEntry{
		.name = u"set_profile_photo"_q,
		.description = u"Set or remove the user's own profile photo. The"
			" image comes from an attachment or an agent folder file."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("attachment", StringProperty(
					u"Attachment id, like \"a1\"."_q));
				properties.insert("file", StringProperty(
					u"Image file name in the agent folder."_q));
				properties.insert("remove", BoolProperty(
					u"True to remove the current profile photo."_q));
				return properties;
			}(),
			QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = false,
			.outbound = true,
		},
		.group = u"profile"_q,
		.run = RunSetProfilePhoto,
	});
	push(ToolEntry{
		.name = u"list_emoji_statuses"_q,
		.description = u"List the current emoji status and the recent and"
			" default emoji statuses with their document ids."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = true,
			.chat = false,
		},
		.group = u"profile"_q,
		.untrusted = false,
		.run = RunListEmojiStatuses,
	});
	push(ToolEntry{
		.name = u"set_emoji_status"_q,
		.description = u"Set, schedule or clear the user's own emoji status."
			" Needs Telegram Premium."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				auto minutes = QJsonObject();
				minutes.insert("type", QJsonValue(u"integer"_q));
				minutes.insert("description",
					u"Keep the status for this many minutes, 1 to"
						" 525600."_q);
				minutes.insert("minimum", QJsonValue(1));
				minutes.insert("maximum", QJsonValue(kMaxEmojiStatusMinutes));
				properties.insert("id", StringProperty(
					u"Custom emoji document id from"
						" list_emoji_statuses."_q));
				properties.insert("emoji", StringProperty(
					u"An emoji from list_emoji_statuses."_q));
				properties.insert("until", StringProperty(
					u"Keep the status until this local time as"
						" YYYY-MM-DD HH:MM."_q));
				properties.insert("duration_minutes", minutes);
				properties.insert("clear", BoolProperty(
					u"True to remove the current emoji status."_q));
				return properties;
			}(),
			QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = false,
		},
		.group = u"profile"_q,
		.run = RunSetEmojiStatus,
	});
	return result;
}

} // namespace Agent::ProfileTools

} // namespace Agent
