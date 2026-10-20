/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_manager.h"
#include "agent/core/agent_mtproto.h"
#include "agent/core/agent_tl_codec.h"
#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_response.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>

namespace Agent::MtprotoTools {
namespace {

constexpr auto kDefaultFindLimit = 20;
constexpr auto kMaxFindLimit = 50;
constexpr auto kMaxDescription = 300;

const auto kGroup = u"mtproto"_q;

struct Encoded {
	QByteArray bytes;
	std::vector<not_null<PeerData*>> peers;
	std::vector<QString> usernames;
	QString error;
};

[[nodiscard]] const TlSchema *ApiSchema() {
	static const auto result = [] {
		auto file = QFile(u":/agent/api.tl"_q);
		if (!file.open(QIODevice::ReadOnly)) {
			LOG(("Agent Error: could not open the embedded api.tl."));
			return std::optional<TlSchema>();
		}
		auto error = QString();
		auto schema = ParseTlSchema(file.readAll(), &error);
		if (!schema) {
			LOG(("Agent Error: could not parse api.tl: %1").arg(error));
		}
		return schema;
	}();
	return result ? &*result : nullptr;
}

template <typename Value>
[[nodiscard]] QByteArray TlBytes(const Value &value) {
	auto buffer = mtpBuffer();
	value.write(buffer);
	return QByteArray(
		reinterpret_cast<const char*>(buffer.constData()),
		buffer.size() * sizeof(mtpPrime));
}

[[nodiscard]] QByteArray EmptyRefBytes(
		const TlSchema &schema,
		const QString &type) {
	const auto name = (type == u"InputUser"_q)
		? u"inputUserEmpty"_q
		: (type == u"InputChannel"_q)
		? u"inputChannelEmpty"_q
		: u"inputPeerEmpty"_q;
	const auto constructor = schema.constructor(name);
	const auto id = constructor ? constructor->id : uint32(0);
	auto result = QByteArray(4, Qt::Uninitialized);
	for (auto i = 0; i != 4; ++i) {
		result[i] = char((id >> (8 * i)) & 0xFF);
	}
	return result;
}

[[nodiscard]] QString RefName(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString().trimmed().left(64);
	} else if (value.isDouble()) {
		return QString::number(qint64(value.toDouble()));
	}
	return u"value"_q;
}

PeerData *ResolveRef(
		not_null<Main::Session*> session,
		const QString &type,
		const QJsonValue &value,
		QString &error) {
	const auto ref = ParsePeerRef(value);
	const auto &owner = session->data();
	const auto user = [&](uint64 id) {
		return owner.peerLoaded(peerFromUser(UserId(id)));
	};
	const auto chat = [&](uint64 id) {
		return owner.peerLoaded(peerFromChat(ChatId(id)));
	};
	const auto channel = [&](uint64 id) {
		return owner.peerLoaded(peerFromChannel(ChannelId(id)));
	};
	auto result = (PeerData*)nullptr;
	switch (ref.kind) {
	case PeerRefKind::None: result = ResolveChatRef(session, value); break;
	case PeerRefKind::Peer: result = owner.peerLoaded(PeerId(ref.id)); break;
	case PeerRefKind::User: result = user(ref.id); break;
	case PeerRefKind::Chat: result = chat(ref.id); break;
	case PeerRefKind::Channel: result = channel(ref.id); break;
	case PeerRefKind::Bare:
		if (type == u"InputUser"_q) {
			result = user(ref.id);
		} else if (type == u"InputChannel"_q) {
			result = channel(ref.id);
		} else {
			const auto candidates = {
				user(ref.id),
				chat(ref.id),
				channel(ref.id),
			};
			auto found = std::vector<PeerData*>();
			for (const auto peer : candidates) {
				if (peer) {
					found.push_back(peer);
				}
			}
			if (found.size() > 1) {
				error = u"Ambiguous id %1: write user:%1, chat:%1 or"
					" channel:%1."_q.arg(ref.id);
				return nullptr;
			}
			result = found.empty() ? nullptr : found.front();
		}
		break;
	}
	if (!result) {
		error = u"Unknown chat: %1. Use a chat id from an earlier tool"
			" result, an @username, or user:<id>, chat:<id> or"
			" channel:<id> with an id from MTProto results."_q.arg(
				RefName(value));
		return nullptr;
	} else if (type == u"InputUser"_q && !result->isUser()) {
		error = u"%1 is not a user."_q.arg(RefName(value));
		return nullptr;
	} else if (type == u"InputChannel"_q && !result->isChannel()) {
		error = u"%1 is not a channel or a supergroup."_q.arg(
			RefName(value));
		return nullptr;
	}
	return result;
}

[[nodiscard]] QByteArray PeerBytes(
		not_null<PeerData*> peer,
		const QString &type) {
	if (type == u"InputUser"_q) {
		return TlBytes(peer->asUser()->inputUser());
	} else if (type == u"InputChannel"_q) {
		return TlBytes(peer->asChannel()->inputChannel());
	}
	return TlBytes(peer->input());
}

[[nodiscard]] QString MethodName(const QJsonObject &input) {
	return input.value(u"method"_q).toString().trimmed();
}

[[nodiscard]] std::optional<QJsonObject> MethodParams(
		const QJsonObject &input,
		QString &error) {
	const auto value = input.value(u"params"_q);
	if (value.isUndefined() || value.isNull()) {
		return QJsonObject();
	} else if (value.isObject()) {
		return value.toObject();
	} else if (value.isString()) {
		auto parseError = QJsonParseError();
		const auto document = QJsonDocument::fromJson(
			value.toString().toUtf8(),
			&parseError);
		if (parseError.error == QJsonParseError::NoError
			&& document.isObject()) {
			return document.object();
		}
	}
	error = u"params must be an object with the method's parameters."_q;
	return std::nullopt;
}

[[nodiscard]] Encoded EncodeCall(
		not_null<Main::Session*> session,
		const TlSchema &schema,
		const QString &method,
		const QJsonObject &params,
		bool collectUsernames) {
	auto result = Encoded();
	const auto refs = TlRefs{ [&](
			const QString &type,
			const QJsonValue &value,
			QString &error) -> std::optional<QByteArray> {
		if (collectUsernames
			&& ParsePeerRef(value).kind == PeerRefKind::None
			&& !ResolveChatRef(session, value)) {
			const auto username = ChatRefUsername(value);
			if (!username.isEmpty()
				&& !ranges::contains(result.usernames, username)) {
				result.usernames.push_back(username);
			}
			return EmptyRefBytes(schema, type);
		}
		const auto peer = ResolveRef(session, type, value, error);
		if (!peer) {
			return std::nullopt;
		} else if (!ranges::contains(result.peers, not_null<PeerData*>(peer))) {
			result.peers.push_back(peer);
		}
		return PeerBytes(peer, type);
	} };
	if (auto bytes = EncodeTlCall(schema, method, params, refs, result.error)) {
		result.bytes = std::move(*bytes);
	}
	return result;
}

[[nodiscard]] QString CheckMethod(
		const TlSchema &schema,
		const QString &method) {
	if (method.isEmpty()) {
		return u"method is required: a Telegram API method name like"
			" messages.getHistory."_q;
	} else if (!schema.function(method)) {
		return u"Unknown method: %1. Use find_methods to look it up."_q.arg(
			method);
	} else if (ClassifyMethod(schema, method) == MethodClass::Forbidden) {
		return u"%1 is never allowed for the agent."_q.arg(method);
	}
	const auto tools = MethodRedirect(method);
	if (!tools.empty()) {
		auto names = QStringList();
		for (const auto &tool : tools) {
			names.push_back(tool);
		}
		return u"call_method doesn't run %1: use %2 instead (if it is not"
			" among your tools, load its group with load_tools)."_q.arg(
				method,
				names.join(u" or "_q));
	}
	return QString();
}

[[nodiscard]] ToolPolicy MethodPolicy(MethodClass kind) {
	return ToolPolicy{
		.risk = (kind == MethodClass::Read) ? Risk::Read : Risk::Visible,
		.chat = false,
		.outbound = true,
		.background = false,
	};
}

[[nodiscard]] std::vector<QString> MethodUsernames(
		not_null<Main::Session*> session,
		const QJsonObject &input) {
	const auto schema = ApiSchema();
	const auto method = MethodName(input);
	auto error = QString();
	const auto params = MethodParams(input, error);
	if (!schema || !params || !CheckMethod(*schema, method).isEmpty()) {
		return {};
	}
	return EncodeCall(session, *schema, method, *params, true).usernames;
}

[[nodiscard]] ToolGateRequest MethodGateRequest(
		not_null<Main::Session*> session,
		const QJsonObject &input) {
	auto result = ToolGateRequest();
	const auto schema = ApiSchema();
	if (!schema) {
		result.error = u"The Telegram API schema is not available."_q;
		return result;
	}
	const auto method = MethodName(input);
	result.error = CheckMethod(*schema, method);
	if (!result.error.isEmpty()) {
		return result;
	}
	const auto kind = ClassifyMethod(*schema, method);
	const auto description = input.value(u"description"_q).toString();
	if (kind != MethodClass::Read && description.trimmed().isEmpty()) {
		result.error = u"description is required: %1 changes something,"
			" so describe in one short line, in the user's language,"
			" what this call does."_q.arg(method);
		return result;
	}
	const auto params = MethodParams(input, result.error);
	if (!params) {
		return result;
	}
	auto encoded = EncodeCall(session, *schema, method, *params, false);
	if (!encoded.error.isEmpty()) {
		result.error = std::move(encoded.error);
		return result;
	}
	result.key = u"call_method "_q + method;
	result.policy = MethodPolicy(kind);
	result.peers = std::move(encoded.peers);
	return result;
}

[[nodiscard]] QString IndentedJson(const QJsonObject &object) {
	const auto lines = QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Indented)
	).trimmed().split('\n');
	auto result = QStringList();
	for (const auto &line : lines) {
		auto spaces = 0;
		while (spaces < line.size() && line[spaces] == ' ') {
			++spaces;
		}
		result.push_back(QString(spaces / 2, ' ') + line.mid(spaces));
	}
	return result.join('\n');
}

void ApplySpans(
		not_null<Main::Session*> session,
		const QByteArray &bytes,
		const std::vector<TlSpan> &spans) {
	const auto data = reinterpret_cast<const mtpPrime*>(bytes.constData());
	auto updates = std::vector<MTPUpdates>();
	for (const auto &span : spans) {
		if ((span.begin % 4) || (span.end % 4) || span.end > bytes.size()) {
			continue;
		}
		auto from = data + (span.begin / 4);
		const auto end = data + (span.end / 4);
		if (span.type == u"User"_q) {
			auto user = MTPUser();
			if (user.read(from, end)) {
				session->data().processUser(user);
			}
		} else if (span.type == u"Chat"_q) {
			auto chat = MTPChat();
			if (chat.read(from, end)) {
				session->data().processChat(chat);
			}
		} else if (span.type == u"Updates"_q) {
			auto value = MTPUpdates();
			if (value.read(from, end)) {
				updates.push_back(std::move(value));
			}
		}
	}
	for (const auto &value : updates) {
		session->api().applyUpdates(value);
	}
}

void FinishCall(
		not_null<PendingCall*> call,
		const QString &method,
		const mtpBuffer &reply) {
	const auto schema = ApiSchema();
	const auto function = schema ? schema->function(method) : nullptr;
	if (!function) {
		call->fail(u"The Telegram API schema is not available."_q);
		return;
	}
	const auto bytes = QByteArray(
		reinterpret_cast<const char*>(reply.constData()),
		reply.size() * sizeof(mtpPrime));
	auto error = QString();
	auto spans = std::vector<TlSpan>();
	const auto options = TlDecodeOptions{
		.spanTypes = { u"Updates"_q, u"User"_q, u"Chat"_q },
	};
	const auto value = DecodeTl(
		*schema,
		function->result,
		bytes,
		error,
		options,
		&spans);
	if (!value) {
		call->fail(u"The call was made, but its result could not be"
			" read: %1"_q.arg(error));
		return;
	}
	{
		const auto acting = call->session->agent().actingGuard();
		ApplySpans(call->session, bytes, spans);
	}
	const auto gate = call->gate;
	const auto excluded = [=](const QString &kind, int64 id) {
		const auto peer = (kind == u"user"_q)
			? peerFromUser(UserId(uint64(id)))
			: (kind == u"chat"_q)
			? peerFromChat(ChatId(uint64(id)))
			: peerFromChannel(ChannelId(uint64(id)));
		return gate && !gate->visible(peer.value);
	};
	auto json = QJsonObject();
	json.insert("method", method);
	json.insert("result", ScrubExcludedPeers(*value, excluded));
	call->done(std::move(json));
}

void SendCall(
		not_null<PendingCall*> call,
		const QString &method,
		const QByteArray &bytes) {
	Expects(!(bytes.size() % 4));

	const auto ints = int(bytes.size() / 4);
	auto request = MTP::details::SerializedRequest::Prepare(ints);
	const auto data = reinterpret_cast<const mtpPrime*>(bytes.constData());
	for (auto i = 0; i != ints; ++i) {
		request->push_back(data[i]);
	}
	const auto weak = base::make_weak(call.get());
	const auto instance = &call->session->mtp();
	const auto requestId = MTP::details::GetNextRequestId();
	instance->sendSerialized(
		requestId,
		std::move(request),
		MTP::ResponseHandler{
			[=](const MTP::Response &response) {
				if (const auto strong = weak.get()) {
					FinishCall(strong, method, response.reply);
				}
				return true;
			},
			[=](const MTP::Error &error, const MTP::Response &) {
				if (MTP::IsDefaultHandledError(error)
					&& !MTP::IsFloodError(error)) {
					return false;
				}
				if (const auto strong = weak.get()) {
					strong->fail(MtpErrorText(error));
				}
				return true;
			},
		},
		0,
		0,
		0);
	call->lifetime.add([=] {
		instance->cancel(requestId);
	});
}

void RunCallMethod(not_null<PendingCall*> call) {
	const auto schema = ApiSchema();
	const auto method = MethodName(call->input);
	auto error = schema
		? CheckMethod(*schema, method)
		: u"The Telegram API schema is not available."_q;
	const auto params = MethodParams(call->input, error);
	if (!error.isEmpty() || !params) {
		call->fail(error);
		return;
	}
	auto encoded = EncodeCall(
		call->session,
		*schema,
		method,
		*params,
		false);
	if (!encoded.error.isEmpty()) {
		call->fail(encoded.error);
		return;
	}
	for (const auto peer : encoded.peers) {
		if (!call->gate || !call->gate->visible(peer->id.value)) {
			call->fail(u"The user hid this chat from the agent. Tell the"
				" user you can't see it and don't try to reach it another"
				" way."_q);
			return;
		}
	}
	const auto bytes = std::move(encoded.bytes);
	if (ClassifyMethod(*schema, method) == MethodClass::Read) {
		SendCall(call, method, bytes);
		return;
	}
	auto card = Card{ .kind = CardKind::Method };
	card.text = call->input.value(
		u"description"_q
	).toString().trimmed().left(kMaxDescription);
	card.detail = method;
	card.proposedText = IndentedJson(*params);
	if (!encoded.peers.empty()) {
		auto titles = QStringList();
		for (const auto peer : encoded.peers) {
			titles.push_back(PeerTitle(peer));
		}
		card.rows.push_back({
			.label = tr::lng_agent_card_method_chats(tr::now),
			.value = titles.join(u", "_q),
		});
	}
	call->confirm(std::move(card), [=](Card decided) {
		SendCall(call, method, bytes);
	});
}

