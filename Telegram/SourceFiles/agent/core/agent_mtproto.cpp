/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_mtproto.h"

#include <algorithm>

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
		int limit) {
	const auto words = QueryWords(query);
	if (words.empty() || limit <= 0) {
		return {};
	}
	struct Candidate {
		const TlCombinator *method = nullptr;
		bool inName = false;
		int index = 0;
	};
	auto candidates = std::vector<Candidate>();
	auto index = 0;
	for (const auto &function : schema.functions()) {
		const auto i = index++;
		if (ClassifyMethod(schema, function.name) == MethodClass::Forbidden) {
			continue;
		}
		auto inName = true;
		auto matches = true;
		for (const auto &word : words) {
			const auto inMethodName = function.name.contains(
				word,
				Qt::CaseInsensitive);
			if (inMethodName) {
				continue;
			}
			inName = false;
			auto found = false;
			for (const auto &param : function.params) {
				if (param.name.contains(word, Qt::CaseInsensitive)
					|| param.type.contains(word, Qt::CaseInsensitive)) {
					found = true;
					break;
				}
			}
			if (!found) {
				matches = false;
				break;
			}
		}
		if (matches) {
			candidates.push_back({ &function, inName, i });
		}
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
		auto result = MethodSignature(*function) + u"\n"_q
			+ ((classification == MethodClass::Read)
				? u"reads, runs without asking.\n"_q
				: u"changes something, the user confirms it on a card.\n"_q)
			+ u"InputPeer, InputUser and InputChannel"
				u" take a chat id or @username.\n"_q;
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