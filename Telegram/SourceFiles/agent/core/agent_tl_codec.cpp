/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_tl_codec.h"

#include <QtCore/QJsonArray>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Agent {
namespace {

constexpr auto kVectorId = 0x1cb5c415u;
constexpr auto kBoolTrueId = 0x997275b5u;
constexpr auto kBoolFalseId = 0xbc799737u;
constexpr auto kIntMin = -2147483648.0;
constexpr auto kIntMax = 2147483647.0;
constexpr auto kLongBound = 9.223372036854775e18;

void WriteUInt32(QByteArray &out, uint32 value) {
	for (auto i = 0; i != 4; ++i) {
		out.append(char((value >> (8 * i)) & 0xffU));
	}
}

void WriteInt32(QByteArray &out, int32 value) {
	WriteUInt32(out, uint32(value));
}

void WriteInt64(QByteArray &out, int64 value) {
	const auto raw = uint64(value);
	for (auto i = 0; i != 8; ++i) {
		out.append(char((raw >> (8 * i)) & 0xffU));
	}
}

void WriteBytes(QByteArray &out, const QByteArray &data) {
	const auto size = data.size();
	const auto prefix = (size < 254) ? 1 : 4;
	if (size < 254) {
		out.append(char(size));
	} else {
		out.append(char(0xfe));
		out.append(char(size & 0xff));
		out.append(char((size >> 8) & 0xff));
		out.append(char((size >> 16) & 0xff));
	}
	out.append(data);
	const auto pad = (4 - ((prefix + size) % 4)) % 4;
	out.append(QByteArray(pad, char(0)));
}

struct Encoder {
	const TlSchema &schema;
	const TlRefs &refs;
	QString &error;

