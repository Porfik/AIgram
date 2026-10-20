/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_media.h"

namespace Agent {

bool Supports(const MediaAbilities &abilities, MediaKind kind) {
	return false;
}

MediaRoute ChooseMediaRoute(
		MediaKind kind,
		const MediaAbilities &main,
		const std::optional<MediaAbilities> &helper) {
	return MediaRoute::None;
}

MediaKind MediaKindForMime(const QString &mimeType) {
	return MediaKind::File;
}

QString FormatByteSize(int64 bytes) {
	return QString();
}

QString MediaLabel(const MediaBlock &block) {
	return QString();
}

QString MediaPlaceholder(const MediaBlock &block) {
	return QString();
}

QString AttachmentId(int index) {
	return QString();
}

int AttachmentIndex(const QString &id) {
	return 0;
}

Request HelperRequest(
		const QString &model,
		MediaBlock media,
		const QString &question) {
	return Request();
}

QByteArray SerializeAttachment(const MediaBlock &block) {
	return QByteArray();
}

std::optional<MediaBlock> DeserializeAttachment(const QByteArray &bytes) {
	return std::nullopt;
}

std::vector<int16> MixToMono(
		const QByteArray &pcm,
		int channels,
		int sampleSize,
		int fromRate,
		int toRate) {
	return {};
}

QByteArray MakeWav(const std::vector<int16> &samples, int sampleRate) {
	return QByteArray();
}

} // namespace Agent
