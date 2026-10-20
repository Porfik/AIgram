/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_types.h"

#include <optional>
#include <vector>

namespace Agent {

inline constexpr auto kImageMaxSide = 1568;
inline constexpr auto kAudioChunkSeconds = 300;
inline constexpr auto kAudioSampleRate = 16000;
inline constexpr auto kAutoDownloadBytes = int64(20 * 1024 * 1024);
inline constexpr auto kModelMediaMaxBytes = int64(20 * 1024 * 1024);
inline constexpr auto kAttachmentMaxBytes = int64(50 * 1024 * 1024);
inline constexpr auto kAttachmentsPerMessage = 10;

struct MediaAbilities {
	bool images = false;
	bool audio = false;
	bool video = false;
	bool files = false;
};

[[nodiscard]] bool Supports(const MediaAbilities &abilities, MediaKind kind);

enum class MediaRoute : uchar {
	Main,
	Helper,
	None,
};

[[nodiscard]] MediaRoute ChooseMediaRoute(
	MediaKind kind,
	const MediaAbilities &main,
	const std::optional<MediaAbilities> &helper);

[[nodiscard]] MediaKind MediaKindForMime(const QString &mimeType);
[[nodiscard]] QString FormatByteSize(int64 bytes);
[[nodiscard]] QString MediaLabel(const MediaBlock &block);
[[nodiscard]] QString MediaPlaceholder(const MediaBlock &block);

[[nodiscard]] QString AttachmentId(int index);
[[nodiscard]] int AttachmentIndex(const QString &id);

[[nodiscard]] Request HelperRequest(
	const QString &model,
	MediaBlock media,
	const QString &question);

[[nodiscard]] QByteArray SerializeAttachment(const MediaBlock &block);
[[nodiscard]] std::optional<MediaBlock> DeserializeAttachment(
	const QByteArray &bytes);

[[nodiscard]] std::vector<int16> MixToMono(
	const QByteArray &pcm,
	int channels,
	int sampleSize,
	int fromRate,
	int toRate);
[[nodiscard]] QByteArray MakeWav(
	const std::vector<int16> &samples,
	int sampleRate);

} // namespace Agent
