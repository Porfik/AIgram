/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_mtproto.h"

namespace Agent::Tests {
namespace {

struct Parsed {
	std::optional<TlSchema> schema;
	QString error;
};

[[nodiscard]] Parsed Parse(const QByteArray &text) {
	auto error = QString();
	auto schema = ParseTlSchema(text, &error);
	return { std::move(schema), error };
}

[[nodiscard]] std::optional<TlSchema> RealSchema() {
	const auto parsed = Parse(ReadApiTl());
	if (!parsed.schema) {
		return std::nullopt;
	}
	return parsed.schema;
}

} // namespace

std::vector<Check> MtprotoChecks() {
	return {
		{ u"mtproto_classify_reads"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto is = [&](const QString &name, MethodClass expected) {
				return ClassifyMethod(*schema, name) == expected;
			};
			const auto reads = {
				u"messages.getHistory"_q,
				u"messages.searchGlobal"_q,
				u"messages.checkChatInvite"_q,
				u"account.checkUsername"_q,
				u"contacts.resolveUsername"_q,
				u"users.getUsers"_q,
			};
			for (const auto &name : reads) {
				if (!is(name, MethodClass::Read)) {
					return u"reads must be classified Read: "_q + name
						+ u" -> "_q
						+ QString::number(int(ClassifyMethod(*schema, name)));
				}
			}
			return QString();
		} },
		{ u"mtproto_classify_writes"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto is = [&](const QString &name, MethodClass expected) {
				return ClassifyMethod(*schema, name) == expected;
			};
			if (!is(u"messages.sendMessage"_q, MethodClass::Write)
				|| !is(u"messages.startBot"_q, MethodClass::Write)
				|| !is(u"stories.startLive"_q, MethodClass::Write)
				|| !is(u"messages.getBotCallbackAnswer"_q, MethodClass::Write)
				|| !is(u"messages.getInlineBotResults"_q, MethodClass::Write)
				|| !is(u"messages.getMessagesViews"_q, MethodClass::Write)
				|| !is(u"contacts.getLocated"_q, MethodClass::Write)
				|| !is(u"messages.getWebPagePreview"_q, MethodClass::Write)
				|| !is(u"messages.getWebPage"_q, MethodClass::Write)
				|| !is(u"ephemeral.getCallbackAnswer"_q, MethodClass::Write)
				|| !is(
					u"phone.getGroupCallStreamRtmpUrl"_q,
					MethodClass::Write)
				|| !is(u"bots.checkDownloadFileParams"_q, MethodClass::Write)) {
				return u"writes must be classified Write"_q;
			}
			return QString();
		} },
		{ u"mtproto_classify_forbidden"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto is = [&](const QString &name) {
				return ClassifyMethod(*schema, name)
					== MethodClass::Forbidden;
			};
			if (!is(u"account.getPassword"_q)
				|| !is(u"messages.sendPaidReaction"_q)
				|| !is(u"messages.sendWebViewData"_q)
				|| !is(u"wallet.getState"_q)
				|| !is(u"auth.checkPassword"_q)
				|| !is(u"account.deleteAccount"_q)
				|| !is(u"account.setAccountTTL"_q)
				|| !is(u"account.sendChangePhoneCode"_q)
				|| !is(u"account.verifyEmail"_q)
				|| !is(u"messages.editChatCreator"_q)
				|| !is(u"messages.startHistoryImport"_q)
				|| !is(u"messages.getBotApp"_q)
				|| !is(u"invokeWithLayer"_q)
				|| !is(u"unknown.method"_q)) {
				return u"forbidden methods must be classified Forbidden"_q;
			}
			return QString();
		} },
		{ u"mtproto_namespaces"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto list = MethodNamespaces(*schema);
			if (list.empty()) {
				return u"namespaces must not be empty"_q;
			}
			auto total = 0;
			for (auto i = 0, count = int(list.size()); i != count; ++i) {
				total += list[i].count;
				if (i && list[i - 1].name >= list[i].name) {
					return u"namespaces must be sorted by name"_q;
				} else if (list[i].name == u"wallet"
					|| list[i].name == u"auth") {
					return u"forbidden namespaces must be absent"_q;
				}
			}
			auto nonForbidden = 0;
			for (const auto &function : schema->functions()) {
				if (ClassifyMethod(*schema, function.name)
					!= MethodClass::Forbidden) {
					++nonForbidden;
				}
			}
			if (total != nonForbidden) {
				return u"counts must cover every non-forbidden method"_q;
			}
			return QString();
		} },
		{ u"mtproto_find_methods"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto found = FindMethods(*schema, u"get history"_q, 10);
			if (found.empty() || found.front()->name != u"messages.getHistory"_q) {
				return u"messages.getHistory must be found first"_q;
			}
			const auto camel = FindMethods(*schema, u"getHistory"_q, 10);
			if (camel.empty() || camel.front()->name != u"messages.getHistory"_q) {
				return u"a camelCase query must split at capitals"_q;
			}
			if (!FindMethods(*schema, QString(), 10).empty()) {
				return u"an empty query must find nothing"_q;
			}
			if (FindMethods(*schema, u"get history"_q, 2).size() > 2) {
				return u"the limit must hold"_q;
			}
			auto partial = true;
			if (FindMethods(*schema, u"get history"_q, 10, &partial).empty()
				|| partial) {
				return u"full matches must not be partial"_q;
			}
			const auto some = FindMethods(
				*schema,
				u"full channel info"_q,
				5,
				&partial);
			if (!partial || some.empty()
				|| some.front()->name != u"channels.getFullChannel"_q) {
				return u"the best partial match must come first: "_q
					+ (some.empty() ? QString() : some.front()->name);
			}
			if (!FindMethods(*schema, u"qqqzzz"_q, 5, &partial).empty()
				|| partial) {
				return u"no match must give nothing"_q;
			}
			const auto byParam = FindMethods(*schema, u"random_id"_q, 10);
			if (byParam.empty()) {
				return u"a parameter name must be searched too"_q;
			}
			const auto startBot = std::find_if(
				byParam.begin(),
				byParam.end(),
				[](const TlCombinator *method) {
					return method->name == u"messages.startBot"_q;
				});
			if (startBot == byParam.end()) {
				return u"messages.startBot must be found by random_id"_q;
			}
			for (const auto &method : byParam) {
				if (method->name == u"auth.importAuthorization"_q) {
					return u"forbidden methods must not be found"_q;
				}
			}
			return QString();
		} },
		{ u"mtproto_signature"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto history = schema->function(u"messages.getHistory"_q);
			if (!history) {
				return u"messages.getHistory not found"_q;
			}
			const auto signature = MethodSignature(*history);
			if (signature
				!= u"messages.getHistory(peer: InputPeer, offset_id: int, offset_date: int, add_offset: int, limit: int, max_id: int, min_id: int, hash: long) -> messages.Messages"_q) {
				return u"unexpected signature: "_q + signature;
			}
			const auto send = schema->function(u"messages.sendMessage"_q);
			if (!send) {
				return u"messages.sendMessage not found"_q;
			}
			const auto sendSignature = MethodSignature(*send);
			if (!sendSignature.contains(u"no_webpage?: true"_q)
				|| !sendSignature.contains(u"reply_to?: InputReplyTo"_q)
				|| !sendSignature.contains(u"message: string"_q)
				|| sendSignature.contains(u"flags"_q)) {
				return u"unexpected sendMessage signature: "_q
					+ sendSignature;
			}
			return QString();
		} },
		{ u"mtproto_describe_function"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			auto error = QString();
			const auto result = DescribeTl(
				*schema,
				u"messages.getHistory"_q,
				error);
			if (!result) {
				return error;
			}
			const auto read = result;
			if (!result->contains(u"reads, runs without asking."_q)
				|| !result->contains(u"InputPeer, InputUser and InputChannel take a chat id from other tools, an @username"_q)
				|| !result->contains(u"uploaded files can't be passed."_q)
				|| !result->contains(u"messages.Messages: "_q)
				|| !result->contains(u"messages.messagesSlice{"_q)) {
				return u"unexpected description:\n"_q + *result;
			}
			const auto write = DescribeTl(
				*schema,
				u"messages.setTyping"_q,
				error);
			if (!write || !write->contains(
				u"changes something, the user confirms it on a card."_q)) {
				return u"a write must say it changes something"_q;
			} else if (read->contains(u"changes something"_q)) {
				return u"a read must not say it changes something"_q;
			}
			const auto redirected = DescribeTl(
				*schema,
				u"channels.editBanned"_q,
				error);
			if (!redirected || !redirected->contains(
				u"call_method refuses it: use remove_members,"
				u" restrict_members or unban_members instead."_q)) {
				return u"a redirected method must name its tools:\n"_q
					+ (redirected ? *redirected : error);
			} else if (redirected->contains(u"changes something"_q)) {
				return u"a redirected method must not offer a card"_q;
			}
			return QString();
		} },
		{ u"mtproto_describe_constructor_and_type"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			auto error = QString();
			auto result = DescribeTl(*schema, u"inputPeerUser"_q, error);
			if (!result) {
				return error;
			} else if (!result->contains(
				u"inputPeerUser{user_id: long, access_hash: long}"_q)
				|| !result->contains(u"A constructor of InputPeer."_q)) {
				return u"unexpected constructor description:\n"_q + *result;
			}
			result = DescribeTl(*schema, u"InputPeer"_q, error);
			if (!result) {
				return error;
			} else if (!result->startsWith(u"InputPeer: "_q)
				|| !result->contains(u"inputPeerEmpty"_q)
				|| result->contains(u"A constructor of"_q)) {
				return u"unexpected type description:\n"_q + *result;
			}
			return QString();
		} },
		{ u"mtproto_describe_errors"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			auto error = QString();
			if (DescribeTl(*schema, u"no.such"_q, error)) {
				return u"an unknown name must fail"_q;
			} else if (!error.contains(u"Unknown name"_q)) {
				return u"unexpected error: "_q + error;
			}
			error = QString();
			if (DescribeTl(*schema, u"account.getPassword"_q, error)) {
				return u"a forbidden method must fail"_q;
			} else if (!error.contains(u"never allowed"_q)) {
				return u"unexpected error: "_q + error;
			}
			return QString();
		} },
		{ u"mtproto_describe_caps"_q, [] {
			auto text = QString(u"---types---\n"_q);
			for (auto i = 0; i != 21; ++i) {
				text += u"t%1#%2 = T%3;\n"_q.arg(
					QString::number(i),
					QString::number(0x0a000001 + i, 16),
					QString::number(i));
			}
			for (auto i = 0; i != 13; ++i) {
				text += u"c%1#%2 x:int = C;\n"_q.arg(
					QString::number(i),
					QString::number(0x0b000001 + i, 16));
			}
			text += u"---functions---\ncap.call#0c000001 c:C "_q;
			for (auto i = 0; i != 21; ++i) {
				text += u"p%1:T%2 "_q.arg(
					QString::number(i),
					QString::number(i));
			}
			text += u"= Ok;\n"_q;
			const auto parsed = Parse(text.toUtf8());
			if (!parsed.schema) {
				return u"a caps schema must parse: "_q + parsed.error;
			}
			auto error = QString();
			auto result = DescribeTl(*parsed.schema, u"C"_q, error);
			if (!result) {
				return error;
			} else if (!result->contains(u"c11{x: int} | ..."_q)) {
				return u"a type line must stop at 12 constructors:\n"_q
					+ *result;
			}
			result = DescribeTl(*parsed.schema, u"cap.call"_q, error);
			if (!result) {
				return error;
			} else if (!result->contains(
				u"More types are involved: describe a type to see them."_q)) {
				return u"a description must stop at 20 types:\n"_q + *result;
			} else if (result->contains(u"T19: "_q)) {
				return u"only 20 types must be shown:\n"_q + *result;
			} else if (!result->contains(u"C: c0{x: int}"_q)) {
				return u"the first type must be shown:\n"_q + *result;
			}
			return QString();
		} },
		{ u"mtproto_redirects"_q, [] {
			const auto schema = RealSchema();
			if (!schema) {
				return u"api.tl must parse"_q;
			}
			const auto expect = [&](
					const QString &name,
					std::vector<QString> tools) {
				return MethodRedirect(name) == tools;
			};
			if (!expect(u"messages.sendMessage"_q, { u"send_message"_q })
				|| !expect(u"folders.editPeerFolders"_q,
					{ u"set_archived"_q })
				|| !expect(u"contacts.addContact"_q,
					{ u"add_contact"_q, u"rename_contact"_q })) {
				return u"known methods must redirect to their tools"_q;
			} else if (!MethodRedirect(u"messages.getHistory"_q).empty()
				|| !MethodRedirect(u"messages.setTyping"_q).empty()
				|| !MethodRedirect(u"messages.sendMessages"_q).empty()) {
				return u"other methods must not redirect"_q;
			}
			for (const auto &name : {
					u"messages.sendMessage"_q,
					u"channels.editBanned"_q,
					u"photos.uploadProfilePhoto"_q,
					u"messages.updateDialogFiltersOrder"_q,
					u"channels.deleteParticipantHistory"_q }) {
				if (!schema->function(name)) {
					return u"a redirected method must exist: "_q + name;
				}
			}
			return QString();
		} },
		{ u"mtproto_peer_refs"_q, [] {
			const auto is = [](
					const QJsonValue &value,
					PeerRefKind kind,
					uint64 id) {
				const auto ref = ParsePeerRef(value);
				return (ref.kind == kind) && (ref.id == id);
			};
			const auto channelPeer = (uint64(2) << 48) | 777;
			if (!is(QJsonValue(12345), PeerRefKind::Bare, 12345)
				|| !is(QJsonValue(u" 12345 "_q), PeerRefKind::Bare, 12345)
				|| !is(
					QJsonValue(double(channelPeer)),
					PeerRefKind::Peer,
					channelPeer)
				|| !is(
					QJsonValue(QString::number(channelPeer)),
					PeerRefKind::Peer,
					channelPeer)) {
				return u"numbers must be bare ids or peer values"_q;
			} else if (!is(QJsonValue(u"user:5"_q), PeerRefKind::User, 5)
				|| !is(QJsonValue(u"Chat: 6"_q), PeerRefKind::Chat, 6)
				|| !is(
					QJsonValue(u"channel:1234567890"_q),
					PeerRefKind::Channel,
					1234567890)) {
				return u"typed ids must keep their kind"_q;
			} else if (!is(
					QJsonValue(u"-1001234567890"_q),
					PeerRefKind::Channel,
					1234567890)
				|| !is(
					QJsonValue(-1001234567890.),
					PeerRefKind::Channel,
					1234567890)
				|| !is(QJsonValue(-1005), PeerRefKind::Chat, 1005)) {
				return u"Bot API ids must map to chats and channels"_q;
			} else if (!is(QJsonValue(u"@durov"_q), PeerRefKind::None, 0)
				|| !is(QJsonValue(0), PeerRefKind::None, 0)
				|| !is(QJsonValue(1.5), PeerRefKind::None, 0)
				|| !is(QJsonValue(u"user:"_q), PeerRefKind::None, 0)
				|| !is(QJsonValue(u"group:5"_q), PeerRefKind::None, 0)
				|| !is(
					QJsonValue(QString::number(channelPeer).prepend(
						u"channel:"_q)),
					PeerRefKind::None,
					0)
				|| !is(QJsonValue(true), PeerRefKind::None, 0)) {
				return u"other values must not be ids"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests