/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_documents.h"

#include "base/flat_set.h"

#include <QtCore/QXmlStreamReader>

#include <algorithm>
#include <optional>
#include <utility>

namespace Agent {

namespace {

const base::flat_set<QString> &TextExtensions() {
	static const auto result = base::flat_set<QString>{
		u"txt"_q, u"md"_q, u"markdown"_q, u"csv"_q, u"tsv"_q,
		u"json"_q, u"jsonl"_q, u"xml"_q, u"html"_q, u"htm"_q,
		u"yaml"_q, u"yml"_q, u"toml"_q, u"ini"_q, u"cfg"_q,
		u"conf"_q, u"log"_q, u"sql"_q, u"sh"_q, u"bat"_q,
		u"ps1"_q, u"py"_q, u"js"_q, u"ts"_q, u"tsx"_q,
		u"jsx"_q, u"c"_q, u"h"_q, u"cpp"_q, u"hpp"_q,
		u"cc"_q, u"cs"_q, u"java"_q, u"kt"_q, u"go"_q,
		u"rs"_q, u"rb"_q, u"php"_q, u"swift"_q, u"m"_q,
		u"mm"_q, u"lua"_q, u"pl"_q, u"r"_q, u"tex"_q,
		u"srt"_q, u"vtt"_q, u"svg"_q, u"css"_q, u"scss"_q,
	};
	return result;
}

[[nodiscard]] bool ValidUtf8(const QByteArray &bytes) {
	const auto data = bytes.constData();
	const auto size = int(bytes.size());
	auto i = 0;
	while (i < size) {
		const auto first = quint8(data[i]);
		auto need = 0;
		auto code = 0u;
		if (first < 0x80) {
			++i;
			continue;
		} else if ((first & 0xE0) == 0xC0) {
			need = 1;
			code = first & 0x1F;
		} else if ((first & 0xF0) == 0xE0) {
			need = 2;
			code = first & 0x0F;
		} else if ((first & 0xF8) == 0xF0) {
			need = 3;
			code = first & 0x07;
		} else {
			return false;
		}
		if (size - i - 1 < need) {
			return false;
		}
		for (auto k = 1; k <= need; ++k) {
			const auto next = quint8(data[i + k]);
			if ((next & 0xC0) != 0x80) {
				return false;
			}
			code = (code << 6) | (next & 0x3F);
		}
		if ((need == 1 && code < 0x80)
			|| (need == 2 && code < 0x800)
			|| (need == 3 && code < 0x10000)
			|| (code >= 0xD800 && code <= 0xDFFF)
			|| code > 0x10FFFF) {
			return false;
		}
		i += need + 1;
	}
	return true;
}

[[nodiscard]] std::optional<QString> DecodeUtf16(
		const QByteArray &bytes,
		bool littleEndian) {
	if (bytes.size() % 2 != 0) {
		return std::nullopt;
	}
	auto result = QString();
	result.reserve(bytes.size() / 2);
	const auto unitAt = [&](int offset) {
		const auto first = quint16(quint8(bytes[offset]));
		const auto second = quint16(quint8(bytes[offset + 1]));
		return littleEndian ? (first | (second << 8)) : ((first << 8) | second);
	};
	auto i = 0;
	while (i < bytes.size()) {
		const auto first = unitAt(i);
		i += 2;
		if (first >= 0xD800 && first <= 0xDBFF) {
			if (i + 2 > bytes.size()) {
				return std::nullopt;
			}
			const auto second = unitAt(i);
			i += 2;
			if (second < 0xDC00 || second > 0xDFFF) {
				return std::nullopt;
			}
			const auto rest
				= (first - 0xD800) * 0x400 + (second - 0xDC00);
			result.append(QChar(ushort(0xD800 + (rest >> 10))));
			result.append(QChar(ushort(0xDC00 + (rest & 0x3FF))));
		} else if (first >= 0xDC00 && first <= 0xDFFF) {
			return std::nullopt;
		} else {
			result.append(QChar(ushort(first)));
		}
	}
	return result;
}

[[nodiscard]] QString AttributeValue(
		const QXmlStreamReader &reader,
		const QString &name) {
	for (const auto &attribute : reader.attributes()) {
		if (attribute.qualifiedName().toString() == name) {
			return attribute.value().toString();
		}
	}
	return QString();
}

[[nodiscard]] std::optional<int> NumberedSuffix(
		const QString &path,
		const QString &prefix,
		const QString &suffix) {
	if (!path.startsWith(prefix) || !path.endsWith(suffix)) {
		return std::nullopt;
	}
	const auto middle = path.mid(
		prefix.size(),
		path.size() - prefix.size() - suffix.size());
	auto ok = false;
	const auto number = middle.toInt(&ok);
	return (ok && middle.size() > 0) ? std::optional<int>(number) : std::nullopt;
}

} // namespace

DocumentKind DetectDocument(const QString &name, const QString &mimeType) {
	const auto dot = name.lastIndexOf(u'.');
	if (dot >= 0 && TextExtensions().contains(name.mid(dot + 1).toLower())) {
		return DocumentKind::Text;
	} else if (dot >= 0) {
		const auto ext = name.mid(dot + 1).toLower();
		if (ext == u"docx"_q) {
			return DocumentKind::Docx;
		} else if (ext == u"xlsx"_q) {
			return DocumentKind::Xlsx;
		} else if (ext == u"pptx"_q) {
			return DocumentKind::Pptx;
		} else if (ext == u"odt"_q || ext == u"ods"_q || ext == u"odp"_q) {
			return DocumentKind::OpenDocument;
		} else if (ext == u"pdf"_q) {
			return DocumentKind::Pdf;
		}
	}
	const auto mime = mimeType.toLower();
	if (mime.startsWith(u"text/"_q)
		|| mime == u"application/json"_q
		|| mime == u"application/xml"_q
		|| mime == u"application/x-sh"_q) {
		return DocumentKind::Text;
	} else if (mime == u"application/pdf"_q) {
		return DocumentKind::Pdf;
	} else if (mime == u"application/vnd.openxmlformats-officedocument."
		u"wordprocessingml.document"_q) {
		return DocumentKind::Docx;
	} else if (mime == u"application/vnd.openxmlformats-officedocument."
		u"spreadsheetml.sheet"_q) {
		return DocumentKind::Xlsx;
	} else if (mime == u"application/vnd.openxmlformats-officedocument."
		u"presentationml.presentation"_q) {
		return DocumentKind::Pptx;
	} else if (mime.startsWith(u"application/vnd.oasis.opendocument."_q)) {
		return DocumentKind::OpenDocument;
	}
	return DocumentKind::Other;
}

bool IsOfficeDocument(DocumentKind kind) {
	switch (kind) {
	case DocumentKind::Docx:
	case DocumentKind::Xlsx:
	case DocumentKind::Pptx:
	case DocumentKind::OpenDocument:
		return true;
	default:
		return false;
	}
}

std::optional<QString> DecodeText(const QByteArray &bytes) {
	auto text = std::optional<QString>();
	if (bytes.size() >= 2
		&& quint8(bytes[0]) == 0xFF
		&& quint8(bytes[1]) == 0xFE) {
		text = DecodeUtf16(bytes.mid(2), true);
	} else if (bytes.size() >= 2
		&& quint8(bytes[0]) == 0xFE
		&& quint8(bytes[1]) == 0xFF) {
		text = DecodeUtf16(bytes.mid(2), false);
	} else {
		auto data = bytes;
		if (data.startsWith("\xEF\xBB\xBF")) {
			data.remove(0, 3);
		}
		if (!ValidUtf8(data)) {
			return std::nullopt;
		}
		text = QString::fromUtf8(data);
	}
	if (!text || text->contains(QChar(0))) {
		return std::nullopt;
	}
	text->replace(u"\r\n"_q, u"\n"_q);
	return text;
}

bool WantOfficeEntry(DocumentKind kind, const QString &path) {
	switch (kind) {
	case DocumentKind::Docx:
		return path == u"word/document.xml"_q;
	case DocumentKind::Xlsx:
		return path == u"xl/workbook.xml"_q
			|| path == u"xl/sharedStrings.xml"_q
			|| NumberedSuffix(path, u"xl/worksheets/sheet"_q, u".xml"_q)
				.has_value();
	case DocumentKind::Pptx:
		return NumberedSuffix(path, u"ppt/slides/slide"_q, u".xml"_q)
			.has_value();
	case DocumentKind::OpenDocument:
		return path == u"content.xml"_q;
	default:
		return false;
	}
}

namespace {

void ExtractDocx(const QByteArray &bytes, QStringList &to) {
	auto reader = QXmlStreamReader(bytes);
	auto current = QString();
	auto inParagraph = false;
	auto inText = false;
	while (!reader.atEnd()) {
		const auto token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"w:p"_q) {
				inParagraph = true;
				current = QString();
			} else if (name == u"w:t"_q) {
				inText = true;
			} else if (inParagraph && name == u"w:tab"_q) {
				current += u'\t';
			} else if (inParagraph && name == u"w:br"_q) {
				current += u'\n';
			}
		} else if (token == QXmlStreamReader::EndElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"w:p"_q) {
				to.push_back(current);
				inParagraph = false;
			} else if (name == u"w:t"_q) {
				inText = false;
			}
		} else if (token == QXmlStreamReader::Characters
			&& inParagraph
			&& inText) {
			current += reader.text().toString();
		}
	}
}

