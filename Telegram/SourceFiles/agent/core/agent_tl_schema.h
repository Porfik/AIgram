/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Agent {

struct TlParam {
	QString name;
	QString type;
	int flagParam = -1;
	int flagBit = -1;
};

struct TlCombinator {
	QString name;
	uint32 id = 0;
	std::vector<TlParam> params;
	QString result;
	bool function = false;
	bool generic = false;
};

class TlSchema final {
public:
	[[nodiscard]] const std::vector<TlCombinator> &constructors() const;
	[[nodiscard]] const std::vector<TlCombinator> &functions() const;
	[[nodiscard]] int layer() const;

	[[nodiscard]] const TlCombinator *constructor(uint32 id) const;
	[[nodiscard]] const TlCombinator *constructor(const QString &name) const;
	[[nodiscard]] const TlCombinator *function(const QString &name) const;
	[[nodiscard]] std::vector<const TlCombinator*> constructorsOf(
		const QString &type) const;

private:
	friend std::optional<TlSchema> ParseTlSchema(
		const QByteArray &text,
		QString *error);

	std::vector<TlCombinator> _constructors;
	std::vector<TlCombinator> _functions;
	base::flat_map<uint32, int> _constructorById;
	base::flat_map<QString, int> _constructorByName;
	base::flat_map<QString, int> _functionByName;
	base::flat_map<QString, std::vector<int>> _constructorsByType;
	int _layer = 0;

};

[[nodiscard]] std::optional<TlSchema> ParseTlSchema(
	const QByteArray &text,
	QString *error = nullptr);

[[nodiscard]] bool TlIsBuiltinType(const QString &type);
[[nodiscard]] std::optional<QString> TlVectorItem(const QString &type);
[[nodiscard]] bool TlIsBareType(const QString &type);

} // namespace Agent
