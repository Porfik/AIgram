/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_mtproto.h"

#include <algorithm>
#include <cmath>

namespace Agent {
namespace {

const auto kForbiddenNamespaces = std::array{
	u"auth"_q,
	u"payments"_q,
	u"wallet"_q,
	u"toncenter"_q,
	u"smsjobs"_q,
	u"fragment"_q,
};

const auto kForbiddenSubstrings = std::array{
	u"Password"_q,
	u"Authorization"_q,
	u"SecureValue"_q,
	u"Takeout"_q,
	u"WebView"_q,
	u"UrlAuth"_q,
	u"Star"_q,
	u"Gift"_q,
	u"Paid"_q,
};

const auto kForbiddenNames = std::array{
	u"account.deleteAccount"_q,
	u"account.setAccountTTL"_q,
	u"account.sendChangePhoneCode"_q,
	u"account.changePhone"_q,
	u"account.sendConfirmPhoneCode"_q,
	u"account.confirmPhone"_q,
	u"account.sendVerifyEmailCode"_q,
	u"account.verifyEmail"_q,
	u"account.sendVerifyPhoneCode"_q,
	u"account.verifyPhone"_q,
	u"account.invalidateSignInCodes"_q,
	u"account.resetPassword"_q,
	u"account.declinePasswordReset"_q,
	u"users.setSecureValueErrors"_q,
	u"messages.getBotApp"_q,
	u"messages.requestChatJoinWebView"_q,
	u"messages.editChatCreator"_q,
	u"messages.startHistoryImport"_q,
	u"messages.declineUrlAuth"_q,
	u"messages.checkUrlAuthMatchCode"_q,
};

const auto kSideEffectNames = std::array{
	u"messages.getBotCallbackAnswer"_q,
	u"messages.getInlineBotResults"_q,
	u"messages.getMessagesViews"_q,
	u"contacts.getLocated"_q,
	u"messages.startBot"_q,
};

constexpr auto kMaxBareId = (uint64(1) << 48) - 1;
constexpr auto kMaxJsonId = (uint64(1) << 53);
constexpr auto kBotApiChannelShift = int64(1000000000000);

[[nodiscard]] const base::flat_map<QString, std::vector<QString>> &Redirects() {
	using List = std::vector<QString>;
	static const auto result = base::flat_map<QString, List>{
		{ u"messages.sendMessage"_q, List{ u"send_message"_q } },
		{ u"messages.forwardMessages"_q, List{ u"forward_messages"_q } },
		{ u"messages.editMessage"_q, List{ u"edit_message"_q } },
		{ u"messages.deleteMessages"_q, List{ u"delete_messages"_q } },
		{ u"channels.deleteMessages"_q, List{
			u"delete_messages"_q,
			u"delete_chat_messages"_q,
		} },
		{ u"messages.sendReaction"_q, List{ u"react"_q } },
		{ u"messages.updatePinnedMessage"_q, List{ u"pin_message"_q } },
		{ u"messages.readHistory"_q, List{ u"mark_read"_q } },
		{ u"channels.readHistory"_q, List{ u"mark_read"_q } },
		{ u"account.updateNotifySettings"_q, List{ u"set_mute"_q } },
		{ u"folders.editPeerFolders"_q, List{ u"set_archived"_q } },
		{ u"messages.toggleDialogPin"_q, List{ u"set_pinned"_q } },
		{ u"messages.updateDialogFilter"_q, List{
			u"create_folder"_q,
			u"rename_folder"_q,
			u"set_folder_chats"_q,
			u"delete_folder"_q,
		} },
		{ u"messages.updateDialogFiltersOrder"_q, List{
			u"reorder_folders"_q,
		} },
		{ u"account.updateProfile"_q, List{ u"update_profile"_q } },
		{ u"photos.updateProfilePhoto"_q, List{ u"set_profile_photo"_q } },
		{ u"photos.uploadProfilePhoto"_q, List{ u"set_profile_photo"_q } },
		{ u"account.updateEmojiStatus"_q, List{ u"set_emoji_status"_q } },
		{ u"contacts.block"_q, List{ u"set_blocked"_q } },
		{ u"contacts.unblock"_q, List{ u"set_blocked"_q } },
		{ u"contacts.addContact"_q, List{
			u"add_contact"_q,
			u"rename_contact"_q,
		} },
		{ u"contacts.deleteContacts"_q, List{ u"delete_contacts"_q } },
		{ u"channels.inviteToChannel"_q, List{ u"add_members"_q } },
		{ u"messages.addChatUser"_q, List{ u"add_members"_q } },
		{ u"channels.editBanned"_q, List{
			u"remove_members"_q,
			u"restrict_members"_q,
			u"unban_members"_q,
		} },
		{ u"channels.editAdmin"_q, List{
			u"promote_admins"_q,
			u"demote_admins"_q,
		} },
		{ u"messages.editChatAdmin"_q, List{
			u"promote_admins"_q,
			u"demote_admins"_q,
		} },
		{ u"messages.deleteChatUser"_q, List{
			u"remove_members"_q,
			u"leave_chat"_q,
		} },
		{ u"channels.leaveChannel"_q, List{ u"leave_chat"_q } },
		{ u"channels.joinChannel"_q, List{ u"join_chat"_q } },
		{ u"messages.importChatInvite"_q, List{ u"join_chat"_q } },
		{ u"channels.createChannel"_q, List{ u"create_chat"_q } },
		{ u"messages.createChat"_q, List{ u"create_chat"_q } },
		{ u"messages.editChatTitle"_q, List{ u"edit_chat_info"_q } },
		{ u"channels.editTitle"_q, List{ u"edit_chat_info"_q } },
		{ u"messages.editChatAbout"_q, List{ u"edit_chat_info"_q } },
		{ u"messages.editChatPhoto"_q, List{ u"set_chat_photo"_q } },
		{ u"channels.editPhoto"_q, List{ u"set_chat_photo"_q } },
		{ u"messages.editChatDefaultBannedRights"_q, List{
			u"set_chat_permissions"_q,
		} },
		{ u"channels.toggleSlowMode"_q, List{ u"set_chat_permissions"_q } },
		{ u"messages.exportChatInvite"_q, List{ u"create_invite_link"_q } },
		{ u"messages.editExportedChatInvite"_q, List{
			u"edit_invite_link"_q,
			u"revoke_invite_links"_q,
		} },
		{ u"messages.hideChatJoinRequest"_q, List{
			u"answer_join_requests"_q,
		} },
		{ u"channels.deleteParticipantHistory"_q, List{
			u"delete_member_history"_q,
		} },
	};
	return result;
}

[[nodiscard]] QString RedirectText(const std::vector<QString> &tools) {
	auto result = QString();
	for (auto i = 0, count = int(tools.size()); i != count; ++i) {
		if (i) {
			result += (i + 1 == count) ? u" or "_q : u", "_q;
		}
		result += tools[i];
	}
	return result;
}

[[nodiscard]] std::optional<int64> JsonInteger(const QJsonValue &value) {
	if (value.isDouble()) {
		const auto d = value.toDouble();
		if (d != std::floor(d) || std::abs(d) > double(kMaxJsonId)) {
			return std::nullopt;
		}
		return int64(d);
	} else if (!value.isString()) {
		return std::nullopt;
	}
	const auto text = value.toString().trimmed();
	auto ok = false;
	const auto result = text.toLongLong(&ok);
	return ok ? std::make_optional(int64(result)) : std::nullopt;
}

[[nodiscard]] PeerRef TypedRef(PeerRefKind kind, int64 id) {
	return (id > 0 && uint64(id) <= kMaxBareId)
		? PeerRef{ kind, uint64(id) }
		: PeerRef();
}

[[nodiscard]] QString NamespaceOf(const QString &name) {
	const auto dot = name.indexOf(u'.');
	return (dot >= 0) ? name.left(dot) : name;
}

[[nodiscard]] QString MethodTail(const QString &name) {
	const auto dot = name.indexOf(u'.');
	return (dot >= 0) ? name.mid(dot + 1) : name;
}

[[nodiscard]] bool InList(
		const auto &list,
		const QString &name) {
	return std::find(list.begin(), list.end(), name) != list.end();
}

[[nodiscard]] std::vector<QString> QueryWords(const QString &query) {
	auto result = std::vector<QString>();
	auto run = QString();
	const auto flush = [&] {
		auto word = QString();
		for (const auto &ch : run) {
			if (ch.isUpper()) {
				if (!word.isEmpty()) {
					result.push_back(word.toLower());
					word = QString();
				}
			}
			word += ch;
		}
		if (!word.isEmpty()) {
			result.push_back(word.toLower());
		}
		run = QString();
	};
	for (const auto &ch : query) {
		if (ch.isLetterOrNumber()) {
			run += ch;
		} else if (!run.isEmpty()) {
			flush();
		}
	}
	if (!run.isEmpty()) {
		flush();
	}
	return result;
}

[[nodiscard]] QString CombinatorParams(const TlCombinator &combinator) {
	auto parts = QStringList();
	for (const auto &param : combinator.params) {
		if (param.type == u"#") {
			continue;
		} else if (param.type == u"true") {
			parts.push_back(param.name + u"?: true"_q);
		} else {
			parts.push_back(
				param.name
					+ ((param.flagParam >= 0) ? u"?: "_q : u": "_q)
					+ param.type);
		}
	}
	return parts.join(u", "_q);
}

[[nodiscard]] QString TypeLine(const TlSchema &schema, const QString &type) {
	const auto list = schema.constructorsOf(type);
	if (list.empty()) {
		return QString();
	}
	auto parts = QStringList();
	for (auto i = 0, count = int(list.size()); i != count && i != 12; ++i) {
		const auto params = CombinatorParams(*list[i]);
		parts.push_back(list[i]->name
			+ (params.isEmpty() ? QString() : u"{"_q + params + u"}"_q));
	}
	if (list.size() > 12) {
		parts.push_back(u"..."_q);
	}
	return type + u": "_q + parts.join(u" | "_q);
}

} // namespace

MethodClass ClassifyMethod(const TlSchema &schema, const QString &name) {
	const auto function = schema.function(name);
	if (!function || function->generic) {
		return MethodClass::Forbidden;
	}
	const auto namespaceName = NamespaceOf(name);
	if (InList(kForbiddenNamespaces, namespaceName)) {
		return MethodClass::Forbidden;
	}
	const auto tail = MethodTail(name);
	for (const auto &substring : kForbiddenSubstrings) {
		if (tail.contains(substring)) {
			return MethodClass::Forbidden;
		}
	}
	if (InList(kForbiddenNames, name)) {
		return MethodClass::Forbidden;
	}
	if (InList(kSideEffectNames, name)) {
		return MethodClass::Write;
	}
	if (tail.startsWith(u"get")
		|| tail.startsWith(u"search")
		|| tail.startsWith(u"check")
		|| tail.startsWith(u"resolve")) {
		return MethodClass::Read;
	}
	return MethodClass::Write;
}

std::vector<QString> MethodRedirect(const QString &name) {
	const auto &redirects = Redirects();
	const auto i = redirects.find(name);
	return (i != redirects.end()) ? i->second : std::vector<QString>();
}

PeerRef ParsePeerRef(const QJsonValue &value) {
	if (value.isString()) {
		const auto text = value.toString().trimmed();
		const auto colon = text.indexOf(u':');
		if (colon > 0) {
			const auto prefix = text.left(colon).trimmed().toLower();
			const auto id = JsonInteger(QJsonValue(text.mid(colon + 1)));
			if (!id) {
				return PeerRef();
			} else if (prefix == u"user") {
				return TypedRef(PeerRefKind::User, *id);
			} else if (prefix == u"chat") {
				return TypedRef(PeerRefKind::Chat, *id);
			} else if (prefix == u"channel") {
				return TypedRef(PeerRefKind::Channel, *id);
			}
			return PeerRef();
		}
	}
	const auto id = JsonInteger(value);
	if (!id || !*id) {
		return PeerRef();
	} else if (*id < 0) {
		return (-*id > kBotApiChannelShift)
			? TypedRef(PeerRefKind::Channel, -*id - kBotApiChannelShift)
			: TypedRef(PeerRefKind::Chat, -*id);
	} else if (uint64(*id) > kMaxBareId) {
		return PeerRef{ PeerRefKind::Peer, uint64(*id) };
	}
	return PeerRef{ PeerRefKind::Bare, uint64(*id) };
}

std::vector<MethodNamespace> MethodNamespaces(const TlSchema &schema) {
	auto counts = base::flat_map<QString, int>();
	for (const auto &function : schema.functions()) {
		if (ClassifyMethod(schema, function.name) == MethodClass::Forbidden) {
			continue;
		}
		++counts[NamespaceOf(function.name)];
	}
	auto result = std::vector<MethodNamespace>();
	for (const auto &[name, count] : counts) {
		result.push_back({ name, count });
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
		return a.name < b.name;
	});
	return result;
}