void ExtractSharedStrings(const QByteArray &bytes, QStringList &to) {
	auto reader = QXmlStreamReader(bytes);
	auto current = QString();
	auto inString = false;
	auto inText = false;
	while (!reader.atEnd()) {
		const auto token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"si"_q) {
				inString = true;
				current = QString();
			} else if (inString && name == u"t"_q) {
				inText = true;
			}
		} else if (token == QXmlStreamReader::EndElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"t"_q) {
				inText = false;
			} else if (name == u"si"_q) {
				to.push_back(current);
				inString = false;
			}
		} else if (token == QXmlStreamReader::Characters
			&& inString
			&& inText) {
			current += reader.text().toString();
		}
	}
}

void ExtractWorkbook(const QByteArray &bytes, QStringList &to) {
	auto reader = QXmlStreamReader(bytes);
	while (!reader.atEnd()) {
		if (reader.readNext() == QXmlStreamReader::StartElement
			&& reader.qualifiedName().toString() == u"sheet"_q) {
			const auto name = AttributeValue(reader, u"name"_q);
			if (!name.isEmpty()) {
				to.push_back(name);
			}
		}
	}
}

void ExtractWorksheet(
		const QByteArray &bytes,
		const QStringList &shared,
		QStringList &to) {
	auto reader = QXmlStreamReader(bytes);
	auto rowCells = QStringList();
	auto cellType = QString();
	auto cellText = QString();
	auto inValue = false;
	auto inString = false;
	auto inText = false;
	while (!reader.atEnd()) {
		const auto token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"row"_q) {
				rowCells = QStringList();
			} else if (name == u"c"_q) {
				cellType = AttributeValue(reader, u"t"_q);
				cellText = QString();
			} else if (name == u"v"_q) {
				inValue = true;
			} else if (name == u"is"_q) {
				inString = true;
			} else if (inString && name == u"t"_q) {
				inText = true;
			}
		} else if (token == QXmlStreamReader::EndElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"v"_q) {
				inValue = false;
			} else if (name == u"t"_q) {
				inText = false;
			} else if (name == u"is"_q) {
				inString = false;
			} else if (name == u"c"_q) {
				auto text = cellText;
				if (cellType == u"s"_q) {
					auto ok = false;
					const auto index = cellText.toInt(&ok);
					text = (ok && index >= 0 && index < shared.size())
						? shared[index]
						: QString();
				}
				rowCells.push_back(text);
			} else if (name == u"row"_q) {
				to.push_back(rowCells.join(u'\t'));
			}
		} else if (token == QXmlStreamReader::Characters) {
			if (inValue || (inString && inText)) {
				cellText += reader.text().toString();
			}
		}
	}
}

