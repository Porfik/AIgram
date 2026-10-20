/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_tl_codec.h"
#include "agent/core/agent_tl_schema.h"

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

} // namespace

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
	};
}

} // namespace Agent::Tests