std::vector<const TlCombinator*> FindMethods(
		const TlSchema &schema,
		const QString &query,
		int limit,
		bool *partial) {
	if (partial) {
		*partial = false;
	}
	const auto words = QueryWords(query);
	if (words.empty() || limit <= 0) {
		return {};
	}
	struct Candidate {
		const TlCombinator *method = nullptr;
		int matched = 0;
		bool inName = false;
		int index = 0;
	};
	auto candidates = std::vector<Candidate>();
	auto best = 0;
	auto index = 0;
	for (const auto &function : schema.functions()) {
		const auto i = index++;
		if (ClassifyMethod(schema, function.name) == MethodClass::Forbidden) {
			continue;
		}
		auto inName = true;
		auto matched = 0;
		for (const auto &word : words) {
			if (function.name.contains(word, Qt::CaseInsensitive)) {
				++matched;
				continue;
			}
			auto found = false;
			for (const auto &param : function.params) {
				if (param.name.contains(word, Qt::CaseInsensitive)
					|| param.type.contains(word, Qt::CaseInsensitive)) {
					found = true;
					break;
				}
			}
			if (found) {
				inName = false;
				++matched;
			}
		}
		if (matched > 0) {
			best = std::max(best, matched);
			candidates.push_back({ &function, matched, inName, i });
		}
	}
	candidates.erase(
		std::remove_if(candidates.begin(), candidates.end(), [&](
				const Candidate &candidate) {
			return candidate.matched < best;
		}),
		candidates.end());
	if (partial && best < int(words.size())) {
		*partial = !candidates.empty();
	}
	std::sort(candidates.begin(), candidates.end(), [](auto &a, auto &b) {
		if (a.inName != b.inName) {
			return a.inName;
		} else if (a.method->name.size() != b.method->name.size()) {
			return a.method->name.size() < b.method->name.size();
		}
		return a.index < b.index;
	});
	auto result = std::vector<const TlCombinator*>();
	for (const auto &candidate : candidates) {
		if (int(result.size()) >= limit) {
			break;
		}
		result.push_back(candidate.method);
	}
	return result;
}

