/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_tl_codec.h"
#include "agent/core/agent_tl_schema.h"

#include <QtCore/QJsonArray>
#include <algorithm>

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

[[nodiscard]] Parsed CodecSchema() {
	return Parse(QByteArray(
		"---types---\n"
		"obj.empty#11111111 = Obj;\n"
		"obj.pair#22222222 x:int y:string = Obj;\n"
		"only.one#aaaaaa01 z:int = Only;\n"
		"obj.flagged#77777777 flags:# t:flags.0?true x:flags.1?int"
			" y:string = Obj;\n"
		"obj.secret#88888888 access_hash:long x:int"
			" file_reference:bytes = Obj;\n"
		"msg.small#44444444 x:int = Msg;\n"
		"msg.big#55555555 m:Msg y:int = Msg;\n"
		"wrap.it#66666666 m:Msg = Wrap;\n"
		"inputPeerEmpty#7f3b18ea = InputPeer;\n"
		"inputPeerSelf#7da07ec9 = InputPeer;\n"
		"inputUserEmpty#b98886cf = InputUser;\n"
		"inputUserSelf#da379149 = InputUser;\n"
		"inputChannelEmpty#ee8c1e86 = InputChannel;\n"
		"---functions---\n"
		"test.call#a1b2c3d4 peer:InputPeer n:int s:string = Ok;\n"
		"test.flags#c1b2c3d4 flags:# t:flags.0?true x:flags.1?int"
			" o:flags.2?Obj = Ok;\n"
		"test.vec#d1b2c3d4 v:Vector<int> b:vector<int> o:Vector<Obj> = Ok;\n"
		"test.str#e1b2c3d4 s:string = Ok;\n"
		"test.raw#f1b2c3d4 l:long by:bytes h:int128 = Ok;\n"
		"test.bool#a2b2c3d4 b:Bool = Ok;\n"
		"test.only#b2b2c3d4 o:Only = Ok;\n"
		"test.peers#c2b2c3d4 p:Vector<InputPeer> = Ok;\n"
		"test.gen#d2b2c3d4 {X:Type} q:!X = X;\n"));
}

[[nodiscard]] TlRefs FixedRefs(QByteArray bytes) {
	return TlRefs{
		[bytes](
				const QString &type,
				const QJsonValue &value,
				QString &error) -> std::optional<QByteArray> {
			return bytes;
		} };
}

[[nodiscard]] QString EncodeFail(
		const TlSchema &schema,
		const QString &method,
		const QJsonObject &params,
		const TlRefs &refs,
		const QString &expected) {
	auto error = QString();
	if (EncodeTlCall(schema, method, params, refs, error)) {
		return u"must fail with: "_q + expected;
	} else if (!error.contains(expected)) {
		return u"unexpected error: "_q + error;
	}
	return QString();
}

[[nodiscard]] QString EncodeBytes(
		const TlSchema &schema,
		const QString &method,
		const QJsonObject &params,
		const TlRefs &refs,
		const QByteArray &expected) {
	auto error = QString();
	const auto result = EncodeTlCall(schema, method, params, refs, error);
	if (!result) {
		return error;
	} else if (*result != expected) {
		return u"unexpected bytes "_q + QString::fromLatin1(result->toHex());
	}
	return QString();
}

} // namespace

[[nodiscard]] QString DecodeOk(
		const TlSchema &schema,
		const QString &type,
		const QByteArray &bytes,
		const QJsonValue &expected,
		const TlDecodeOptions &options = TlDecodeOptions()) {
	auto error = QString();
	const auto result = DecodeTl(schema, type, bytes, error, options);
	if (!result) {
		return error;
	} else if (*result != expected) {
		return u"unexpected decoded value"_q;
	}
	return QString();
}

[[nodiscard]] QString DecodeFail(
		const TlSchema &schema,
		const QString &type,
		const QByteArray &bytes,
		const QString &expected) {
	auto error = QString();
	if (DecodeTl(schema, type, bytes, error)) {
		return u"must fail with: "_q + expected;
	} else if (!error.contains(expected)) {
		return u"unexpected error: "_q + error;
	}
	return QString();
}

