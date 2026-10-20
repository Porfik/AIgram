/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_tl_schema.h"

namespace Agent {
namespace {

[[nodiscard]] bool IsHexId(const QString &text) {
	if (text.size() != 8) {
		return false;
	}
	for (const auto &ch : text) {
		const auto c = ch.unicode();
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool IsDigits(const QString &text) {
	if (text.isEmpty()) {
		return false;
	}
	for (const auto &ch : text) {
		if (ch < '0' || ch > '9') {
			return false;
		}
	}
	return true;
}

} // namespace

const std::vector<TlCombinator> &TlSchema::constructors() const {
	return _constructors;
}

const std::vector<TlCombinator> &TlSchema::functions() const {
	return _functions;
}

int TlSchema::layer() const {
	return _layer;
}

const TlCombinator *TlSchema::constructor(uint32 id) const {
	const auto i = _constructorById.find(id);
	return (i == _constructorById.end()) ? nullptr : &_constructors[i->second];
}

const TlCombinator *TlSchema::constructor(const QString &name) const {
	const auto i = _constructorByName.find(name);
	return (i == _constructorByName.end()) ? nullptr : &_constructors[i->second];
}

const TlCombinator *TlSchema::function(const QString &name) const {
	const auto i = _functionByName.find(name);
	return (i == _functionByName.end()) ? nullptr : &_functions[i->second];
}

std::vector<const TlCombinator*> TlSchema::constructorsOf(
		const QString &type) const {
	auto result = std::vector<const TlCombinator*>();
	const auto i = _constructorsByType.find(type);
	if (i != _constructorsByType.end()) {
		for (const auto index : i->second) {
			result.push_back(&_constructors[index]);
		}
	}
	return result;
}

std::optional<TlSchema> ParseTlSchema(const QByteArray &text, QString *error) {
	auto schema = TlSchema();
	auto functions = false;
	const auto lines = QString::fromUtf8(text).split(u'\n');
	const auto fail = [&](int index, const QString &message) {
		if (error) {
			*error = u"Line %1: %2."_q.arg(index + 1).arg(message);
		}
		return std::optional<TlSchema>();
	};
	for (auto i = 0, count = int(lines.size()); i != count; ++i) {
		const auto line = lines[i].trimmed();
		if (line.isEmpty()) {
			continue;
		} else if (line.startsWith(u"//")) {
			if (line.startsWith(u"// LAYER ")) {
				schema._layer = line.mid(9).trimmed().toInt();
			}
			continue;
		} else if (line == u"---types---") {
			functions = false;
			continue;
		} else if (line == u"---functions---") {
			functions = true;
			continue;
		}
		const auto tokens = QString(line).replace(u'\t', u' ').split(
			u' ',
			Qt::SkipEmptyParts);
		const auto &nameId = tokens[0];
		const auto hash = nameId.indexOf(u'#');
		if (hash < 0) {
			continue;
		}
		const auto name = nameId.left(hash);
		const auto idString = nameId.mid(hash + 1);
		if (!IsHexId(idString)) {
			continue;
		} else if (name == u"vector") {
			continue;
		}
		const auto id = uint32(idString.toUInt(nullptr, 16));
		const auto eq = tokens.indexOf(u"=");
		if (eq < 1) {
			return fail(i, u"no result"_q);
		}
		auto result = QString();
		for (auto t = eq + 1; t != tokens.size(); ++t) {
			result += (t == eq + 1 ? QString() : u" "_q) + tokens[t];
		}
		if (result.endsWith(u';')) {
			result.chop(1);
		}
		if (result.isEmpty()) {
			return fail(i, u"no result"_q);
		}
		auto params = std::vector<TlParam>();
		auto generic = false;
		for (auto t = 1; t != eq; ++t) {
			const auto token = tokens[t];
			if (token.startsWith(u'{')) {
				generic = true;
				continue;
			}
			const auto colon = token.indexOf(u':');
			if (colon <= 0 || colon == token.size() - 1) {
				return fail(i, u"bad parameter %1"_q.arg(token));
			}
			auto type = token.mid(colon + 1);
			auto flagParam = -1;
			auto flagBit = -1;
			const auto q = type.indexOf(u'?');
			if (q >= 0) {
				const auto prefix = type.left(q);
				const auto dot = prefix.lastIndexOf(u'.');
				if (dot <= 0) {
					return fail(i, u"bad parameter %1"_q.arg(token));
				}
				const auto flagName = prefix.left(dot);
				const auto bitText = prefix.mid(dot + 1);
				if (!IsDigits(bitText) || bitText.toInt() > 31) {
					return fail(i, u"bad parameter %1"_q.arg(token));
				}
				auto found = -1;
				for (auto p = 0, till = int(params.size()); p != till; ++p) {
					if (params[p].name == flagName) {
						found = p;
						break;
					}
				}
				if (found < 0 || params[found].type != u"#") {
					return fail(
						i,
						u"flag %1 names an unknown parameter"_q.arg(flagName));
				}
				flagParam = found;
				flagBit = bitText.toInt();
				type = type.mid(q + 1);
			}
			params.push_back({ token.left(colon), type, flagParam, flagBit });
		}
		auto combinator = TlCombinator();
		combinator.name = name;
		combinator.id = id;
		combinator.params = std::move(params);
		combinator.result = result;
		combinator.function = functions;
		combinator.generic = generic;
		if (functions) {
			schema._functionByName[name] = int(schema._functions.size());
			schema._functions.push_back(std::move(combinator));
		} else {
			schema._constructorById[id] = int(schema._constructors.size());
			schema._constructorByName[name] = int(schema._constructors.size());
			schema._constructorsByType[result].push_back(
				int(schema._constructors.size()));
			schema._constructors.push_back(std::move(combinator));
		}
	}
	return schema;
}

bool TlIsBuiltinType(const QString &type) {
	return (type == u"int")
		|| (type == u"long")
		|| (type == u"double")
		|| (type == u"string")
		|| (type == u"bytes")
		|| (type == u"int128")
		|| (type == u"int256")
		|| (type == u"Bool")
		|| (type == u"true")
		|| (type == u"#");
}

std::optional<QString> TlVectorItem(const QString &type) {
	if (!type.endsWith(u'>')) {
		return std::nullopt;
	} else if (type.startsWith(u"Vector<") && type.size() > 8) {
		return type.mid(7, type.size() - 8);
	} else if (type.startsWith(u"vector<") && type.size() > 8) {
		return type.mid(7, type.size() - 8);
	}
	return std::nullopt;
}

bool TlIsBareType(const QString &type) {
	if (TlIsBuiltinType(type)) {
		return type != u"Bool";
	}
	if (TlVectorItem(type)) {
		return type.startsWith(u"vector<");
	}
	const auto dot = type.lastIndexOf(u'.');
	const auto name = (dot >= 0) ? type.mid(dot + 1) : type;
	return !name.isEmpty() && name[0].isLower();
}

} // namespace Agent