QString ExtractXlsx(const std::vector<OfficeEntry> &entries) {
	auto shared = QStringList();
	auto names = QStringList();
	for (const auto &entry : entries) {
		if (entry.path == u"xl/sharedStrings.xml"_q) {
			ExtractSharedStrings(entry.bytes, shared);
		} else if (entry.path == u"xl/workbook.xml"_q) {
			ExtractWorkbook(entry.bytes, names);
		}
	}
	auto lines = QStringList();
	for (auto i = 0; i != names.size(); ++i) {
		lines.push_back(u"# %1"_q.arg(names[i]));
		const auto path = u"xl/worksheets/sheet%1.xml"_q.arg(i + 1);
		for (const auto &entry : entries) {
			if (entry.path == path) {
				ExtractWorksheet(entry.bytes, shared, lines);
			}
		}
	}
	return lines.join(u'\n');
}

void ExtractSlide(const QByteArray &bytes, QStringList &to) {
	auto reader = QXmlStreamReader(bytes);
	auto current = QString();
	auto inParagraph = false;
	auto inText = false;
	while (!reader.atEnd()) {
		const auto token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"a:p"_q) {
				inParagraph = true;
				current = QString();
			} else if (inParagraph && name == u"a:t"_q) {
				inText = true;
			}
		} else if (token == QXmlStreamReader::EndElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"a:p"_q) {
				to.push_back(current);
				inParagraph = false;
			} else if (name == u"a:t"_q) {
				inText = false;
			}
		} else if (token == QXmlStreamReader::Characters
			&& inParagraph
			&& inText) {
			current += reader.text().toString();
		}
	}
}