void RunFindMethods(not_null<PendingCall*> call) {
	const auto schema = ApiSchema();
	if (!schema) {
		call->fail(u"The Telegram API schema is not available."_q);
		return;
	}
	const auto query = call->input.value(u"query"_q).toString().trimmed();
	auto json = QJsonObject();
	if (query.isEmpty()) {
		auto namespaces = QJsonArray();
		for (const auto &entry : MethodNamespaces(*schema)) {
			auto object = QJsonObject();
			object.insert("name", entry.name);
			object.insert("methods", entry.count);
			namespaces.push_back(object);
		}
		json.insert("namespaces", namespaces);
		json.insert("hint", u"Search with words from a method name or its"
			" parameters, like \"slow mode\" or \"getHistory\"."_q);
		call->done(std::move(json));
		return;
	}
	const auto value = call->input.value(u"limit"_q);
	const auto limit = value.isDouble()
		? std::clamp(int(value.toDouble()), 1, kMaxFindLimit)
		: kDefaultFindLimit;
	auto methods = QJsonArray();
	auto partial = false;
	for (const auto method : FindMethods(*schema, query, limit, &partial)) {
		auto object = QJsonObject();
		object.insert("name", method->name);
		object.insert("signature", MethodSignature(*method));
		const auto tools = MethodRedirect(method->name);
		if (!tools.empty()) {
			auto names = QStringList();
			for (const auto &tool : tools) {
				names.push_back(tool);
			}
			object.insert("use", names.join(u" or "_q));
		} else {
			object.insert(
				"kind",
				(ClassifyMethod(*schema, method->name) == MethodClass::Read)
					? u"read"_q
					: u"write"_q);
		}
		methods.push_back(object);
	}
	json.insert("methods", methods);
	if (methods.isEmpty()) {
		json.insert("hint", u"Nothing matched. Try fewer or other words."_q);
	} else if (partial) {
		json.insert("hint", u"No method matched every word; these match"
			" the most words."_q);
	}
	call->done(std::move(json));
}

