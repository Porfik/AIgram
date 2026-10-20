/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_documents.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] OfficeEntry Entry(const QString &path, const char *xml) {
	return { path, QByteArray(xml) };
}

} // namespace

std::vector<Check> DocumentsChecks() {
	return {
		{ u"documents_detect_by_extension"_q, [] {
			const auto check = [&](const QString &name, DocumentKind kind) {
				return (DetectDocument(name, QString()) == kind)
					? QString()
					: u"%1: got %2"_q.arg(name).arg(int(DetectDocument(
						name,
						QString())));
			};
			return check(u"file.txt"_q, DocumentKind::Text)
				+ check(u"FILE.TXT"_q, DocumentKind::Text)
				+ check(u"notes.md"_q, DocumentKind::Text)
				+ check(u"data.csv"_q, DocumentKind::Text)
				+ check(u"app.py"_q, DocumentKind::Text)
				+ check(u"style.css"_q, DocumentKind::Text)
				+ check(u"page.svg"_q, DocumentKind::Text)
				+ check(u"config.yaml"_q, DocumentKind::Text)
				+ check(u"doc.docx"_q, DocumentKind::Docx)
				+ check(u"BOOK.XLSX"_q, DocumentKind::Xlsx)
				+ check(u"deck.pptx"_q, DocumentKind::Pptx)
				+ check(u"a.odt"_q, DocumentKind::OpenDocument)
				+ check(u"b.ods"_q, DocumentKind::OpenDocument)
				+ check(u"c.odp"_q, DocumentKind::OpenDocument)
				+ check(u"d.pdf"_q, DocumentKind::Pdf)
				+ check(u"archive.bin"_q, DocumentKind::Other);
			} },
		{ u"documents_detect_by_mime"_q, [] {
			const auto check = [&](const QString &mime, DocumentKind kind) {
				return (DetectDocument(u"file"_q, mime) == kind)
					? QString()
					: u"%1: got %2"_q.arg(mime).arg(int(DetectDocument(
						u"file"_q,
						mime)));
			};
			return check(u"text/plain"_q, DocumentKind::Text)
				+ check(u"TEXT/HTML"_q, DocumentKind::Text)
				+ check(u"application/json"_q, DocumentKind::Text)
				+ check(u"application/xml"_q, DocumentKind::Text)
				+ check(u"application/x-sh"_q, DocumentKind::Text)
				+ check(u"application/pdf"_q, DocumentKind::Pdf)
				+ check(u"application/vnd.openxmlformats-officedocument."
					u"wordprocessingml.document"_q, DocumentKind::Docx)
				+ check(u"application/vnd.openxmlformats-officedocument."
					u"spreadsheetml.sheet"_q, DocumentKind::Xlsx)
				+ check(u"application/vnd.openxmlformats-officedocument."
					u"presentationml.presentation"_q, DocumentKind::Pptx)
				+ check(u"application/vnd.oasis.opendocument.text"_q,
					DocumentKind::OpenDocument)
				+ check(u"application/vnd.oasis.opendocument.spreadsheet"_q,
					DocumentKind::OpenDocument)
				+ check(u"application/octet-stream"_q, DocumentKind::Other)
				+ check(QString(), DocumentKind::Other);
			} },
		{ u"documents_extension_wins_over_mime"_q, [] {
			if (DetectDocument(u"notes.txt"_q, u"application/pdf"_q)
				!= DocumentKind::Text) {
				return u"text extension must win over pdf mime"_q;
			} else if (DetectDocument(u"doc.docx"_q, u"text/plain"_q)
				!= DocumentKind::Docx) {
				return u"docx extension must win over text mime"_q;
			} else if (!IsOfficeDocument(DocumentKind::Docx)
				|| !IsOfficeDocument(DocumentKind::Xlsx)
				|| !IsOfficeDocument(DocumentKind::Pptx)
				|| !IsOfficeDocument(DocumentKind::OpenDocument)
				|| IsOfficeDocument(DocumentKind::Text)
				|| IsOfficeDocument(DocumentKind::Pdf)
				|| IsOfficeDocument(DocumentKind::Other)) {
				return u"wrong office kinds"_q;
			}
			return QString();
		} },
		{ u"documents_decode_text"_q, [] {
			const auto check = [&](const QByteArray &bytes) {
				return DecodeText(bytes);
			};
			if (check(QByteArray("hello")).value_or(QString())
				!= u"hello"_q) {
				return u"plain utf-8 failed"_q;
			} else if (check(QByteArray("\xEF\xBB\xBFhello", 8))
					.value_or(QString()) != u"hello"_q) {
				return u"utf-8 bom failed"_q;
			} else if (check(QByteArray("\xFF\xFEh\0i\0", 6))
					.value_or(QString()) != u"hi"_q) {
				return u"utf-16 le failed"_q;
			} else if (check(QByteArray("\xFE\xFF\0h\0i", 6))
					.value_or(QString()) != u"hi"_q) {
				return u"utf-16 be failed"_q;
			} else if (check(QByteArray("\xFF\xFE=\xD8\0\xDE", 6))
					.value_or(QString()) != u"\xD83D\xDE00"_q) {
				return u"utf-16 surrogate failed"_q;
			} else if (check(QByteArray("a\r\nb")).value_or(QString())
				!= u"a\nb"_q) {
				return u"crlf failed"_q;
			}
			if (check(QByteArray("\xFF")) || check(QByteArray("\x80"))
				|| check(QByteArray("a\0b", 3)) || check(QByteArray("\xFF\xFEa", 3))) {
				return u"binary must fail"_q;
			}
			return QString();
		} },
		{ u"documents_want_office_entry"_q, [] {
			const auto yes = [](DocumentKind kind, const QString &path) {
				return WantOfficeEntry(kind, path) ? QString()
					: u"%1 must be wanted"_q.arg(path);
			};
			const auto no = [](DocumentKind kind, const QString &path) {
				return !WantOfficeEntry(kind, path) ? QString()
					: u"%1 must not be wanted"_q.arg(path);
			};
			return yes(DocumentKind::Docx, u"word/document.xml"_q)
				+ no(DocumentKind::Docx, u"word/other.xml"_q)
				+ yes(DocumentKind::Xlsx, u"xl/workbook.xml"_q)
				+ yes(DocumentKind::Xlsx, u"xl/sharedStrings.xml"_q)
				+ yes(DocumentKind::Xlsx, u"xl/worksheets/sheet1.xml"_q)
				+ yes(DocumentKind::Xlsx, u"xl/worksheets/sheet12.xml"_q)
				+ no(DocumentKind::Xlsx, u"xl/worksheets/sheet.xml"_q)
				+ no(DocumentKind::Xlsx, u"xl/worksheets/sheet1a.xml"_q)
				+ yes(DocumentKind::Pptx, u"ppt/slides/slide1.xml"_q)
				+ yes(DocumentKind::Pptx, u"ppt/slides/slide10.xml"_q)
				+ no(DocumentKind::Pptx, u"ppt/slides/slide.xml"_q)
				+ no(DocumentKind::Pptx, u"ppt/notesSlides/slide1.xml"_q)
				+ yes(DocumentKind::OpenDocument, u"content.xml"_q)
				+ no(DocumentKind::OpenDocument, u"styles.xml"_q)
				+ no(DocumentKind::Text, u"anything.txt"_q)
				+ no(DocumentKind::Pdf, u"content.xml"_q);
		} },
		{ u"documents_extract_docx"_q, [] {
			auto lines = ExtractOfficeText(DocumentKind::Docx, {
				Entry(u"word/document.xml"_q,
					"<?xml version=\"1.0\"?>"
					"<w:document xmlns:w=\"urn:doc\">"
					"<w:body>"
					"<w:p><w:r><w:t>Hello</w:t></w:r>"
					"<w:tab/><w:r><w:t>world</w:t></w:r>"
					"<w:br/><w:r><w:t>line</w:t></w:r></w:p>"
					"<w:p><w:r><w:t>Second</w:t></w:r></w:p>"
					"</w:body></w:document>"),
			});
			const auto expected = u"Hello\tworld\nline\nSecond"_q;
			if (lines != expected) {
				return u"got: %1"_q.arg(lines);
			}
			const auto partial = ExtractOfficeText(DocumentKind::Docx, {
				Entry(u"word/document.xml"_q,
					"<w:document xmlns:w=\"urn:doc\">"
					"<w:p><w:t>First</w:t></w:p><broken"),
			});
			if (partial != u"First"_q) {
				return u"broken xml must keep what was read: %1"_q.arg(partial);
			}
			return QString();
			} },
		{ u"documents_extract_xlsx"_q, [] {
			auto text = ExtractOfficeText(DocumentKind::Xlsx, {
				Entry(u"xl/workbook.xml"_q,
					"<workbook><sheets>"
					"<sheet name=\"Alpha\" sheetId=\"1\"/>"
					"<sheet name=\"Beta\" sheetId=\"2\"/>"
					"</sheets></workbook>"),
				Entry(u"xl/sharedStrings.xml"_q,
					"<sst><si><t>one</t></si>"
					"<si><r><t>tw</t></r><r><t>o</t></r></si>"
					"<si><t>three</t></si></sst>"),
				Entry(u"xl/worksheets/sheet1.xml"_q,
					"<worksheet><row>"
					"<c t=\"s\"><v>0</v></c>"
					"<c><v>42</v></c>"
					"<c t=\"inlineStr\"><is><t>inline</t></is></c>"
					"</row><row><c><v>1.5</v></c></row></worksheet>"),
				Entry(u"xl/worksheets/sheet2.xml"_q,
					"<worksheet><row><c t=\"s\"><v>1</v></c></row>"
					"</worksheet>"),
			});
			const auto expected = u"# Alpha\none\t42\tinline\n1.5"
				u"\n# Beta\ntwo"_q;
			if (text != expected) {
				return u"got: %1"_q.arg(text);
			}
			return QString();
			} },
		{ u"documents_extract_pptx"_q, [] {
			auto text = ExtractOfficeText(DocumentKind::Pptx, {
				Entry(u"ppt/slides/slide10.xml"_q,
					"<p:sld xmlns:p=\"urn:p\" xmlns:a=\"urn:a\">"
					"<a:p><a:r><a:t>Second</a:t></a:r></a:p></p:sld>"),
				Entry(u"ppt/slides/slide1.xml"_q,
					"<p:sld xmlns:p=\"urn:p\" xmlns:a=\"urn:a\">"
					"<a:p><a:r><a:t>First</a:t></a:r></a:p>"
					"<a:p><a:r><a:t>two</a:t></a:r></a:p></p:sld>"),
				Entry(u"ppt/slideLayouts/layout1.xml"_q,
					"<a:p><a:r><a:t>ignored</a:t></a:r></a:p>"),
			});
			const auto expected = u"# Slide 1\nFirst\ntwo\n# Slide 10\nSecond"_q;
			if (text != expected) {
				return u"got: %1"_q.arg(text);
			}
			return QString();
			} },
		{ u"documents_extract_open_document"_q, [] {
			auto text = ExtractOfficeText(DocumentKind::OpenDocument, {
				Entry(u"content.xml"_q,
					"<office:document-content"
					" xmlns:text=\"urn:text\" xmlns:table=\"urn:table\""
					" xmlns:office=\"urn:office\">"
					"<office:body><office:text>"
					"<text:p>First<text:tab/>second</text:p>"
					"<text:h>Heading</text:h>"
					"<text:p>sp<text:s text:c=\"3\"/>aces"
					"<text:line-break/>new</text:p>"
					"<table:table>"
					"<table:table-row>"
					"<table:table-cell><text:p>a</text:p></table:table-cell>"
					"<table:table-cell><text:p>b</text:p></table:table-cell>"
					"</table:table-row>"
					"<table:table-row>"
					"<table:table-cell><text:p>c</text:p></table:table-cell>"
					"<table:table-cell><text:p>d</text:p></table:table-cell>"
					"</table:table-row>"
					"</table:table>"
					"</office:text></office:body></office:document-content>"),
			});
			const auto expected = u"First\tsecond\nHeading\nsp   aces\nnew"
				u"\na\tb\nc\td"_q;
			if (text != expected) {
				return u"got: %1"_q.arg(text);
			}
			return QString();
			} },
		{ u"documents_page_text_cut_at_line_break"_q, [] {
			const auto text = u"abcdefghijklmnop\nqrst"_q;
			const auto page = PageText(text, 0, 20);
			if (page.text != u"abcdefghijklmnop\n"_q
				|| page.offset != 0
				|| page.total != 21
				|| page.nextOffset != 17) {
				return u"got: %1 (%2)"_q.arg(page.text).arg(page.nextOffset);
			}
			return QString();
			} },
		{ u"documents_page_text_full_reach_not_cut"_q, [] {
			const auto text = u"abcdefghijklmnop\nqrst"_q;
			const auto page = PageText(text, 0, 21);
			if (page.text != text
				|| page.offset != 0
				|| page.total != 21
				|| page.nextOffset != -1) {
				return u"got: %1 (%2)"_q.arg(page.text).arg(page.nextOffset);
			}
			return QString();
			} },
		{ u"documents_page_text_no_line_break"_q, [] {
			const auto text = u"aaa bbb ccc ddd eee"_q;
			const auto page = PageText(text, 0, 10);
			if (page.text != u"aaa bbb cc"_q || page.nextOffset != 10) {
				return u"got: %1 (%2)"_q.arg(page.text).arg(page.nextOffset);
			}
			return QString();
			} },
		{ u"documents_page_text_surrogate_edge"_q, [] {
			auto text = u"abcdef"_q + QString::fromUtf8("\xF0\x9F\x98\x80")
				+ u"xyz"_q;
			if (text.size() != 11) {
				return u"test text must be 11 chars"_q;
			}
			const auto page = PageText(text, 0, 7);
			if (page.text != u"abcdef"_q || page.nextOffset != 6) {
				return u"got: %1 (%2)"_q.arg(page.text).arg(page.nextOffset);
			}
			return QString();
			} },
		{ u"documents_page_text_edges"_q, [] {
			const auto text = u"abcdefgh"_q;
			const auto past = PageText(text, 999, 5);
			if (!past.text.isEmpty()
				|| past.offset != 8
				|| past.total != 8
				|| past.nextOffset != -1) {
				return u"past end: %1 (%2)"_q.arg(past.text)
					.arg(past.nextOffset);
			}
			const auto last = PageText(text, 5, 5);
			if (last.text != u"fgh"_q || last.nextOffset != -1) {
				return u"last: %1 (%2)"_q.arg(last.text)
					.arg(last.nextOffset);
			}
			const auto negative = PageText(text, -3, 3);
			if (negative.text != u"abc"_q || negative.offset != 0) {
				return u"negative offset must clamp"_q;
			}
			const auto empty = PageText(QString(), 0, 5);
			if (!empty.text.isEmpty() || empty.nextOffset != -1) {
				return u"empty text must end"_q;
			}
			return QString();
			} },
	};
}

} // namespace Agent::Tests