QString ExtractPptx(const std::vector<OfficeEntry> &entries) {
	auto slides = std::vector<std::pair<int, const OfficeEntry *>>();
	for (const auto &entry : entries) {
		const auto number = NumberedSuffix(
			entry.path,
			u"ppt/slides/slide"_q,
			u".xml"_q);
		if (number) {
			slides.push_back({ *number, &entry });
		}
	}
	std::stable_sort(slides.begin(), slides.end(), [](auto &&a, auto &&b) {
		return a.first < b.first;
	});
	auto lines = QStringList();
	for (const auto &[number, entry] : slides) {
		lines.push_back(u"# Slide %1"_q.arg(number));
		ExtractSlide(entry->bytes, lines);
	}
	return lines.join(u'\n');
}

void ExtractOpenDocument(const QByteArray &bytes, QStringList &to) {
	auto reader = QXmlStreamReader(bytes);
	auto paragraph = QString();
	auto inParagraph = false;
	auto cell = QString();
	auto inCell = false;
	auto rowCells = QStringList();
	const auto target = [&]() -> QString * {
		return inParagraph ? &paragraph : inCell ? &cell : nullptr;
	};
	while (!reader.atEnd()) {
		const auto token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"text:p"_q || name == u"text:h"_q) {
				inParagraph = true;
				paragraph = QString();
			} else if (name == u"text:tab"_q) {
				if (auto where = target()) {
					*where += u'\t';
				}
			} else if (name == u"text:s"_q) {
				auto count = AttributeValue(reader, u"text:c"_q).toInt();
				if (count <= 0) {
					count = 1;
				}
				if (auto where = target()) {
					*where += QString(count, u' ');
				}
			} else if (name == u"text:line-break"_q) {
				if (auto where = target()) {
					*where += u'\n';
				}
			} else if (name == u"table:table-cell"_q) {
				inCell = true;
				cell = QString();
			}
		} else if (token == QXmlStreamReader::EndElement) {
			const auto name = reader.qualifiedName().toString();
			if (name == u"text:p"_q || name == u"text:h"_q) {
				if (inCell) {
					cell += paragraph;
				} else {
					to.push_back(paragraph);
				}
				inParagraph = false;
			} else if (name == u"table:table-cell"_q) {
				rowCells.push_back(cell);
				inCell = false;
			} else if (name == u"table:table-row"_q) {
				to.push_back(rowCells.join(u'\t'));
				rowCells = QStringList();
			}
		} else if (token == QXmlStreamReader::Characters) {
			if (auto where = target()) {
				*where += reader.text().toString();
			}
		}
	}
}

} // namespace

QString ExtractOfficeText(
		DocumentKind kind,
		const std::vector<OfficeEntry> &entries) {
	auto lines = QStringList();
	if (kind == DocumentKind::Docx) {
		for (const auto &entry : entries) {
			if (entry.path == u"word/document.xml"_q) {
				ExtractDocx(entry.bytes, lines);
				break;
			}
		}
	} else if (kind == DocumentKind::Xlsx) {
		return ExtractXlsx(entries);
	} else if (kind == DocumentKind::Pptx) {
		return ExtractPptx(entries);
	} else if (kind == DocumentKind::OpenDocument) {
		for (const auto &entry : entries) {
			if (entry.path == u"content.xml"_q) {
				ExtractOpenDocument(entry.bytes, lines);
				break;
			}
		}
	}
	return lines.join(u'\n');
}

TextPage PageText(const QString &text, int offset, int limit) {
	auto page = TextPage();
	page.total = int(text.size());
	page.offset = std::clamp(offset, 0, page.total);
	if (limit <= 0 || page.offset >= page.total) {
		return page;
	}
	auto result = text.mid(page.offset, limit);
	if (!result.isEmpty()
		&& page.offset + result.size() < page.total
		&& result.back().isHighSurrogate()
		&& text[page.offset + result.size()].isLowSurrogate()) {
		result.chop(1);
	}
	const auto size = int(result.size());
	const auto fifth = size - size / 5;
	const auto breakAt = result.lastIndexOf(u'\n');
	if (breakAt >= fifth
		&& page.offset + size < page.total) {
		result = result.left(breakAt + 1);
	}
	page.text = result;
	page.nextOffset = page.offset + result.size();
	if (page.nextOffset >= page.total) {
		page.nextOffset = -1;
	}
	return page;
}

} // namespace Agent