QString MethodSignature(const TlCombinator &method) {
	return method.name + u"("_q + CombinatorParams(method)
		+ u") -> "_q + method.result;
}

std::optional<QString> DescribeTl(
		const TlSchema &schema,
		const QString &name,
		QString &error) {
	if (const auto function = schema.function(name)) {
		const auto classification = ClassifyMethod(schema, name);
		if (classification == MethodClass::Forbidden) {
			error = u"%1 is never allowed."_q.arg(name);
			return std::nullopt;
		}
		const auto redirect = MethodRedirect(name);
		auto result = MethodSignature(*function) + u"\n"_q
			+ (!redirect.empty()
				? u"call_method refuses it: use %1 instead.\n"_q.arg(
					RedirectText(redirect))
				: (classification == MethodClass::Read)
				? u"reads, runs without asking.\n"_q
				: u"changes something, the user confirms it on a card.\n"_q)
			+ u"InputPeer, InputUser and InputChannel take a chat id from"
				u" other tools, an @username, or a user_id, chat_id or"
				u" channel_id from MTProto results (write user:<id>,"
				u" chat:<id> or channel:<id> when unsure).\n"
				u"Values with access_hash or file_reference and uploaded"
				u" files can't be passed.\n"_q;
		auto types = QStringList();
		const auto addType = [&](const QString &type) {
			if (TlIsBuiltinType(type)) {
				return;
			}
			const auto item = TlVectorItem(type);
			const auto target = item ? *item : type;
			if (TlIsBuiltinType(target)
				|| schema.constructorsOf(target).empty()
				|| types.contains(target)) {
				return;
			}
			types.push_back(target);
		};
		for (const auto &param : function->params) {
			addType(param.type);
		}
		addType(function->result);
		auto shown = 0;
		for (const auto &type : types) {
			if (shown == 20) {
				result += u"More types are involved:"
					u" describe a type to see them.\n"_q;
				break;
			}
			const auto line = TypeLine(schema, type);
			if (!line.isEmpty()) {
				result += line + u"\n"_q;
				++shown;
			}
		}
		return result;
	}
	if (const auto constructor = schema.constructor(name)) {
		const auto params = CombinatorParams(*constructor);
		return constructor->name
			+ (params.isEmpty() ? QString() : u"{"_q + params + u"}"_q)
			+ u"\nA constructor of "_q + constructor->result + u".\n"_q;
	}
	if (!schema.constructorsOf(name).empty()) {
		return TypeLine(schema, name) + u"\n"_q;
	}
	error = u"Unknown name: %1."_q.arg(name);
	return std::nullopt;
}

} // namespace Agent