	[[nodiscard]] bool fail(const QString &path, const QString &message) {
		error = path + u": "_q + message;
		return false;
	}
};

struct BareIdList {
	QString owner;
	QString param;
	QString kind;
};

const auto kBareIdLists = std::array{
	BareIdList{ u"messages.getChats"_q, u"id"_q, u"chat"_q },
	BareIdList{
		u"inputPrivacyValueAllowChatParticipants"_q,
		u"chats"_q,
		u"chat"_q,
	},
	BareIdList{
		u"inputPrivacyValueDisallowChatParticipants"_q,
		u"chats"_q,
		u"chat"_q,
	},
	BareIdList{ u"contacts.editCloseFriends"_q, u"id"_q, u"user"_q },
	BareIdList{
		u"phone.deleteConferenceCallParticipants"_q,
		u"ids"_q,
		u"user"_q,
	},
};

[[nodiscard]] QString BareIdKind(
		const TlCombinator &combinator,
		const TlParam &param) {
	if (param.type == u"long" && param.name == u"chat_id") {
		return u"chat"_q;
	} else if (param.type != u"Vector<long>") {
		return QString();
	}
	for (const auto &list : kBareIdLists) {
		if (list.owner == combinator.name && list.param == param.name) {
			return list.kind;
		}
	}
	return QString();
}

[[nodiscard]] std::optional<int64> BareIdValue(const QJsonValue &value) {
	if (value.isDouble()) {
		const auto d = value.toDouble();
		return (d == std::floor(d) && std::abs(d) <= kLongBound)
			? std::make_optional(int64(d))
			: std::nullopt;
	} else if (value.isString()) {
		auto ok = false;
		const auto result = value.toString().toLongLong(&ok);
		return ok ? std::make_optional(int64(result)) : std::nullopt;
	}
	return std::nullopt;
}

bool CheckBareIds(
		Encoder &e,
		const TlCombinator &combinator,
		const TlParam &param,
		const QJsonValue &value,
		const QString &path) {
	const auto kind = e.refs.bare ? BareIdKind(combinator, param) : QString();
	if (kind.isEmpty()) {
		return true;
	}
	const auto check = [&](const QJsonValue &item, const QString &where) {
		const auto id = BareIdValue(item);
		if (!id) {
			return true;
		}
		auto error = QString();
		if (!e.refs.bare(kind, *id, error)) {
			return e.fail(where, error);
		}
		return true;
	};
	if (!value.isArray()) {
		return check(value, path);
	}
	auto index = 0;
	for (const auto &item : value.toArray()) {
		const auto where = path + u"["_q + QString::number(index++) + u"]"_q;
		if (!check(item, where)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool IsRefType(const QString &type) {
	return (type == u"InputPeer")
		|| (type == u"InputUser")
		|| (type == u"InputChannel");
}

bool EncodeValue(
	Encoder &e,
	QByteArray &out,
	const QString &type,
	const QJsonValue &value,
	const QString &path);

bool EncodeCombinator(
	Encoder &e,
	QByteArray &out,
	const TlCombinator &combinator,
	const QJsonObject &source,
	const QString &path,
	bool boxed);

bool EncodeBuiltin(
		Encoder &e,
		QByteArray &out,
		const QString &type,
		const QJsonValue &value,
		const QString &path) {
	if (type == u"int") {
		if (!value.isDouble()) {
			return e.fail(path, u"expected a number"_q);
		}
		const auto d = value.toDouble();
		if (d != std::floor(d)) {
			return e.fail(path, u"expected an integer"_q);
		} else if (d < kIntMin || d > kIntMax) {
			return e.fail(path, u"a number outside the int range"_q);
		}
		WriteInt32(out, int32(d));
		return true;
	} else if (type == u"long") {
		auto result = int64(0);
		if (value.isDouble()) {
			const auto d = value.toDouble();
			if (d != std::floor(d)) {
				return e.fail(path, u"expected an integer"_q);
			} else if (d < -kLongBound || d > kLongBound) {
				return e.fail(path, u"a number outside the long range"_q);
			}
			result = int64(d);
		} else if (value.isString()) {
			auto ok = false;
			result = value.toString().toLongLong(&ok);
			if (!ok) {
				return e.fail(path, u"expected a decimal number"_q);
			}
		} else {
			return e.fail(path, u"expected a number"_q);
		}
		WriteInt64(out, result);
		return true;
	} else if (type == u"double") {
		if (!value.isDouble()) {
			return e.fail(path, u"expected a number"_q);
		}
		const auto d = value.toDouble();
		const auto raw = QByteArray(
			reinterpret_cast<const char*>(&d),
			sizeof(double));
		out.append(raw);
		return true;
	} else if (type == u"string") {
		if (!value.isString()) {
			return e.fail(path, u"expected a string"_q);
		}
		WriteBytes(out, value.toString().toUtf8());
		return true;
	} else if (type == u"bytes") {
		if (!value.isString()) {
			return e.fail(path, u"expected a base64 string"_q);
		}
		const auto decoded = QByteArray::fromBase64Encoding(
			value.toString().toLatin1(),
			QByteArray::Base64Encoding
				| QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded) {
			return e.fail(path, u"invalid base64"_q);
		}
		WriteBytes(out, decoded.decoded);
		return true;
	} else if (type == u"Bool") {
		if (!value.isBool()) {
			return e.fail(path, u"expected true or false"_q);
		}
		WriteUInt32(out, value.toBool() ? kBoolTrueId : kBoolFalseId);
		return true;
	} else if (type == u"true" || type == u"#") {
		return true;
	} else if (type == u"int128" || type == u"int256") {
		if (!value.isString()) {
			return e.fail(path, u"expected a hex string"_q);
		}
		const auto text = value.toString();
		const auto chars = (type == u"int128") ? 32 : 64;
		if (text.size() != chars) {
			return e.fail(path, u"expected %1 hex characters"_q.arg(chars));
		}
		for (const auto &ch : text) {
			const auto c = ch.toLower().unicode();
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
				return e.fail(path, u"invalid hex"_q);
			}
		}
		out += QByteArray::fromHex(text.toLatin1());
		return true;
	}
	return e.fail(path, u"unknown type %1"_q.arg(type));
}

bool EncodeVector(
		Encoder &e,
		QByteArray &out,
		const QString &type,
		const QJsonValue &value,
		const QString &path) {
	const auto item = TlVectorItem(type);
	if (!item) {
		return e.fail(path, u"unknown type %1"_q.arg(type));
	} else if (!value.isArray()) {
		return e.fail(path, u"expected an array"_q);
	}
	const auto array = value.toArray();
	if (type.startsWith(u"Vector<")) {
		WriteUInt32(out, kVectorId);
	}
	WriteInt32(out, array.size());
	auto index = 0;
	for (const auto &element : array) {
		if (!EncodeValue(
			e,
			out,
			*item,
			element,
			path + u"["_q + QString::number(index) + u"]"_q)) {
			return false;
		}
		++index;
	}
	return true;
}

[[nodiscard]] bool Buildable(
		Encoder &e,
		const TlCombinator &combinator,
		const QString &path) {
	for (const auto &param : combinator.params) {
		if (param.name == u"access_hash" || param.name == u"file_reference") {
			return e.fail(
				path,
				u"%1 needs an %2 the agent never sees; files, media and"
				u" such values can't be passed here."_q.arg(
					combinator.name,
					param.name));
		}
	}
	return true;
}

bool EncodeObject(
		Encoder &e,
		QByteArray &out,
		const QString &type,
		const QJsonValue &value,
		const QString &path) {
	if (type == u"InputFile"
		|| type == u"InputEncryptedFile"
		|| type == u"InputSecureFile") {
		return e.fail(
			path,
			u"uploading files is not supported here; send files with"
			u" send_files, set_chat_photo or set_profile_photo."_q);
	} else if (!value.isObject()) {
		return e.fail(path, u"expected an object"_q);
	}
	const auto object = value.toObject();
	const auto name = object.value(u"_"_q).toString();
	if (name.isEmpty()) {
		const auto list = e.schema.constructorsOf(type);
		if (list.empty()) {
			return e.fail(path, u"unknown type %1"_q.arg(type));
		} else if (list.size() > 1) {
			return e.fail(
				path,
				u"pass \"_\" naming a constructor of %1"_q.arg(type));
		} else if (!Buildable(e, *list.front(), path)) {
			return false;
		}
		return EncodeCombinator(
			e, out, *list.front(), object, path, !TlIsBareType(type));
	}
	const auto combinator = e.schema.constructor(name);
	if (!combinator) {
		return e.fail(path, u"unknown constructor %1"_q.arg(name));
	} else if (combinator->result != type) {
		return e.fail(
			path,
			u"%1 is not of type %2"_q.arg(name).arg(type));
	} else if (!Buildable(e, *combinator, path)) {
		return false;
	}
	return EncodeCombinator(
		e,
		out,
		*combinator,
		object,
		path,
		!TlIsBareType(type));
}

bool EncodeRef(
		Encoder &e,
		QByteArray &out,
		const QString &type,
		const QJsonValue &value,
		const QString &path) {
	if (value.isString() || value.isDouble()) {
		if (!e.refs.input) {
			return e.fail(path, u"cannot resolve %1"_q.arg(type));
		}
		auto refError = QString();
		auto bytes = e.refs.input(type, value, refError);
		if (!bytes) {
			return e.fail(
				path,
				refError.isEmpty() ? u"cannot resolve %1"_q.arg(type)
					: refError);
		}
		out += std::move(*bytes);
		return true;
	} else if (value.isObject()) {
		static const auto kAllowed = base::flat_map<QString, QString>{
			{ u"InputPeer"_q, u"inputPeerEmpty,inputPeerSelf"_q },
			{ u"InputUser"_q, u"inputUserEmpty,inputUserSelf"_q },
			{ u"InputChannel"_q, u"inputChannelEmpty"_q },
		};
		const auto name = value.toObject().value(u"_"_q).toString();
		const auto i = kAllowed.find(type);
		const auto allowed = (i != kAllowed.end()) ? i->second : QString();
		if (!allowed.split(u',').contains(name)) {
			return e.fail(
				path,
				u"Pass chats as ids or @usernames, not objects."_q);
		}
		return EncodeObject(e, out, type, value, path);
	}
	return e.fail(path, u"expected a chat id or @username"_q);
}

bool EncodeValue(
		Encoder &e,
		QByteArray &out,
		const QString &type,
		const QJsonValue &value,
		const QString &path) {
	if (IsRefType(type)) {
		return EncodeRef(e, out, type, value, path);
	} else if (TlIsBuiltinType(type)) {
		return EncodeBuiltin(e, out, type, value, path);
	} else if (TlVectorItem(type)) {
		return EncodeVector(e, out, type, value, path);
	}
	return EncodeObject(e, out, type, value, path);
}

bool EncodeCombinator(
		Encoder &e,
		QByteArray &out,
		const TlCombinator &combinator,
		const QJsonObject &source,
		const QString &path,
		bool boxed) {
	const auto &params = combinator.params;
	for (const auto &key : source.keys()) {
		if (key == u"_") {
			continue;
		}
		auto known = false;
		for (const auto &param : params) {
			if (param.type != u"#" && param.name == key) {
				known = true;
				break;
			}
		}
		if (!known) {
			return e.fail(path + u"."_q + key, u"unknown parameter"_q);
		}
	}
	auto flagValues = std::vector<uint32>(params.size(), 0);
	for (auto i = 0, count = int(params.size()); i != count; ++i) {
		if (params[i].type != u"#") {
			continue;
		}
		for (auto j = 0; j != count; ++j) {
			const auto &param = params[j];
			if (param.flagParam != i) {
				continue;
			}
			const auto value = source.value(param.name);
			if (value.isUndefined() || value.isNull()) {
				continue;
			} else if (param.type == u"true") {
				if (!value.isBool()) {
					return e.fail(
						path + u"."_q + param.name,
						u"expected true or false"_q);
				} else if (!value.toBool()) {
					continue;
				}
			}
			flagValues[i] |= (1U << param.flagBit);
		}
	}
	if (boxed) {
		WriteUInt32(out, combinator.id);
	}
	for (auto i = 0, count = int(params.size()); i != count; ++i) {
		const auto &param = params[i];
		if (param.type == u"#") {
			WriteUInt32(out, flagValues[i]);
			continue;
		} else if (param.type == u"true") {
			continue;
		}
		const auto value = source.value(param.name);
		if (param.flagParam >= 0) {
			if (!(flagValues[param.flagParam] & (1U << param.flagBit))) {
				continue;
			}
		} else if (value.isUndefined() || value.isNull()) {
			return e.fail(
				path + u"."_q + param.name,
				u"missing required parameter"_q);
		}
		if (!CheckBareIds(
				e,
				combinator,
				param,
				value,
				path + u"."_q + param.name)
			|| !EncodeValue(
				e,
				out,
				param.type,
				value,
				path + u"."_q + param.name)) {
			return false;
		}
	}
	return true;
}

} // namespace

std::optional<QByteArray> EncodeTlCall(
		const TlSchema &schema,
		const QString &method,
		const QJsonObject &params,
		const TlRefs &refs,
		QString &error) {
	const auto function = schema.function(method);
	if (!function) {
		error = u"Unknown method: %1."_q.arg(method);
		return std::nullopt;
	} else if (function->generic) {
		error = u"%1 is generic and cannot be called directly."_q.arg(method);
		return std::nullopt;
	}
	auto encoder = Encoder{ schema, refs, error };
	auto out = QByteArray();
	if (!EncodeCombinator(encoder, out, *function, params, u"params"_q, true)) {
		return std::nullopt;
	}
	return out;
}

namespace {

struct Decoder {
	const TlSchema &schema;
	const QByteArray &bytes;
	const TlDecodeOptions &options;
	std::vector<TlSpan> *spans;
	QString &error;
	int offset = 0;
};

[[nodiscard]] bool Take(Decoder &d, int size, const char **result) {
	if (size < 0 || d.offset + size > d.bytes.size()) {
		d.error = u"Truncated data."_q;
		return false;
	}
	*result = d.bytes.constData() + d.offset;
	d.offset += size;
	return true;
}

[[nodiscard]] bool ReadUInt32(Decoder &d, uint32 &out) {
	const char *p = nullptr;
	if (!Take(d, 4, &p)) {
		return false;
	}
	out = uint32(uchar(p[0]))
		| (uint32(uchar(p[1])) << 8)
		| (uint32(uchar(p[2])) << 16)
		| (uint32(uchar(p[3])) << 24);
	return true;
}

[[nodiscard]] bool ReadInt32(Decoder &d, uint32 &out) {
	return ReadUInt32(d, out);
}

[[nodiscard]] bool ReadInt64(Decoder &d, int64 &out) {
	const char *p = nullptr;
	if (!Take(d, 8, &p)) {
		return false;
	}
	out = int64();
	for (auto i = 7; i >= 0; --i) {
		out = (out << 8) | uchar(p[i]);
	}
	return true;
}

[[nodiscard]] bool ReadDouble(Decoder &d, double &out) {
	const char *p = nullptr;
	if (!Take(d, 8, &p)) {
		return false;
	}
	memcpy(&out, p, sizeof(double));
	return true;
}

[[nodiscard]] bool ReadByteString(Decoder &d, QByteArray &out) {
	const char *p = nullptr;
	if (!Take(d, 1, &p)) {
		return false;
	}
	auto size = int(uchar(p[0]));
	auto prefix = 1;
	if (size == 0xfe) {
		if (!Take(d, 3, &p)) {
			return false;
		}
		size = uchar(p[0]) | (uchar(p[1]) << 8) | (uchar(p[2]) << 16);
		prefix = 4;
	}
	const char *data = nullptr;
	if (!Take(d, size, &data)) {
		return false;
	}
	out = QByteArray(data, size);
	return Take(d, (4 - ((prefix + size) % 4)) % 4, &data);
}

[[nodiscard]] bool HasSpan(
		const TlDecodeOptions &options,
		const QString &type) {
	return std::find(
		options.spanTypes.begin(),
		options.spanTypes.end(),
		type) != options.spanTypes.end();
}

bool DecodeValue(
		Decoder &d,
		const QString &type,
		QJsonValue &out,
		bool insideSpan);

bool DecodeFields(
		Decoder &d,
		const TlCombinator &combinator,
		QJsonObject &out,
		bool insideSpan) {
	auto flagValues = std::vector<uint32>(combinator.params.size(), 0);
	for (auto i = 0, count = int(combinator.params.size()); i != count; ++i) {
		const auto &param = combinator.params[i];
		auto value = QJsonValue();
		if (param.type == u"#") {
			if (!ReadInt32(d, flagValues[i])) {
				return false;
			}
			continue;
		} else if (param.type == u"true") {
			if (flagValues[param.flagParam] & (1U << param.flagBit)) {
				out.insert(param.name, true);
			}
			continue;
		} else if (param.flagParam >= 0) {
			if (!(flagValues[param.flagParam] & (1U << param.flagBit))) {
				continue;
			}
		}
		if (!DecodeValue(d, param.type, value, insideSpan)) {
			return false;
		}
		if (param.name != u"access_hash" && param.name != u"file_reference") {
			out.insert(param.name, value);
		}
	}
	return true;
}

bool DecodeValue(
		Decoder &d,
		const QString &type,
		QJsonValue &out,
		bool insideSpan) {
	if (TlIsBuiltinType(type)) {
		if (type == u"int") {
			auto raw = uint32(0);
			if (!ReadInt32(d, raw)) {
				return false;
			}
			out = int32(raw);
		} else if (type == u"long") {
			auto raw = int64(0);
			if (!ReadInt64(d, raw)) {
				return false;
			}
			out = QString::number(raw);
		} else if (type == u"double") {
			auto raw = double(0);
			if (!ReadDouble(d, raw)) {
				return false;
			}
			out = raw;
		} else if (type == u"string") {
			auto raw = QByteArray();
			if (!ReadByteString(d, raw)) {
				return false;
			}
			out = QString::fromUtf8(raw);
		} else if (type == u"bytes") {
			auto raw = QByteArray();
			if (!ReadByteString(d, raw)) {
				return false;
			}
			out = (raw.size() > d.options.maxBytesShown)
				? u"[%1 bytes]"_q.arg(raw.size())
				: QString::fromLatin1(raw.toBase64());
		} else if (type == u"Bool") {
			auto id = uint32(0);
			if (!ReadUInt32(d, id)) {
				return false;
			} else if (id == kBoolTrueId) {
				out = true;
			} else if (id == kBoolFalseId) {
				out = false;
			} else {
				d.error = u"Constructor 0x%1 is not a Bool."_q.arg(
					id,
					8,
					16,
					QChar('0'));
				return false;
			}
		} else if (type == u"true") {
			out = true;
		} else if (type == u"int128" || type == u"int256") {
			auto raw = QByteArray();
			const char *p = nullptr;
			if (!Take(d, (type == u"int128") ? 16 : 32, &p)) {
				return false;
			}
			raw = QByteArray(p, (type == u"int128") ? 16 : 32);
			out = QString::fromLatin1(raw.toHex());
		} else {
			out = QJsonValue();
		}
		return true;
	}
	auto item = TlVectorItem(type);
	if (item) {
		if (type.startsWith(u"Vector<")) {
			auto id = uint32(0);
			if (!ReadUInt32(d, id)) {
				return false;
			} else if (id != kVectorId) {
				d.error = u"Constructor 0x%1 is not of type %2."_q.arg(
					QString::number(id, 16),
					type);
				return false;
			}
		}
		auto count = uint32(0);
		if (!ReadInt32(d, count)) {
			return false;
		}
		auto array = QJsonArray();
		for (auto i = 0; i < count; ++i) {
			auto value = QJsonValue();
			if (!DecodeValue(d, *item, value, insideSpan)) {
				return false;
			}
			array.append(value);
		}
		out = array;
		return true;
	}
	const auto boxed = !TlIsBareType(type);
	auto begin = -1;
	if (boxed) {
		if (!insideSpan && HasSpan(d.options, type) && d.spans) {
			begin = d.offset;
			insideSpan = true;
		}
		auto id = uint32(0);
		if (!ReadUInt32(d, id)) {
			return false;
			}
		const auto combinator = d.schema.constructor(id);
		if (!combinator) {
			d.error = u"Unknown constructor 0x%1."_q.arg(
				id,
				8,
				16,
				QChar('0'));
			return false;
		} else if (combinator->result != type) {
			d.error = u"Constructor 0x%1 is not of type %2."_q.arg(
				id,
				8,
				16,
				QChar('0')).arg(type);
			return false;
		}
		auto object = QJsonObject();
		object.insert(u"_"_q, combinator->name);
		if (!DecodeFields(d, *combinator, object, insideSpan)) {
			return false;
		}
		out = object;
	} else {
		const auto list = d.schema.constructorsOf(type);
		if (list.size() != 1) {
			d.error = u"Unknown bare type %1."_q.arg(type);
			return false;
		}
		auto object = QJsonObject();
		object.insert(u"_"_q, list.front()->name);
		if (!DecodeFields(d, *list.front(), object, insideSpan)) {
			return false;
		}
		out = object;
	}
	if (begin >= 0 && d.spans) {
		d.spans->push_back({ type, begin, d.offset });
	}
	return true;
}

} // namespace

std::optional<QJsonValue> DecodeTl(
		const TlSchema &schema,
		const QString &type,
		const QByteArray &bytes,
		QString &error,
		const TlDecodeOptions &options,
		std::vector<TlSpan> *spans) {
	auto decoder = Decoder{ schema, bytes, options, spans, error, 0 };
	auto value = QJsonValue();
	if (!DecodeValue(decoder, type, value, false)) {
		return std::nullopt;
	} else if (decoder.offset != bytes.size()) {
		error = u"%1 bytes remain after the value."_q.arg(
			bytes.size() - decoder.offset);
		return std::nullopt;
	}
	return value;
}

namespace {

struct PeerRef {
	QString kind;
	int64 id = 0;
};

[[nodiscard]] std::optional<int64> ParsePeerId(const QJsonValue &value) {
	if (value.isDouble()) {
		return int64(value.toDouble());
	} else if (value.isString()) {
		auto ok = false;
		const auto result = value.toString().toLongLong(&ok);
		return ok ? std::optional<int64>(result) : std::nullopt;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<PeerRef> PeerRefOf(const QJsonValue &value) {
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto object = value.toObject();
	const auto type = object.value(u"_"_q).toString();
	auto kind = QString();
	auto field = QString();
	if (type == u"peerUser") {
		kind = u"user"_q;
		field = u"user_id"_q;
	} else if (type == u"peerChat") {
		kind = u"chat"_q;
		field = u"chat_id"_q;
	} else if (type == u"peerChannel") {
		kind = u"channel"_q;
		field = u"channel_id"_q;
	}
	if (kind.isEmpty()) {
		return std::nullopt;
	}
	const auto id = ParsePeerId(object.value(field));
	if (!id) {
		return std::nullopt;
	}
	return PeerRef{ kind, *id };
}

[[nodiscard]] bool ObjectRemoved(
		const QJsonObject &object,
		const Fn<bool(const QString &kind, int64 id)> &excluded) {
	for (const auto field : { u"peer_id"_q, u"peer"_q, u"saved_peer_id"_q }) {
		const auto ref = PeerRefOf(object.value(field));
		if (ref && excluded(ref->kind, ref->id)) {
			return true;
		}
	}
	const auto type = object.value(u"_"_q).toString();
	const auto kind = (type == u"chat"
			|| type == u"chatForbidden"
			|| type == u"chatFull")
		? u"chat"_q
		: (type == u"channel"
			|| type == u"channelForbidden"
			|| type == u"channelFull")
		? u"channel"_q
		: (type == u"dialogFilter" || type == u"dialogFilterChatlist")
		? u"folder"_q
		: QString();
	if (!kind.isEmpty()) {
		const auto id = ParsePeerId(object.value(u"id"_q));
		if (id && excluded(kind, *id)) {
			return true;
		}
	}
	const auto fields = {
		std::pair{ u"chat_id"_q, u"chat"_q },
		std::pair{ u"channel_id"_q, u"channel"_q },
	};
	for (const auto &[field, fieldKind] : fields) {
		const auto id = ParsePeerId(object.value(field));
		if (id && excluded(fieldKind, *id)) {
			return true;
		}
	}
	if (type == u"inputPeerUser") {
		const auto id = ParsePeerId(object.value(u"user_id"_q));
		if (id && excluded(u"user"_q, *id)) {
			return true;
		}
	}
	return false;
}

QJsonValue ScrubValue(
		const QJsonValue &value,
		const Fn<bool(const QString &kind, int64 id)> &excluded) {
	if (value.isArray()) {
		auto result = QJsonArray();
		for (const auto &item : value.toArray()) {
			auto scrubbed = ScrubValue(item, excluded);
			if (!scrubbed.isUndefined()) {
				result.append(std::move(scrubbed));
			}
		}
		return result;
	} else if (value.isObject()) {
		const auto object = value.toObject();
		if (ObjectRemoved(object, excluded)) {
			return QJsonValue(QJsonValue::Undefined);
		}
		auto result = QJsonObject();
		for (auto i = object.begin(); i != object.end(); ++i) {
			auto scrubbed = ScrubValue(i.value(), excluded);
			result.insert(
				i.key(),
				scrubbed.isUndefined() ? QJsonValue(QJsonValue::Null)
					: std::move(scrubbed));
		}
		return result;
	}
	return value;
}

} // namespace

QJsonValue ScrubExcludedPeers(
		QJsonValue value,
		Fn<bool(const QString &kind, int64 id)> excluded) {
	auto result = ScrubValue(value, excluded);
	if (result.isUndefined()) {
		return QJsonValue(QJsonValue::Null);
	}
	return result;
}

} // namespace Agent