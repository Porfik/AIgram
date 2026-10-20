/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_media_convert.h"

#include "agent/core/agent_media.h"
#include "base/zlib_help.h"
#include "core/file_location.h"
#include "media/audio/media_audio_ffmpeg_loader.h"
#include "ui/image/image_prepare.h"

#include <QtCore/QBuffer>
#include <QtCore/QIODevice>
#include <QtGui/QImage>
#include <QtGui/QPainter>

#include <algorithm>
#include <variant>

namespace Agent {

QByteArray PrepareImageForModel(const QImage &image) {
	if (image.isNull()) {
		return QByteArray();
	}
	const auto side = std::max(image.width(), image.height());
	const auto scaled = (side > kImageMaxSide)
		? image.scaled(
			kImageMaxSide,
			kImageMaxSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: image;
	auto flat = QImage(scaled.size(), QImage::Format_RGB32);
	flat.fill(Qt::white);
	{
		auto painter = QPainter(&flat);
		painter.drawImage(0, 0, scaled);
	}
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	buffer.open(QIODevice::WriteOnly);
	flat.save(&buffer, "JPEG", 87);
	return result;
}

QByteArray PrepareImageForModel(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return {};
	}
	auto read = Images::Read({ .content = bytes });
	if (read.image.isNull()) {
		return {};
	}
	return PrepareImageForModel(read.image);
}

std::optional<DecodedAudio> DecodeAudio(
		const QByteArray &bytes,
		int fromSeconds,
		int seconds) {
	if (bytes.isEmpty() || seconds <= 0 || fromSeconds < 0) {
		return std::nullopt;
	}
	auto loader = Media::FFMpegLoader(
		Core::FileLocation(),
		bytes,
		bytes::vector());
	if (!loader.open(fromSeconds * crl::time(1000))) {
		return std::nullopt;
	}
	const auto totalSeconds = int(loader.duration() / 1000);
	if (totalSeconds > 0 && fromSeconds >= totalSeconds) {
		return std::nullopt;
	}

	auto channels = 0;
	auto sampleSize = 0;
	switch (loader.format()) {
	case AL_FORMAT_MONO8:
		channels = 1;
		sampleSize = 1;
		break;
	case AL_FORMAT_MONO16:
		channels = 1;
		sampleSize = 2;
		break;
	case AL_FORMAT_STEREO8:
		channels = 2;
		sampleSize = 1;
		break;
	case AL_FORMAT_STEREO16:
		channels = 2;
		sampleSize = 2;
		break;
	default:
		return std::nullopt;
	}
	const auto fromRate = loader.samplesFrequency();
	const auto toRate = kAudioSampleRate;
	if (fromRate <= 0) {
		return std::nullopt;
	}

	const auto limit = int64(seconds) * fromRate * channels * sampleSize;
	auto pcm = QByteArray();
	while (true) {
		const auto result = loader.readMore();
		if (auto error = std::get_if<Media::AudioPlayerLoader::ReadError>(
				&result)) {
			if (*error == Media::AudioPlayerLoader::ReadError::Retry
				|| *error
					== Media::AudioPlayerLoader::ReadError
						::RetryNotQueued) {
				continue;
			} else if (
				*error != Media::AudioPlayerLoader::ReadError::EndOfFile) {
				return std::nullopt;
			}
			break;
		}
		const auto span = std::get<bytes::const_span>(result);
		if (span.empty()) {
			return std::nullopt;
		}
		pcm.append(
			reinterpret_cast<const char*>(span.data()),
			int(span.size()));
		if (pcm.size() >= limit) {
			break;
		}
	}

	auto samples = MixToMono(pcm, channels, sampleSize, fromRate, toRate);
	if (samples.empty()) {
		return std::nullopt;
	}

	auto result = DecodedAudio();
	result.wav = MakeWav(samples, toRate);
	result.totalSeconds = totalSeconds;
	result.fromSeconds = fromSeconds;
	result.toSeconds = fromSeconds + int(samples.size() / toRate);
	return result;
}

std::optional<std::vector<OfficeEntry>> UnzipOffice(
		const QByteArray &zip,
		DocumentKind kind) {
	if (zip.isEmpty()) {
		return std::nullopt;
	}
	constexpr auto kMaxEntrySize = int(50 * 1024 * 1024);
	constexpr auto kMaxName = 128;

	auto reader = zlib::FileToRead(zip);
	if (reader.goToFirstFile() != UNZ_OK) {
		return std::nullopt;
	}
	auto result = std::vector<OfficeEntry>();
	do {
		auto info = unz_file_info();
		char name[kMaxName + 1] = { 0 };
		if (reader.getCurrentFileInfo(
				&info,
				name,
				kMaxName,
				nullptr,
				0,
				nullptr,
				0) != UNZ_OK) {
			return std::nullopt;
		}
		const auto path = QString::fromUtf8(name);
		if (WantOfficeEntry(kind, path)
			&& info.uncompressed_size <= uint32(kMaxEntrySize)) {
			auto bytes = reader.readCurrentFileContent(kMaxEntrySize);
			if (reader.error() != UNZ_OK
				|| (bytes.isEmpty() && info.uncompressed_size > 0)) {
				return std::nullopt;
			}
			result.push_back({ path, bytes });
		}
		const auto jump = reader.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			return std::nullopt;
		}
	} while (true);
	return result;
}

} // namespace Agent