std::vector<Check> TlChecks() {
	return {
		{ u"tl_parse_real_api"_q, [] {
			const auto text = ReadApiTl();
			if (text.isEmpty()) {
				return u"api.tl is empty"_q;
			}
			const auto parsed = Parse(text);
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			return QString();
		} },
		{ u"tl_layer"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			} else if (parsed.schema->layer() != 230) {
				return u"unexpected layer "_q
					+ QString::number(parsed.schema->layer());
			}
			return QString();
		} },
		{ u"tl_get_history_line"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			const auto method = parsed.schema->function(u"messages.getHistory"_q);
			if (!method) {
				return u"messages.getHistory not found"_q;
			} else if (method->id != 0x4423e6c5) {
				return u"unexpected id"_q;
			} else if (!method->function || method->generic) {
				return u"must be a non-generic function"_q;
			} else if (method->params.size() != 8) {
				return u"unexpected parameter count "_q
					+ QString::number(method->params.size());
			} else if (method->params[0].name != u"peer"
				|| method->params[0].type != u"InputPeer") {
				return u"unexpected first parameter"_q;
			} else if (method->params[1].name != u"offset_id"
				|| method->params[1].type != u"int") {
				return u"unexpected second parameter"_q;
			} else if (method->params[7].name != u"hash"
				|| method->params[7].type != u"long") {
				return u"unexpected last parameter"_q;
			} else if (method->result != u"messages.Messages") {
				return u"unexpected result "_q + method->result;
			}
			return QString();
		} },
		{ u"tl_input_peer_user_lookup"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			const auto byName = parsed.schema->constructor(u"inputPeerUser"_q);
			const auto byId = parsed.schema->constructor(0xdde8a54c);
			if (!byName || !byId || byName != byId) {
				return u"inputPeerUser must be found by name and by id"_q;
			} else if (byName->params.size() != 2
				|| byName->params[0].name != u"user_id"
				|| byName->params[0].type != u"long"
				|| byName->params[1].name != u"access_hash"
				|| byName->params[1].type != u"long") {
				return u"unexpected parameters"_q;
			} else if (byName->result != u"InputPeer") {
				return u"unexpected result "_q + byName->result;
			}
			return QString();
		} },
		{ u"tl_constructors_of_input_peer"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			const auto list = parsed.schema->constructorsOf(u"InputPeer"_q);
			if (list.empty() || list.front()->name != u"inputPeerEmpty"_q) {
				return u"constructorsOf(InputPeer) must start with inputPeerEmpty"_q;
			}
			const auto self = std::find_if(
				list.begin(),
				list.end(),
				[](const TlCombinator *c) {
					return c->name == u"inputPeerSelf"_q;
				});
			if (self == list.end()) {
				return u"inputPeerSelf is missing"_q;
			}
			return QString();
		} },
		{ u"tl_user_flags2"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			const auto user = parsed.schema->constructor(u"user"_q);
			if (!user) {
				return u"user not found"_q;
			}
			const auto botCanEdit = std::find_if(
				user->params.begin(),
				user->params.end(),
				[](const TlParam &param) {
					return param.name == u"bot_can_edit"_q;
				});
			if (botCanEdit == user->params.end()) {
				return u"bot_can_edit is missing"_q;
			}
			const auto index = botCanEdit - user->params.begin();
			if (botCanEdit->type != u"true"
				|| botCanEdit->flagBit != 1) {
				return u"unexpected flag bit or type"_q;
			} else if (index < 0
				|| user->params[botCanEdit->flagParam].name != u"flags2"_q
				|| user->params[botCanEdit->flagParam].type != u"#") {
				return u"flagParam must point at the flags2 parameter"_q;
			}
			return QString();
		} },
		{ u"tl_invoke_with_layer_generic"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			const auto method = parsed.schema->function(u"invokeWithLayer"_q);
			if (!method) {
				return u"invokeWithLayer not found"_q;
			} else if (!method->generic) {
				return u"invokeWithLayer must be generic"_q;
			}
			return QString();
		} },
		{ u"tl_malformed_line_fails"_q, [] {
			const auto text = QByteArray(
				"---types---\n"
				"ok.ok#00000001 = Ok;\n"
				"bad.bad#00000002 x = Bad;\n");
			const auto parsed = Parse(text);
			if (parsed.schema) {
				return u"a line without = must fail the parse"_q;
			} else if (!parsed.error.contains(u"Line 3"_q)) {
				return u"the error must name the line: "_q + parsed.error;
			}
			const auto flagText = QByteArray(
				"---types---\n"
				"bad.flags#00000003 x:flags.0?int = Bad;\n");
			const auto flagParsed = Parse(flagText);
			if (flagParsed.schema) {
				return u"a flag naming an unknown parameter must fail"_q;
			}
			return QString();
		} },
		{ u"tl_builtin_lines_skipped"_q, [] {
			const auto text = QByteArray(
				"int ? = Int;\n"
				"vector#1cb5c415 {t:Type} # [ t ] = Vector t;\n"
				"noId noId2 = X;\n"
				"foo#zzzzzzzz = X;\n"
				"boolTrue#997275b5 = Bool;\n");
			const auto parsed = Parse(text);
			if (!parsed.schema) {
				return u"built-in lines must be skipped: "_q + parsed.error;
			} else if (parsed.schema->constructors().size() != 1) {
				return u"only boolTrue must survive"_q;
			} else if (parsed.schema->constructors()[0].name
				!= u"boolTrue"_q) {
				return u"unexpected constructor"_q;
			}
			return QString();
		} },
		{ u"tl_encode_call_bytes"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			return EncodeBytes(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{
					{ u"peer"_q, 1 },
					{ u"n"_q, 7 },
					{ u"s"_q, u"abc"_q } },
				FixedRefs(QByteArray("\x01\x02\x03\x04", 4)),
				QByteArray::fromHex("d4c3b2a1"
					"01020304"
					"07000000"
					"03616263"));
		} },
		{ u"tl_encode_long"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			auto check = EncodeBytes(
				*parsed.schema,
				u"test.raw"_q,
				QJsonObject{
					{ u"l"_q, 4398046511104.0 },
					{ u"by"_q, u"aGVsbG8="_q },
					{ u"h"_q, u"00112233445566778899aabbccddeeff"_q } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2f1"
					"0000000000040000"
					"0568656c6c6f0000"
					"00112233445566778899aabbccddeeff"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.raw"_q,
				QJsonObject{
					{ u"l"_q, u"9223372036854775807"_q },
					{ u"by"_q, u""_q },
					{ u"h"_q, u"00000000000000000000000000000000"_q } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2f1"
					"ffffffffffffff7f"
					"00000000"
					"00000000000000000000000000000000"));
			if (!check.isEmpty()) {
				return check;
			}
			return EncodeFail(*parsed.schema, u"test.raw"_q,
				QJsonObject{ { u"l"_q, u"abc"_q },
					{ u"by"_q, u""_q },
					{ u"h"_q, u""_q } },
				TlRefs(),
				u"expected a decimal number"_q);
		} },
		{ u"tl_encode_string_lengths"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto encode = [&](const QString &value) {
				auto error = QString();
				return EncodeTlCall(
					*parsed.schema,
					u"test.str"_q,
					QJsonObject{ { u"s"_q, value } },
					TlRefs(),
					error);
			};
			const auto empty = encode(QString());
			if (!empty || *empty != QByteArray::fromHex("d4c3b2e100000000")) {
				return u"empty string must be four zero bytes"_q;
			}
			const auto three = encode(u"xyz"_q);
			if (!three || *three != QByteArray::fromHex("d4c3b2e10378797a")) {
				return u"three-byte string must be padded with one byte"_q;
			}
			const auto c253 = encode(QString(253, 'x'));
			if (!c253 || c253->size() != 260
				|| (*c253)[4] != char(0xfd)
				|| (*c253)[258] != 0
				|| (*c253)[259] != 0) {
				return u"a 253-byte string must use a one-byte length"_q;
			}
			const auto c254 = encode(QString(254, 'x'));
			if (!c254 || c254->size() != 264
				|| (*c254)[4] != char(0xfe)
				|| (*c254)[5] != char(0xfe)
				|| (*c254)[6] != 0
				|| (*c254)[262] != 0
				|| (*c254)[263] != 0) {
				return u"a 254-byte string must use 0xfe plus three bytes"_q;
			}
			const auto c300 = encode(QString(300, 'x'));
			if (!c300 || c300->size() != 308
				|| (*c300)[4] != char(0xfe)
				|| (*c300)[5] != char(0x2c)
				|| (*c300)[6] != char(0x01)
				|| (*c300)[7] != 0
				|| (*c300)[307] != 'x') {
				return u"a 300-byte string needs no padding"_q;
			}
			return QString();
		} },
		{ u"tl_encode_bool_and_vectors"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			auto check = EncodeBytes(
				*parsed.schema,
				u"test.bool"_q,
				QJsonObject{ { u"b"_q, true } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2a2b5757299"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.bool"_q,
				QJsonObject{ { u"b"_q, false } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2a2379779bc"));
			if (!check.isEmpty()) {
				return check;
			}
			return EncodeBytes(
				*parsed.schema,
				u"test.vec"_q,
				QJsonObject{
					{ u"v"_q, QJsonArray{ 1, 2 } },
					{ u"b"_q, QJsonArray{ 3, 4 } },
					{ u"o"_q, QJsonArray{ QJsonObject{
						{ u"_"_q, u"obj.pair"_q },
						{ u"x"_q, 1 }, { u"y"_q, u""_q } } } } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2d1"
					"15c4b51c" "02000000" "01000000" "02000000"
					"02000000" "03000000" "04000000"
					"15c4b51c" "01000000" "22222222"
					"01000000" "00000000"));
		} },
		{ u"tl_encode_nested_objects"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			auto check = EncodeBytes(
				*parsed.schema,
				u"test.only"_q,
				QJsonObject{ { u"o"_q, QJsonObject{ { u"z"_q, 5 } } } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2b2" "01aaaaaa" "05000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.only"_q,
				QJsonObject{ { u"o"_q, QJsonObject{
					{ u"_"_q, u"only.one"_q }, { u"z"_q, 5 } } } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2b2" "01aaaaaa" "05000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"o"_q, QJsonObject() } },
				TlRefs(),
				u"pass \"_\" naming a constructor of Obj"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"o"_q, QJsonObject{ { u"_"_q, u"obj.nope"_q } } } },
				TlRefs(),
				u"unknown constructor obj.nope"_q);
			if (!check.isEmpty()) {
				return check;
			}
			return EncodeFail(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"o"_q, QJsonObject{ { u"_"_q, u"only.one"_q } } } },
				TlRefs(),
				u"only.one is not of type Obj"_q);
		} },
		{ u"tl_encode_flags"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			auto check = EncodeBytes(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject(),
				TlRefs(),
				QByteArray::fromHex("d4c3b2c1" "00000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"t"_q, true } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2c1" "01000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"t"_q, false }, { u"x"_q, 5 } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2c1" "02000000" "05000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"t"_q, true }, { u"x"_q, 5 } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2c1" "03000000" "05000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"t"_q, QJsonValue() },
					{ u"o"_q, QJsonObject{
						{ u"_"_q, u"obj.pair"_q },
						{ u"x"_q, 1 }, { u"y"_q, u"z"_q } } } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2c1" "04000000" "22222222"
					"01000000" "017a0000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"flags"_q, 0 } },
				TlRefs(),
				u"params.flags: unknown parameter"_q);
			if (!check.isEmpty()) {
				return check;
			}
			return EncodeFail(
				*parsed.schema,
				u"test.flags"_q,
				QJsonObject{ { u"t"_q, u"yes"_q } },
				TlRefs(),
				u"expected true or false"_q);
		} },
		{ u"tl_encode_errors"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto refs = FixedRefs(QByteArray::fromHex("ea183b7f"));
			auto check = EncodeFail(
				*parsed.schema,
				u"test.nope"_q,
				QJsonObject(),
				refs,
				u"Unknown method: test.nope."_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.gen"_q,
				QJsonObject(),
				refs,
				u"test.gen is generic and cannot be called directly."_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 1 } },
				refs,
				u"params.s: missing required parameter"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 3.5 },
					{ u"s"_q, u""_q } },
				refs,
				u"params.n: expected an integer"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 2147483648.0 },
					{ u"s"_q, u""_q } },
				refs,
				u"params.n: a number outside the int range"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, u"5"_q },
					{ u"s"_q, u""_q } },
				refs,
				u"params.n: expected a number"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 1 },
					{ u"s"_q, 5 } },
				refs,
				u"params.s: expected a string"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 1 },
					{ u"s"_q, u""_q }, { u"zzz"_q, 1 } },
				refs,
				u"params.zzz: unknown parameter"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.vec"_q,
				QJsonObject{ { u"v"_q, 5 }, { u"b"_q, QJsonArray() },
					{ u"o"_q, QJsonArray() } },
				refs,
				u"params.v: expected an array"_q);
			if (!check.isEmpty()) {
				return check;
			}
			return EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, QJsonArray() }, { u"n"_q, 1 },
					{ u"s"_q, u""_q } },
				TlRefs(),
				u"params.peer: expected a chat id or @username"_q);
		} },
		{ u"tl_encode_refs"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			auto seenType = QString();
			auto seenValue = QJsonValue();
			auto refs = TlRefs{
				[&](
						const QString &type,
						const QJsonValue &value,
						QString &error) -> std::optional<QByteArray> {
					seenType = type;
					seenValue = value;
					return QByteArray("\x01\x02\x03\x04", 4);
				} };
			auto check = EncodeBytes(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{
					{ u"peer"_q, u"@me"_q },
					{ u"n"_q, 1 },
					{ u"s"_q, u""_q } },
				refs,
				QByteArray::fromHex("d4c3b2a1" "01020304"
					"01000000" "00000000"));
			if (!check.isEmpty()) {
				return check;
			} else if (seenType != u"InputPeer"_q
				|| seenValue.toString() != u"@me"_q) {
				return u"the hook must receive the type and the value"_q;
			}
			const auto failing = TlRefs{ [](const QString &type,
					const QJsonValue &value,
					QString &error) -> std::optional<QByteArray> {
				error = u"No such chat."_q;
				return std::nullopt;
			} };
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 1 },
					{ u"s"_q, u""_q } },
				failing,
				u"params.peer: No such chat."_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, 1 }, { u"n"_q, 1 },
					{ u"s"_q, u""_q } },
				TlRefs(),
				u"params.peer: cannot resolve InputPeer"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeFail(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, QJsonObject{
					{ u"_"_q, u"inputPeerChat"_q }, { u"chat_id"_q, 5 } } },
					{ u"n"_q, 1 }, { u"s"_q, u""_q } },
				TlRefs(),
				u"Pass chats as ids or @usernames, not objects."_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.call"_q,
				QJsonObject{ { u"peer"_q, QJsonObject{
					{ u"_"_q, u"inputPeerEmpty"_q } } },
					{ u"n"_q, 1 }, { u"s"_q, u""_q } },
				TlRefs(),
				QByteArray::fromHex("d4c3b2a1" "ea183b7f"
					"01000000" "00000000"));
			if (!check.isEmpty()) {
				return check;
			}
			check = EncodeBytes(
				*parsed.schema,
				u"test.peers"_q,
				QJsonObject{ { u"p"_q, QJsonArray{ 7 } } },
				refs,
				QByteArray::fromHex("d4c3b2c2" "15c4b51c" "01000000"
					"01020304"));
			if (!check.isEmpty()) {
				return check;
			} else if (seenType != u"InputPeer"_q || seenValue.toDouble() != 7) {
				return u"a vector item must reach the hook"_q;
			}
			return QString();
		} },
		{ u"tl_encode_real_get_history"_q, [] {
			const auto parsed = Parse(ReadApiTl());
			if (!parsed.schema) {
				return u"api.tl must parse: "_q + parsed.error;
			}
			const auto refs = FixedRefs(QByteArray::fromHex("ea183b7f"));
			return EncodeBytes(
				*parsed.schema,
				u"messages.getHistory"_q,
				QJsonObject{
					{ u"peer"_q, u"@chat"_q },
					{ u"offset_id"_q, 0 },
					{ u"offset_date"_q, 0 },
					{ u"add_offset"_q, 0 },
					{ u"limit"_q, 10 },
					{ u"max_id"_q, 0 },
					{ u"min_id"_q, 0 },
					{ u"hash"_q, u"0"_q } },
				refs,
				QByteArray::fromHex("c5e62344" "ea183b7f"
					"00000000" "00000000" "00000000"
					"0a000000" "00000000" "00000000"
					"0000000000000000"));
		} },
		{ u"tl_decode_roundtrip"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto &schema = *parsed.schema;
			auto check = DecodeOk(schema, u"Obj"_q,
				QByteArray::fromHex("22222222" "07000000" "03616263"),
				QJsonObject{ { u"_"_q, u"obj.pair"_q },
					{ u"x"_q, 7 }, { u"y"_q, u"abc"_q } });
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"Only"_q,
				QByteArray::fromHex("01aaaaaa" "05000000"),
				QJsonObject{ { u"_"_q, u"only.one"_q }, { u"z"_q, 5 } });
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"long"_q,
				QByteArray::fromHex("0000000000040000"),
				QJsonValue(u"4398046511104"_q));
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"int128"_q,
				QByteArray::fromHex("00112233445566778899aabbccddeeff"),
				QJsonValue(u"00112233445566778899aabbccddeeff"_q));
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"Bool"_q,
				QByteArray::fromHex("b5757299"), QJsonValue(true));
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"Bool"_q,
				QByteArray::fromHex("379779bc"), QJsonValue(false));
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"Vector<int>"_q,
				QByteArray::fromHex("15c4b51c" "02000000"
					"01000000" "02000000"),
				QJsonArray{ 1, 2 });
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"vector<int>"_q,
				QByteArray::fromHex("02000000" "01000000" "02000000"),
				QJsonArray{ 1, 2 });
			if (!check.isEmpty()) {
				return check;
			}
			return DecodeOk(schema, u"Vector<Obj>"_q,
				QByteArray::fromHex("15c4b51c" "01000000" "22222222"
					"01000000" "03616263"),
				QJsonArray{ QJsonObject{ { u"_"_q, u"obj.pair"_q },
					{ u"x"_q, 1 }, { u"y"_q, u"abc"_q } } });
		} },
		{ u"tl_decode_strings_and_bytes"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto &schema = *parsed.schema;
			const auto values = {
				QString(),
				u"xyz"_q,
				QString(253, 'x'),
				QString(254, 'x'),
				QString(300, 'x'),
			};
			for (const auto &value : values) {
				auto error = QString();
				auto encoded = EncodeTlCall(
					schema,
					u"test.str"_q,
					QJsonObject{ { u"s"_q, value } },
					TlRefs(),
					error);
				if (!encoded) {
					return error;
				}
				const auto decoded = DecodeTl(
					schema,
					u"string"_q,
					auto(encoded->mid(4)),
					error);
				if (!decoded || decoded->toString() != value) {
					return u"a string of %1 bytes must round-trip"_q.arg(
						value.size());
				}
			}
			auto check = DecodeOk(schema, u"bytes"_q,
				QByteArray::fromHex("0568656c6c6f0000"),
				QJsonValue(u"aGVsbG8="_q));
			if (!check.isEmpty()) {
				return check;
			}
			const auto big = QByteArray(300, '\x01');
			const auto framed = QByteArray::fromHex("fe2c0100") + big;
			check = DecodeOk(schema, u"bytes"_q,
				framed,
				QJsonValue(u"[300 bytes]"_q));
			if (!check.isEmpty()) {
				return check;
			}
			return DecodeOk(schema, u"bytes"_q,
				framed,
				QJsonValue(QString::fromLatin1(big.toBase64())),
				TlDecodeOptions{ .spanTypes = {}, .maxBytesShown = 1000 });
		} },
		{ u"tl_decode_flags_and_secrets"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto &schema = *parsed.schema;
			auto check = DecodeOk(schema, u"Obj"_q,
				QByteArray::fromHex("77777777" "03000000" "05000000"
					"02686900"),
				QJsonObject{ { u"_"_q, u"obj.flagged"_q },
					{ u"t"_q, true }, { u"x"_q, 5 }, { u"y"_q, u"hi"_q } });
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"Obj"_q,
				QByteArray::fromHex("77777777" "02000000" "05000000"
					"02686900"),
				QJsonObject{ { u"_"_q, u"obj.flagged"_q },
					{ u"x"_q, 5 }, { u"y"_q, u"hi"_q } });
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeOk(schema, u"Obj"_q,
				QByteArray::fromHex("77777777" "00000000" "02686900"),
				QJsonObject{ { u"_"_q, u"obj.flagged"_q },
					{ u"y"_q, u"hi"_q } });
			if (!check.isEmpty()) {
				return check;
			}
			return DecodeOk(schema, u"Obj"_q,
				QByteArray::fromHex("88888888" "ffffffffffffffff"
					"2a000000" "02010500"),
				QJsonObject{ { u"_"_q, u"obj.secret"_q }, { u"x"_q, 42 } });
		} },
		{ u"tl_decode_errors"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto &schema = *parsed.schema;
			auto check = DecodeFail(schema, u"Obj"_q,
				QByteArray::fromHex("22222222" "07000000"),
				u"Truncated data."_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeFail(schema, u"Obj"_q,
				QByteArray::fromHex("22222222" "07000000" "03616263"
					"00000000"),
				u"bytes remain after the value"_q);
			if (!check.isEmpty()) {
				return check;
			}
			check = DecodeFail(schema, u"Obj"_q,
				QByteArray::fromHex("efbeadde" "00000000"),
				u"Unknown constructor 0xdeadbeef."_q);
			if (!check.isEmpty()) {
				return check;
			}
			return DecodeFail(schema, u"Obj"_q,
				QByteArray::fromHex("01aaaaaa" "05000000"),
				u"Constructor 0xaaaaaa01 is not of type Obj."_q);
		} },
		{ u"tl_decode_spans"_q, [] {
			const auto parsed = CodecSchema();
			if (!parsed.schema) {
				return u"codec schema must parse: "_q + parsed.error;
			}
			const auto &schema = *parsed.schema;
			const auto options = TlDecodeOptions{
				.spanTypes = { u"Msg"_q, u"Wrap"_q } };
			auto spans = std::vector<TlSpan>();
			auto error = QString();
			auto result = DecodeTl(
				schema,
				u"Wrap"_q,
				QByteArray::fromHex("66666666" "55555555" "44444444"
					"01000000" "02000000"),
				error,
				options,
				&spans);
			if (!result) {
				return error;
			} else if (spans.size() != 1
				|| spans[0].type != u"Wrap"_q
				|| spans[0].begin != 0
				|| spans[0].end != 20) {
				return u"the outermost span only must be recorded"_q;
			}
			spans.clear();
			result = DecodeTl(
				schema,
				u"Msg"_q,
				QByteArray::fromHex("55555555" "44444444"
					"01000000" "02000000"),
				error,
				options,
				&spans);
			if (!result) {
				return error;
			} else if (spans.size() != 1
				|| spans[0].type != u"Msg"_q
				|| spans[0].begin != 0
				|| spans[0].end != 16) {
				return u"the top Msg span must be recorded"_q;
			}
			spans.clear();
			result = DecodeTl(
				schema,
				u"Msg"_q,
				QByteArray::fromHex("44444444" "01000000"),
				error,
				options,
				&spans);
			if (!result) {
				return error;
			} else if (spans.size() != 1 || spans[0].end != 8) {
				return u"a small Msg span must be recorded"_q;
			}
			spans.clear();
			result = DecodeTl(
				schema,
				u"Obj"_q,
				QByteArray::fromHex("22222222" "07000000" "03616263"),
				error,
				options,
				&spans);
			if (!result || !spans.empty()) {
				return u"a type outside spanTypes must not be recorded"_q;
			}
			return QString();
		} },
		{ u"tl_scrub_excluded_peers"_q, [] {
			const auto excluded = [](const QString &kind, int64 id) {
				return (kind == u"channel" && id == 5)
					|| (kind == u"chat" && id == 7);
			};
			const auto message = [&](int64 channel) {
				return QJsonObject{ { u"_"_q, u"message"_q },
					{ u"peer_id"_q, QJsonObject{
						{ u"_"_q, u"peerChannel"_q },
						{ u"channel_id"_q, double(channel) } } } };
			};
			auto scrubbed = ScrubExcludedPeers(
				QJsonArray{ message(5), message(9) },
				excluded);
			auto list = scrubbed.toArray();
			if (list.size() != 1 || list.first().toObject()
				!= message(9)) {
				return u"a message of an excluded chat must be dropped"_q;
			}
			scrubbed = ScrubExcludedPeers(
				QJsonArray{ QJsonObject{ { u"_"_q, u"dialog"_q },
					{ u"peer"_q, QJsonObject{ { u"_"_q, u"peerChat"_q },
						{ u"chat_id"_q, 7 } } } } },
				excluded);
			if (!scrubbed.toArray().isEmpty()) {
				return u"a dialog of an excluded chat must be dropped"_q;
			}
			scrubbed = ScrubExcludedPeers(
				QJsonObject{ { u"_"_q, u"updateNewMessage"_q },
					{ u"message"_q, message(5) }, { u"pts"_q, 1 } },
				excluded);
			if (scrubbed.toObject().value(u"message"_q)
				!= QJsonValue(QJsonValue::Null)) {
				return u"a removed field value must become null"_q;
			} else if (scrubbed.toObject().value(u"pts"_q).toInt() != 1) {
				return u"the other fields must stay"_q;
			}
			scrubbed = ScrubExcludedPeers(
				QJsonArray{
					QJsonObject{ { u"_"_q, u"chat"_q }, { u"id"_q, 7 } },
					QJsonObject{ { u"_"_q, u"chatForbidden"_q }, { u"id"_q, 7 } },
					QJsonObject{ { u"_"_q, u"channel"_q }, { u"id"_q, 5 } },
					QJsonObject{ { u"_"_q, u"channelForbidden"_q },
						{ u"id"_q, 5 } },
					QJsonObject{ { u"_"_q, u"user"_q }, { u"id"_q, 7 } } },
				excluded);
			list = scrubbed.toArray();
			if (list.size() != 1 || list.first().toObject().value(u"_"_q)
				!= u"user"_q) {
				return u"excluded chats must go and users must stay"_q;
			}
			scrubbed = ScrubExcludedPeers(message(5), excluded);
			if (!scrubbed.isNull()) {
				return u"the whole value must become null"_q;
			}
			scrubbed = ScrubExcludedPeers(message(9), excluded);
			if (scrubbed.toObject().value(u"_"_q) != u"message"_q) {
				return u"a visible peer must stay"_q;
			}
			return QString();
		} },
	};
}
} // namespace Agent::Tests