void RunDescribeMethod(not_null<PendingCall*> call) {
	const auto schema = ApiSchema();
	if (!schema) {
		call->fail(u"The Telegram API schema is not available."_q);
		return;
	}
	const auto name = call->input.value(u"name"_q).toString().trimmed();
	auto error = QString();
	const auto description = DescribeTl(*schema, name, error);
	if (!description) {
		call->fail(error);
		return;
	}
	auto json = QJsonObject();
	json.insert("name", name);
	json.insert("schema", *description);
	call->done(std::move(json));
}

} // namespace

std::vector<ToolEntry> Entries() {
	const auto policy = ToolPolicy{
		.risk = Risk::Read,
		.chat = false,
		.background = false,
	};
	auto result = std::vector<ToolEntry>();
	auto find = QJsonObject();
	find.insert("query", StringProperty(
		u"Words to look for in method names, parameter names and types."
		" Leave it out to list the namespaces."_q));
	find.insert("limit", IntProperty(
		1,
		kMaxFindLimit,
		u"How many methods to return, 20 by default."_q));
	result.push_back({
		.name = u"find_methods"_q,
		.description = u"Search the raw Telegram API (MTProto) for a"
			" method that no other tool covers. Returns names, signatures"
			" and whether a method only reads (\"read\") or changes"
			" something (\"write\")."_q,
		.inputSchema = ToolSchema(std::move(find), QJsonArray()),
		.policy = policy,
		.group = kGroup,
		.untrusted = false,
		.run = RunFindMethods,
	});
	auto describe = QJsonObject();
	describe.insert("name", StringProperty(
		u"A method, constructor or type name, like messages.getHistory"
		" or InputPeer."_q));
	result.push_back({
		.name = u"describe_method"_q,
		.description = u"Show the parameters of a Telegram API method, or"
			" the constructors of a type, before calling it."_q,
		.inputSchema = ToolSchema(
			std::move(describe),
			QJsonArray{ QJsonValue(u"name"_q) }),
		.policy = policy,
		.group = kGroup,
		.untrusted = false,
		.run = RunDescribeMethod,
	});
	auto params = QJsonObject();
	params.insert("type", QJsonValue(u"object"_q));
	params.insert("description", QJsonValue(u"The method's parameters by"
		" name, as describe_method shows them ({} when it has none). An"
		" object value is"
		" {\"_\": \"constructorName\", ...its parameters}; flags are"
		" computed; long values may be strings; chats and users are ids"
		" or @usernames, never objects."_q));
	params.insert("additionalProperties", QJsonValue(true));
	auto callSchema = QJsonObject();
	callSchema.insert("method", StringProperty(
		u"The method name, like messages.getHistory."_q));
	callSchema.insert("params", params);
	callSchema.insert("description", StringProperty(
		u"One short line in the user's language saying what this call"
		" does; for a method that changes something the user sees it on"
		" the confirmation card."_q));
	result.push_back({
		.name = u"call_method"_q,
		.description = u"Call a Telegram API method that no other tool"
			" covers. Look it up with describe_method first. Reads run at"
			" once; a method that changes something waits for the user's"
			" confirmation on a card. Methods that other tools cover are"
			" refused with the tool to use."_q,
		.inputSchema = ToolSchema(
			std::move(callSchema),
			QJsonArray{
				QJsonValue(u"method"_q),
				QJsonValue(u"params"_q),
				QJsonValue(u"description"_q),
			}),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.chat = false,
			.outbound = true,
			.background = false,
		},
		.group = kGroup,
		.usernames = MethodUsernames,
		.gateRequest = MethodGateRequest,
		.run = RunCallMethod,
	});
	return result;
}

} // namespace Agent::MtprotoTools
