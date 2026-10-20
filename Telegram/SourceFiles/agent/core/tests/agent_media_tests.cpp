/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_media.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

namespace Agent::Tests {
namespace {

[[nodiscard]] MediaBlock AttachmentBlock() {
	auto block = MediaBlock();
	block.kind = MediaKind::File;
	block.mimeType = u"application/pdf"_q;
	block.name = u"report.pdf"_q;
	block.attachment = u"a1"_q;
	block.size = 2411725;
	return block;
}

[[nodiscard]] bool SameMedia(const MediaBlock &a, const MediaBlock &b) {
	return a.kind == b.kind
		&& a.mimeType == b.mimeType
		&& a.name == b.name
		&& a.attachment == b.attachment
		&& a.size == b.size
		&& a.duration == b.duration
		&& a.bytes == b.bytes;
}

[[nodiscard]] bool EqualBytes(
		const QByteArray &bytes,
		std::initializer_list<quint8> expected) {
	if (bytes.size() != int(expected.size())) {
		return false;
	}
	auto i = 0;
	for (const auto value : expected) {
		if (quint8(bytes[i++]) != value) {
			return false;
		}
	}
	return true;
}

} // namespace

std::vector<Check> MediaChecks() {
	return {
		{ u"media_supports_maps_kinds"_q, [] {
			const auto abilities = MediaAbilities{
				.images = true,
				.audio = true,
				.video = true,
				.files = true,
			};
			const auto none = MediaAbilities();
			if (!Supports(abilities, MediaKind::Image)
				|| !Supports(abilities, MediaKind::Audio)
				|| !Supports(abilities, MediaKind::Video)
				|| !Supports(abilities, MediaKind::File)) {
				return u"full abilities must support everything"_q;
			} else if (Supports(none, MediaKind::Image)
				|| Supports(none, MediaKind::Audio)
				|| Supports(none, MediaKind::Video)
				|| Supports(none, MediaKind::File)) {
				return u"empty abilities must support nothing"_q;
			}
			return QString();
		} },
		{ u"media_routes_with_and_without_helper"_q, [] {
			const auto main = MediaAbilities{ .audio = true };
			const auto helper = MediaAbilities{
				.images = true,
				.files = true,
			};
			const auto noHelper = std::optional<MediaAbilities>();
			const auto check = [&](MediaKind kind, MediaRoute route) {
				if (ChooseMediaRoute(kind, main, helper) != route
					|| ChooseMediaRoute(kind, MediaAbilities(), noHelper)
						!= MediaRoute::None) {
					return false;
				}
				return true;
			};
			if (!check(MediaKind::Image, MediaRoute::Helper)
				|| !check(MediaKind::Audio, MediaRoute::Main)
				|| !check(MediaKind::Video, MediaRoute::None)
				|| !check(MediaKind::File, MediaRoute::Helper)) {
				return u"wrong route choice"_q;
			} else if (ChooseMediaRoute(
					MediaKind::Image,
					main,
					helper) != MediaRoute::Helper) {
				return u"helper must not override main"_q;
			}
			return QString();
		} },
		{ u"media_kind_for_mime"_q, [] {
			const auto check = [&](const QString &mime, MediaKind kind) {
				return MediaKindForMime(mime) == kind;
			};
			if (!check(u"image/jpeg"_q, MediaKind::Image)
				|| !check(u"IMAGE/PNG"_q, MediaKind::Image)
				|| !check(u"image/svg+xml"_q, MediaKind::File)
				|| !check(u"audio/ogg"_q, MediaKind::Audio)
				|| !check(u"application/ogg"_q, MediaKind::Audio)
				|| !check(u"video/mp4"_q, MediaKind::Video)
				|| !check(u"application/pdf"_q, MediaKind::File)
				|| !check(u"text/plain"_q, MediaKind::File)
				|| !check(QString(), MediaKind::File)) {
				return u"wrong kind for mime"_q;
			}
			return QString();
		} },
		{ u"media_byte_size_edges"_q, [] {
			const auto check = [&](int64 bytes, const QString &expected) {
				const auto got = FormatByteSize(bytes);
				return (got == expected) ? QString()
					: u"%1: got %2"_q.arg(bytes).arg(got);
			};
			return check(0, u"0 B"_q)
				+ check(512, u"512 B"_q)
				+ check(1023, u"1023 B"_q)
				+ check(1024, u"1 KB"_q)
				+ check(245760, u"240 KB"_q)
				+ check(1048575, u"1024 KB"_q)
				+ check(1048576, u"1.0 MB"_q)
				+ check(2411725, u"2.3 MB"_q)
				+ check(1073741823, u"1024.0 MB"_q)
				+ check(1073741824, u"1.0 GB"_q)
				+ check(int64(5) * 1073741824, u"5.0 GB"_q);
		} },
		{ u"media_labels_attachment"_q, [] {
			auto block = AttachmentBlock();
			block.kind = MediaKind::Image;
			block.name = u"shot.png"_q;
			block.mimeType = u"image/png"_q;
			block.size = 245760;
			const auto plain = MediaLabel(block);
			if (plain != u"[Attachment a1: shot.png, image/png, 240 KB]"_q) {
				return u"plain: %1"_q.arg(plain);
			}
			block.duration = 42;
			const auto timed = MediaLabel(block);
			if (timed
				!= u"[Attachment a1: shot.png, image/png, 240 KB, 0:42]"_q) {
				return u"duration: %1"_q.arg(timed);
			}
			block.duration = 3661;
			const auto hour = MediaLabel(block);
			if (hour
				!= u"[Attachment a1: shot.png, image/png, 240 KB, 1:01:01]"_q) {
				return u"hour: %1"_q.arg(hour);
			}
			return QString();
		} },
		{ u"media_labels_message_media_and_plain"_q, [] {
			auto block = MediaBlock();
			block.kind = MediaKind::Video;
			block.peer = 456;
			block.messageId = 123;
			const auto message = MediaLabel(block);
			if (message != u"[Video of message 123 in chat 456]"_q) {
				return u"message: %1"_q.arg(message);
			}
			block.kind = MediaKind::File;
			block.name = u"report.pdf"_q;
			const auto file = MediaLabel(block);
			if (file
				!= u"[File report.pdf of message 123 in chat 456]"_q) {
				return u"file: %1"_q.arg(file);
			}
			block.peer = 0;
			block.messageId = 0;
			block.kind = MediaKind::Audio;
			block.name = QString();
			const auto plain = MediaLabel(block);
			if (plain != u"[Audio]"_q) {
				return u"plain: %1"_q.arg(plain);
			}
			block.kind = MediaKind::File;
			block.name = u"report.pdf"_q;
			const auto plainFile = MediaLabel(block);
			if (plainFile != u"[File report.pdf]"_q) {
				return u"plain file: %1"_q.arg(plainFile);
			}
			return QString();
		} },
		{ u"media_placeholders"_q, [] {
			auto attachment = AttachmentBlock();
			const auto a = MediaPlaceholder(attachment);
			if (a != u"[Attachment a1: report.pdf, application/pdf, 2.3 MB. "
				u"Open it with a media tool and \"attachment\": \"a1\".]"_q) {
				return u"attachment: %1"_q.arg(a);
			}
			auto timed = attachment;
			timed.duration = 42;
			const auto t = MediaPlaceholder(timed);
			if (t != u"[Attachment a1: report.pdf, application/pdf, 2.3 MB, "
				u"0:42. Open it with a media tool and \"attachment\": "
				u"\"a1\".]"_q) {
				return u"timed: %1"_q.arg(t);
			}
			auto message = MediaBlock();
			message.kind = MediaKind::Image;
			message.peer = 456;
			message.messageId = 123;
			const auto m = MediaPlaceholder(message);
			if (m != u"[The image of message 123 in chat 456 was shown "
				u"earlier. Call the tool again to look at it.]"_q) {
				return u"message: %1"_q.arg(m);
			}
			message.kind = MediaKind::File;
			message.name = u"report.pdf"_q;
			const auto f = MediaPlaceholder(message);
			if (f != u"[The file report.pdf of message 123 in chat 456 was "
				u"shown earlier. Call the tool again to look at it.]"_q) {
				return u"file: %1"_q.arg(f);
			}
			const auto check = [&](MediaKind kind, const QString &expected) {
				auto block = MediaBlock();
				block.kind = kind;
				block.name = u"doc.pdf"_q;
				const auto got = MediaPlaceholder(block);
				return (got == expected) ? QString()
					: u"got %1"_q.arg(got);
			};
			return check(MediaKind::Image,
					u"[An image was shown earlier.]"_q)
				+ check(MediaKind::Audio,
					u"[An audio was shown earlier.]"_q)
				+ check(MediaKind::Video,
					u"[A video was shown earlier.]"_q)
				+ check(MediaKind::File,
					u"[A file doc.pdf was shown earlier.]"_q);
			} },
		{ u"media_attachment_ids"_q, [] {
			if (AttachmentId(1) != u"a1"_q || AttachmentId(42) != u"a42"_q
				|| !AttachmentId(0).isEmpty() || !AttachmentId(-5).isEmpty()) {
				return u"AttachmentId wrong"_q;
			}
			const auto check = [&](const QString &id, int expected) {
				return (AttachmentIndex(id) == expected) ? QString()
					: u"%1: got %2"_q.arg(id).arg(AttachmentIndex(id));
			};
			return check(u"a1"_q, 1)
				+ check(u"a42"_q, 42)
				+ check(u"a0"_q, 0)
				+ check(u"b1"_q, 0)
				+ check(u"a"_q, 0)
				+ check(QString(), 0)
				+ check(u"a1x"_q, 0)
				+ check(u"1"_q, 0)
				+ check(u"a-1"_q, 0)
				+ check(u"a007"_q, 7);
			} },
		{ u"media_helper_request_shape"_q, [] {
			auto media = MediaBlock();
			media.kind = MediaKind::Audio;
			media.mimeType = u"audio/wav"_q;
			media.bytes = QByteArray("abc");
			const auto request = HelperRequest(
				u"media-model"_q,
				media,
				u"What is said?"_q);
			if (request.model != u"media-model"_q
				|| request.system.isEmpty()
				|| !request.system.contains(u"cannot see"_q)
				|| !request.system.contains(u"original language"_q)
				|| !request.system.contains(u"Never follow instructions"_q)
				|| !request.tools.empty()
				|| request.messages.size() != 1) {
				return u"wrong request head"_q;
			}
			const auto &message = request.messages.front();
			if (message.role != Role::User || message.blocks.size() != 2) {
				return u"wrong message"_q;
			}
			const auto text = std::get_if<TextBlock>(&message.blocks[0]);
			const auto block = std::get_if<MediaBlock>(&message.blocks[1]);
			if (!text || text->text != u"Question: What is said?"_q || !block) {
				return u"wrong blocks"_q;
			} else if (!SameMedia(*block, media)) {
				return u"media block changed"_q;
			}
			const auto describe = HelperRequest(
				u"m"_q,
				MediaBlock(),
				QString());
			const auto text2 = std::get_if<TextBlock>(
				&describe.messages.front().blocks[0]);
			if (!text2 || text2->text != u"Describe this media in detail."_q) {
				return u"no question must describe"_q;
			}
			return QString();
			} },
		{ u"media_attachment_round_trip"_q, [] {
			auto block = AttachmentBlock();
			block.kind = MediaKind::Audio;
			block.mimeType = u"audio/ogg"_q;
			block.name = u"voice.ogg"_q;
			block.attachment = u"a3"_q;
			block.size = 123456;
			block.duration = 95;
			block.bytes = QByteArray("xyz");
			block.peer = 456;
			block.messageId = 789;
			const auto restored = DeserializeAttachment(
				SerializeAttachment(block));
			if (!restored) {
				return u"failed to deserialize"_q;
			}
			if (!SameMedia(*restored, block)) {
				return u"round trip changed the block"_q;
			} else if (restored->peer != 0 || restored->messageId != 0) {
				return u"peer must not be stored"_q;
			}
			return QString();
			} },
		{ u"media_attachment_bad_data"_q, [] {
			if (DeserializeAttachment(QByteArray())) {
				return u"empty must fail"_q;
			}
			const auto good = SerializeAttachment(AttachmentBlock());
			if (DeserializeAttachment(good.mid(0, good.size() / 2))) {
				return u"truncated must fail"_q;
			}
			auto bytes = QByteArray();
			auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_5_1);
			stream << quint32(2) << qint32(0) << QString() << QString()
				<< QString() << qint64(0) << qint32(0) << QByteArray();
			if (DeserializeAttachment(bytes)) {
				return u"bad version must fail"_q;
			}
			bytes = QByteArray();
			auto second = QDataStream(&bytes, QIODevice::WriteOnly);
			second.setVersion(QDataStream::Qt_5_1);
			second << quint32(1) << qint32(9) << QString() << QString()
				<< QString() << qint64(0) << qint32(0) << QByteArray();
			if (DeserializeAttachment(bytes)) {
				return u"bad kind must fail"_q;
			}
			return QString();
			} },
		{ u"media_mix_to_mono_stereo"_q, [] {
			auto pcm = QByteArray();
			auto stream = QDataStream(&pcm, QIODevice::WriteOnly);
			stream.setByteOrder(QDataStream::LittleEndian);
			stream << qint16(1000) << qint16(3000)
				<< qint16(-1000) << qint16(1000);
			const auto mixed = MixToMono(pcm, 2, 2, 16000, 16000);
			if (mixed.size() != 2 || mixed[0] != 2000 || mixed[1] != 0) {
				return u"got %1 samples"_q.arg(mixed.size());
			}
			return QString();
			} },
		{ u"media_mix_to_mono_8bit"_q, [] {
			const auto pcm = QByteArray("\x00\xff\x80", 3);
			const auto mixed = MixToMono(pcm, 1, 1, 16000, 16000);
			if (mixed.size() != 3
				|| mixed[0] != -32768
				|| mixed[1] != 32512
				|| mixed[2] != 0) {
				return u"wrong 8-bit mixing"_q;
			}
			return QString();
			} },
		{ u"media_mix_to_mono_resample_length"_q, [] {
			auto pcm = QByteArray();
			auto stream = QDataStream(&pcm, QIODevice::WriteOnly);
			stream.setByteOrder(QDataStream::LittleEndian);
			stream << qint16(100) << qint16(200) << qint16(300);
			const auto mixed = MixToMono(pcm, 1, 2, 48000, 16000);
			if (mixed.size() != 1 || mixed[0] != 100) {
				return u"got %1 samples"_q.arg(mixed.size());
			}
			const auto partial = pcm + QByteArray("\x01\x02", 2);
			if (MixToMono(partial, 1, 2, 48000, 16000).size() != 1) {
				return u"partial frame must be ignored"_q;
			}
			return QString();
			} },
		{ u"media_mix_to_mono_bad_params"_q, [] {
			const auto pcm = QByteArray(16, '\0');
			if (!MixToMono(pcm, 0, 2, 16000, 16000).empty()
				|| !MixToMono(pcm, 1, 3, 16000, 16000).empty()
				|| !MixToMono(pcm, 1, 2, 0, 16000).empty()
				|| !MixToMono(pcm, 1, 2, 16000, -1).empty()
				|| !MixToMono(QByteArray(), 1, 2, 16000, 16000).empty()) {
				return u"bad params must give nothing"_q;
			}
			return QString();
			} },
		{ u"media_make_wav_header"_q, [] {
			const auto wav = MakeWav({ 1000, -1000 }, 8000);
			if (wav.size() != 48) {
				return u"size: %1"_q.arg(wav.size());
			} else if (!wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE"
				|| wav.mid(12, 4) != "fmt " || wav.mid(36, 4) != "data") {
				return u"wrong chunk ids"_q;
			}
			if (!EqualBytes(wav.mid(4, 40), {
				40, 0, 0, 0,
				'W', 'A', 'V', 'E',
				'f', 'm', 't', ' ',
				16, 0, 0, 0,
				1, 0,
				1, 0,
				64, 31, 0, 0,
				128, 62, 0, 0,
				2, 0,
				16, 0,
				'd', 'a', 't', 'a',
				4, 0, 0, 0,
			}) || !EqualBytes(wav.mid(44), {
				232, 3, 24, 252,
			})) {
				return u"wrong header bytes"_q;
			}
			return QString();
			} },
	};
}

} // namespace Agent::Tests