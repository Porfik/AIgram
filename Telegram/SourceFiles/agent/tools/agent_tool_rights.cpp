/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_rights.h"

#include <QtCore/QJsonArray>

namespace Agent::ToolRights {
namespace {

template <typename Flag>
struct Named {
	Flag flag;
	const char *name;
};

using AdminRight = Named<ChatAdminRight>;
using Restriction = Named<ChatRestriction>;

const auto kAdminRights = std::array{
	AdminRight{ ChatAdminRight::ChangeInfo, "change_info" },
	AdminRight{ ChatAdminRight::PostMessages, "post_messages" },
	AdminRight{ ChatAdminRight::EditMessages, "edit_messages" },
	AdminRight{ ChatAdminRight::DeleteMessages, "delete_messages" },
	AdminRight{ ChatAdminRight::BanUsers, "ban_users" },
	AdminRight{ ChatAdminRight::InviteByLinkOrAdd, "invite_users" },
	AdminRight{ ChatAdminRight::PinMessages, "pin_messages" },
	AdminRight{ ChatAdminRight::AddAdmins, "add_admins" },
	AdminRight{ ChatAdminRight::Anonymous, "anonymous" },
	AdminRight{ ChatAdminRight::ManageCall, "manage_call" },
	AdminRight{ ChatAdminRight::ManageTopics, "manage_topics" },
	AdminRight{ ChatAdminRight::PostStories, "post_stories" },
	AdminRight{ ChatAdminRight::EditStories, "edit_stories" },
	AdminRight{ ChatAdminRight::DeleteStories, "delete_stories" },
	AdminRight{ ChatAdminRight::ManageDirect, "manage_direct_messages" },
};

const auto kRestrictions = std::array{
	Restriction{ ChatRestriction::ViewMessages, "view_messages" },
	Restriction{ ChatRestriction::SendOther, "send_text" },
	Restriction{ ChatRestriction::SendPhotos, "send_photos" },
	Restriction{ ChatRestriction::SendVideos, "send_videos" },
	Restriction{ ChatRestriction::SendVideoMessages, "send_round_videos" },
	Restriction{ ChatRestriction::SendMusic, "send_audios" },
	Restriction{ ChatRestriction::SendVoiceMessages, "send_voices" },
	Restriction{ ChatRestriction::SendFiles, "send_docs" },
	Restriction{ ChatRestriction::SendStickers, "send_stickers" },
	Restriction{ ChatRestriction::SendGifs, "send_gifs" },
	Restriction{ ChatRestriction::SendGames, "send_games" },
	Restriction{ ChatRestriction::SendInline, "send_inline" },
	Restriction{ ChatRestriction::EmbedLinks, "embed_links" },
	Restriction{ ChatRestriction::SendPolls, "send_polls" },
	Restriction{ ChatRestriction::SendReactions, "send_reactions" },
	Restriction{ ChatRestriction::ChangeInfo, "change_info" },
	Restriction{ ChatRestriction::AddParticipants, "invite_users" },
	Restriction{ ChatRestriction::PinMessages, "pin_messages" },
	Restriction{ ChatRestriction::CreateTopics, "manage_topics" },
};

template <typename Flags, typename List>
[[nodiscard]] QStringList Names(Flags flags, const List &list) {
	auto result = QStringList();
	for (const auto &entry : list) {
		if (flags & entry.flag) {
			result.push_back(QString::fromLatin1(entry.name));
		}
	}
	return result;
}

template <typename List>
[[nodiscard]] QStringList AllNames(const List &list) {
	auto result = QStringList();
	for (const auto &entry : list) {
		result.push_back(QString::fromLatin1(entry.name));
	}
	return result;
}

template <typename Flags, typename List>
[[nodiscard]] std::optional<Flags> Parse(
		const QJsonValue &value,
		const List &list,
		QString &error) {
	if (!value.isArray()) {
		error = u"Expected an array of names: %1."_q.arg(
			AllNames(list).join(u", "_q));
		return std::nullopt;
	}
	auto result = Flags();
	for (const auto &item : value.toArray()) {
		const auto name = item.toString().trimmed().toLower();
		const auto i = ranges::find_if(list, [&](const auto &entry) {
			return (name == QLatin1String(entry.name));
		});
		if (i == end(list)) {
			error = u"Unknown name: %1. The names are: %2."_q.arg(
				item.toString(),
				AllNames(list).join(u", "_q));
			return std::nullopt;
		}
		result |= i->flag;
	}
	return result;
}

} // namespace

const QStringList &AllAdminRightNames() {
	static const auto result = AllNames(kAdminRights);
	return result;
}

QStringList AdminRightNames(ChatAdminRights rights) {
	return Names(rights, kAdminRights);
}

std::optional<ChatAdminRights> ParseAdminRights(
		const QJsonValue &value,
		QString &error) {
	return Parse<ChatAdminRights>(value, kAdminRights, error);
}

const QStringList &AllRestrictionNames() {
	static const auto result = AllNames(kRestrictions);
	return result;
}

QStringList RestrictionNames(ChatRestrictions restrictions) {
	return Names(restrictions, kRestrictions);
}

std::optional<ChatRestrictions> ParseRestrictions(
		const QJsonValue &value,
		QString &error) {
	return Parse<ChatRestrictions>(value, kRestrictions, error);
}

} // namespace Agent::ToolRights
