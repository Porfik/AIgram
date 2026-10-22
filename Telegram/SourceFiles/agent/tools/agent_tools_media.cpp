/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/agent_media_convert.h"
#include "agent/agent_media_loader.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_documents.h"
#include "agent/core/agent_media.h"
#include "agent/tools/agent_tool_format.h"
#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

#include <limits>

namespace Agent::MediaTools {
namespace {

constexpr auto kTextPageChars = kToolResultMaxChars - 1000;
constexpr auto kMaxViewMessageIds = 10;

struct Source {
	HistoryItem *item = nullptr;
	std::optional<MessageMedia> media;
	std::optional<MediaBlock> attachment;
	QJsonObject json;
};

[[nodiscard]] QString TypeName(MessageMediaType type) {
	switch (type) {
	case MessageMediaType::Photo: return u"photo"_q;
	case MessageMediaType::Sticker: return u"sticker"_q;
	case MessageMediaType::Animation: return u"gif"_q;
	case MessageMediaType::Video: return u"video"_q;
	case MessageMediaType::RoundVideo: return u"round_video"_q;
	case MessageMediaType::Voice: return u"voice"_q;
	case MessageMediaType::Audio: return u"audio"_q;
	case MessageMediaType::ImageFile: return u"image_file"_q;
	case MessageMediaType::Document: return u"document"_q;
	}
	Unexpected("MessageMediaType in TypeName.");
}

[[nodiscard]] MediaKind KindFor(MessageMediaType type) {
	switch (type) {
	case MessageMediaType::Photo:
	case MessageMediaType::Sticker:
	case MessageMediaType::ImageFile: return MediaKind::Image;
	case MessageMediaType::Animation:
	case MessageMediaType::Video:
	case MessageMediaType::RoundVideo: return MediaKind::Video;
	case MessageMediaType::Voice:
	case MessageMediaType::Audio: return MediaKind::Audio;
	case MessageMediaType::Document: return MediaKind::File;
	}
	Unexpected("MessageMediaType in KindFor.");
}

[[nodiscard]] MediaKind SourceKind(const Source &source) {
	return source.attachment
		? source.attachment->kind
		: KindFor(source.media->type);
}

[[nodiscard]] QString SourceName(const Source &source) {
	return source.attachment
		? source.attachment->name
		: source.media->name;
}

[[nodiscard]] QString SourceMime(const Source &source) {
	return source.attachment
		? source.attachment->mimeType
		: source.media->mimeType;
}

[[nodiscard]] int64 SourceSize(const Source &source) {
	return source.attachment
		? source.attachment->size
		: source.media->size;
}

[[nodiscard]] MediaBlock SourceBlock(
		const Source &source,
		MediaKind kind,
		QByteArray bytes,
		QString mimeType) {
	auto result = MediaBlock{
		.kind = kind,
		.bytes = std::move(bytes),
		.mimeType = std::move(mimeType),
		.name = SourceName(source),
	};
	if (source.attachment) {
		result.attachment = source.attachment->attachment;
		result.size = source.attachment->size;
		result.duration = source.attachment->duration;
	} else {
		result.peer = source.item->history()->peer->id.value;
		result.messageId = source.item->id.bare;
		result.size = source.media->size;
		result.duration = source.media->duration;
	}
	return result;
}

[[nodiscard]] QString RouteFailText(MediaKind kind) {
	switch (kind) {
	case MediaKind::Image:
		return u"No model here can see images."_q;
	case MediaKind::Audio:
		return u"No model here can listen to audio. For voice and round"
			" video messages try transcribe_voice."_q;
	case MediaKind::Video:
		return u"No model here can watch video."_q;
	case MediaKind::File:
		return u"No model here can read PDF files. The user can choose a"
			" media model in the agent settings."_q;
	}
	Unexpected("MediaKind in RouteFailText.");
}

[[nodiscard]] MediaRoute RouteFor(
		not_null<PendingCall*> call,
		MediaKind kind) {
	return ChooseMediaRoute(
		kind,
		call->mainAbilities,
		call->helperAbilities);
}

struct ViewSink {
	Fn<void(QJsonObject json, std::vector<MediaBlock> media)> media;
	Fn<void(QJsonObject json)> plain;
	Fn<void(QString error)> fail;
};

[[nodiscard]] ViewSink SingleSink(not_null<PendingCall*> call) {
	return ViewSink{
		.media = [=](QJsonObject json, std::vector<MediaBlock> media) {
			call->doneMedia(std::move(json), std::move(media));
		},
		.plain = [=](QJsonObject json) {
			call->done(std::move(json));
		},
		.fail = [=](QString error) {
			call->fail(std::move(error));
		},
	};
}

void WithSource(
		not_null<PendingCall*> call,
		bool allowAttachment,
		Fn<void(Source)> ready) {
	const auto attachment = call->input.value("attachment").toString();
	const auto messageValue = call->input.value("message_id");
	const auto hasMessage = !messageValue.isUndefined()
		&& !messageValue.isNull();
	if (!attachment.isEmpty() && allowAttachment) {
		if (hasMessage) {
			call->fail(u"Pass either message_id or attachment, not"
				" both."_q);
			return;
		}
		auto block = call->readAttachment(attachment);
		if (!block) {
			call->fail(u"There is no attachment %1 in this session."_q
				.arg(attachment));
			return;
		}
		auto source = Source{ .attachment = std::move(block) };
		source.json.insert("attachment", attachment);
		source.json.insert("type", u"attachment"_q);
		ready(std::move(source));
		return;
	} else if (!hasMessage) {
		call->fail(allowAttachment
			? u"Pass message_id (with chat outside a chat) or"
				" attachment."_q
			: u"message_id is required."_q);
		return;
	} else if (!call->peer) {
		call->fail(u"chat is required together with message_id."_q);
		return;
	}
	const auto peer = not_null(call->peer);
	const auto id = MsgId(int64(messageValue.toDouble()));
	const auto session = call->session;
	const auto finish = [=] {
		const auto item = session->data().message(peer->id, id);
		if (!item) {
			call->fail(u"Message %1 was not found in %2. Read the history"
				" to get valid message ids."_q
				.arg(id.bare)
				.arg(peer->name()));
			return;
		}
		auto media = FindMessageMedia(item);
		if (!media) {
			call->fail(u"Message %1 has no photo, video, audio or"
				" file."_q.arg(id.bare));
			return;
		}
		auto source = Source{ .item = item, .media = std::move(media) };
		source.json.insert("chat", QJsonValue(qint64(peer->id.value)));
		source.json.insert("message", QJsonValue(qint64(id.bare)));
		source.json.insert("type", TypeName(source.media->type));
		ready(std::move(source));
	};
	if (session->data().message(peer->id, id)) {
		finish();
	} else {
		session->api().requestMessageData(
			peer,
			id,
			crl::guard(call.get(), finish));
	}
}

[[nodiscard]] bool LoadedAlready(const MessageMedia &media) {
	return media.document
		&& media.document->createMediaView()->loaded(true);
}

void LoadSource(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		const Source &source,
		MediaPart part,
		bool approved,
		Fn<void(QByteArray bytes)> ready) {
	if (source.attachment) {
		ready(source.attachment->bytes);
		return;
	}
	const auto item = not_null(source.item);
	const auto size = source.media->size;
	const auto start = [=] {
		call->lifetime.make_state<MediaLoader>(
			item,
			part,
			[=](QByteArray bytes, QString error) {
				if (!error.isEmpty()) {
					sink.fail(error);
				} else {
					ready(std::move(bytes));
				}
			});
	};
	if (part == MediaPart::Thumbnail
		|| size <= kAutoDownloadBytes
		|| LoadedAlready(*source.media)) {
		start();
		return;
	} else if (size > kAttachmentMaxBytes) {
		sink.fail(u"The file is %1, too big for the agent to read."_q
			.arg(FormatByteSize(size)));
		return;
	} else if (approved) {
		start();
		return;
	}
	const auto peer = item->history()->peer;
	auto card = Card{
		.kind = CardKind::Download,
		.targets = { CardTarget{
			.peer = peer->id.value,
			.messageId = item->id.bare,
			.title = peer->name(),
			.preview = source.media->name,
		} },
		.files = { CardFile{
			.name = source.media->name,
			.mimeType = source.media->mimeType,
			.size = size,
		} },
		.amount = size,
	};
	call->confirm(std::move(card), [=](Card decided) {
		start();
	});
}

void DeliverFromHelper(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		QJsonObject json,
		MediaBlock block,
		const QString &key) {
	const auto question = call->input.value("question").toString();
	call->askMediaModel(
		std::move(block),
		question,
		[=](QString text, QString error) {
			if (!error.isEmpty()) {
				sink.fail(error);
				return;
			}
			auto result = json;
			result.insert(key, text);
			result.insert("media_model", QJsonValue(true));
			sink.plain(std::move(result));
		});
}

void ShowImage(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		const Source &source,
		const QByteArray &bytes,
		const QString &shown) {
	auto prepared = PrepareImageForModel(bytes);
	if (prepared.isEmpty()) {
		sink.fail(u"Could not decode the image."_q);
		return;
	}
	auto block = SourceBlock(
		source,
		MediaKind::Image,
		std::move(prepared),
		u"image/jpeg"_q);
	auto json = source.json;
	switch (RouteFor(call, MediaKind::Image)) {
	case MediaRoute::Main:
		json.insert("shown", shown);
		sink.media(std::move(json), { std::move(block) });
		return;
	case MediaRoute::Helper:
		json.insert("shown", u"description"_q);
		DeliverFromHelper(
			call,
			sink,
			std::move(json),
			std::move(block),
			u"description"_q);
		return;
	case MediaRoute::None:
		sink.fail(RouteFailText(MediaKind::Image));
		return;
	}
}

void ShowCover(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		const Source &source,
		bool approved) {
	if (source.attachment) {
		sink.fail(RouteFailText(MediaKind::Video));
		return;
	}
	LoadSource(call, sink, source, MediaPart::Thumbnail, approved,
		[=](QByteArray bytes) {
			ShowImage(call, sink, source, bytes, u"cover"_q);
		});
}

void ShowVideo(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		const Source &source,
		bool approved) {
	const auto route = RouteFor(call, MediaKind::Video);
	if (route == MediaRoute::None
		|| SourceSize(source) > kModelMediaMaxBytes) {
		ShowCover(call, sink, source, approved);
		return;
	}
	LoadSource(call, sink, source, MediaPart::Full, approved,
		[=](QByteArray bytes) {
			auto mime = SourceMime(source);
			if (!mime.startsWith(u"video/"_q)) {
				mime = u"video/mp4"_q;
			}
			auto block = SourceBlock(
				source,
				MediaKind::Video,
				std::move(bytes),
				std::move(mime));
			auto json = source.json;
			if (route == MediaRoute::Main) {
				json.insert("shown", u"video"_q);
				sink.media(std::move(json), { std::move(block) });
			} else {
				json.insert("shown", u"description"_q);
				DeliverFromHelper(
					call,
					sink,
					std::move(json),
					std::move(block),
					u"description"_q);
			}
		});
}

[[nodiscard]] bool AnimatedSticker(const Source &source) {
	if (!source.media || source.media->type != MessageMediaType::Sticker) {
		return false;
	}
	const auto sticker = source.media->document->sticker();
	return sticker && !sticker->isStatic();
}

void RunViewMediaOne(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		const Source &source,
		bool approved) {
	switch (SourceKind(source)) {
	case MediaKind::Image: {
		const auto part = AnimatedSticker(source)
			? MediaPart::Thumbnail
			: MediaPart::Full;
		LoadSource(
			call,
			sink,
			source,
			part,
			approved,
			[=](QByteArray bytes) {
				ShowImage(call, sink, source, bytes, u"image"_q);
			});
	} break;
	case MediaKind::Video:
		ShowVideo(call, sink, source, approved);
		break;
	case MediaKind::Audio:
		sink.fail(u"This is audio. Use listen_audio, or"
			" transcribe_voice for a voice message."_q);
		break;
	case MediaKind::File:
		sink.fail(u"This is a file. Use read_file."_q);
		break;
	}
}

[[nodiscard]] std::optional<std::vector<MsgId>> ParseViewMessageIds(
		const QJsonObject &input,
		QString &error) {
	const auto value = input.value("message_ids");
	if (value.isUndefined() || value.isNull()) {
		return std::nullopt;
	} else if (!value.isArray()) {
		error = u"message_ids must be an array of message ids."_q;
		return std::nullopt;
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > kMaxViewMessageIds) {
		error = u"message_ids must contain 1 to %1 message ids."_q
			.arg(kMaxViewMessageIds);
		return std::nullopt;
	}
	auto result = std::vector<MsgId>();
	for (const auto &item : list) {
		if (!item.isDouble() || item.toDouble() < 1) {
			error = u"message_ids must contain message ids."_q;
			return std::nullopt;
		}
		const auto id = MsgId(qint64(item.toDouble()));
		auto known = false;
		for (const auto already : result) {
			if (already == id) {
				known = true;
				break;
			}
		}
		if (!known) {
			result.push_back(id);
		}
	}
	return result;
}

struct ViewManyState {
	ViewSink sink;
	std::vector<Source> sources;
	std::vector<MediaBlock> blocks;
	QJsonArray results;
	int index = 0;
	bool finished = false;
};

void ViewManyNext(
		not_null<PendingCall*> call,
		not_null<ViewManyState*> state) {
	if (state->finished) {
		return;
	}
	if (state->index >= int(state->sources.size())) {
		auto json = QJsonObject();
		json.insert("results", state->results);
		call->doneMedia(std::move(json), std::move(state->blocks));
		return;
	}
	const auto &source = state->sources[state->index++];
	RunViewMediaOne(call, state->sink, source, true);
}

void RunViewMediaSources(
		not_null<PendingCall*> call,
		std::vector<Source> sources) {
	const auto state = call->lifetime.make_state<ViewManyState>();
	state->sources = std::move(sources);
	state->sink = ViewSink{
		.media = [=](QJsonObject json, std::vector<MediaBlock> media) {
			state->results.append(std::move(json));
			state->blocks.insert(
				state->blocks.end(),
				std::make_move_iterator(media.begin()),
				std::make_move_iterator(media.end()));
			ViewManyNext(call, state);
		},
		.plain = [=](QJsonObject json) {
			state->results.append(std::move(json));
			ViewManyNext(call, state);
		},
		.fail = [=](QString error) {
			state->finished = true;
			call->fail(std::move(error));
		},
	};
	ViewManyNext(call, state);
}

void RunViewMediaMany(
		not_null<PendingCall*> call,
		std::vector<MsgId> ids) {
	const auto peer = not_null(call->peer);
	if (!call->gate->inFrame(peer->id.value)) {
		call->fail(u"chat is outside the chats this task may read."_q);
		return;
	}
	const auto session = call->session;
	const auto resolve = [=] {
		auto sources = std::vector<Source>();
		auto files = std::vector<CardFile>();
		auto targets = std::vector<CardTarget>();
		auto total = int64(0);
		for (const auto id : ids) {
			const auto item = session->data().message(peer->id, id);
			if (!item) {
				call->fail(u"Message %1 was not found in %2. Read the"
					" history to get valid message ids."_q
					.arg(id.bare)
					.arg(peer->name()));
				return;
			}
			auto media = FindMessageMedia(item);
			if (!media) {
				call->fail(u"Message %1 has no photo, video, audio or"
					" file."_q.arg(id.bare));
				return;
			}
			auto source = Source{ .item = item, .media = std::move(media) };
			source.json.insert("chat", QJsonValue(qint64(peer->id.value)));
			source.json.insert("message", QJsonValue(qint64(id.bare)));
			source.json.insert("type", TypeName(source.media->type));
			if (!LoadedAlready(*source.media)) {
				if (source.media->size > kAttachmentMaxBytes) {
					call->fail(u"The file is %1, too big for the agent to"
						" read."_q.arg(FormatByteSize(source.media->size)));
					return;
				}
				total += source.media->size;
				files.push_back(CardFile{
					.name = source.media->name,
					.mimeType = source.media->mimeType,
					.size = source.media->size,
				});
				targets.push_back(CardTarget{
					.peer = peer->id.value,
					.messageId = id.bare,
					.title = peer->name(),
					.preview = source.media->name,
				});
			}
			sources.push_back(std::move(source));
		}
		const auto kept = std::make_shared<std::vector<Source>>(
			std::move(sources));
		const auto run = [call, kept] {
			RunViewMediaSources(call, std::move(*kept));
		};
		if (total > kAutoDownloadBytes) {
			auto card = Card{
				.kind = CardKind::Download,
				.targets = std::move(targets),
				.files = std::move(files),
				.amount = total,
			};
			call->confirm(std::move(card), [=](Card decided) {
				run();
			});
		} else {
			run();
		}
	};
	auto missing = std::vector<MsgId>();
	for (const auto id : ids) {
		if (!session->data().message(peer->id, id)) {
			missing.push_back(id);
		}
	}
	if (missing.empty()) {
		resolve();
		return;
	}
	const auto left = std::make_shared<int>(int(missing.size()));
	for (const auto id : missing) {
		session->api().requestMessageData(
			peer,
			id,
			crl::guard(call.get(), [=] {
				if (!--*left) {
					resolve();
				}
			}));
	}
}

void RunViewMedia(not_null<PendingCall*> call) {
	auto error = QString();
	const auto ids = ParseViewMessageIds(call->input, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	} else if (ids) {
		if (!call->peer) {
			call->fail(u"chat is required together with message_ids."_q);
			return;
		}
		RunViewMediaMany(call, *ids);
		return;
	}
	const auto sink = SingleSink(call);
	WithSource(call, true, [=](Source source) {
		RunViewMediaOne(call, sink, source, false);
	});
}

[[nodiscard]] bool HasSound(const Source &source) {
	if (source.attachment) {
		return (source.attachment->kind == MediaKind::Audio)
			|| (source.attachment->kind == MediaKind::Video);
	}
	switch (source.media->type) {
	case MessageMediaType::Voice:
	case MessageMediaType::Audio:
	case MessageMediaType::RoundVideo:
	case MessageMediaType::Video: return true;
	default: return false;
	}
}

void RunListenAudio(not_null<PendingCall*> call) {
	const auto from = std::max(
		call->input.value("from_seconds").toInt(),
		0);
	const auto sink = SingleSink(call);
	WithSource(call, true, [=](Source source) {
		if (!HasSound(source)) {
			call->fail(u"This has no sound to listen to. Use view_media"
				" for images and video, read_file for files."_q);
			return;
		}
		const auto route = RouteFor(call, MediaKind::Audio);
		if (route == MediaRoute::None) {
			call->fail(RouteFailText(MediaKind::Audio));
			return;
		}
		LoadSource(call, sink, source, MediaPart::Full, false,
			[=](QByteArray bytes) {
			crl::async([=, bytes = std::move(bytes)] {
				const auto decoded = std::make_shared<
					std::optional<DecodedAudio>>(DecodeAudio(
						bytes,
						from,
						kAudioChunkSeconds));
				crl::on_main(call.get(), [=] {
					if (!*decoded) {
						call->fail(from
							? u"Could not decode the audio from %1 seconds."
								" It may be past the end."_q.arg(from)
							: u"Could not decode the audio."_q);
						return;
					}
					const auto &audio = **decoded;
					auto json = source.json;
					json.insert("duration", audio.totalSeconds);
					json.insert("from", audio.fromSeconds);
					json.insert("to", audio.toSeconds);
					json.insert(
						"more",
						QJsonValue(audio.toSeconds < audio.totalSeconds));
					auto block = SourceBlock(
						source,
						MediaKind::Audio,
						audio.wav,
						u"audio/wav"_q);
					block.duration = audio.toSeconds - audio.fromSeconds;
					if (route == MediaRoute::Main) {
						sink.media(std::move(json), { std::move(block) });
					} else {
						DeliverFromHelper(
							call,
							sink,
							std::move(json),
							std::move(block),
							u"transcript"_q);
					}
				});
			});
		});
	});
}

[[nodiscard]] QString VoicePreview(const MessageMedia &media) {
	const auto name = (media.type == MessageMediaType::RoundVideo)
		? tr::lng_in_dlg_video_message(tr::now)
		: tr::lng_in_dlg_audio(tr::now);
	if (media.duration <= 0) {
		return name;
	}
	return name + ' ' + u"%1:%2"_q
		.arg(media.duration / 60)
		.arg(media.duration % 60, 2, 10, QChar('0'));
}

void WaitTranscript(
		not_null<PendingCall*> call,
		not_null<HistoryItem*> item,
		QJsonObject json) {
	const auto session = call->session;
	const auto finished = std::make_shared<bool>(false);
	const auto check = [=] {
		if (*finished) {
			return;
		}
		const auto &entry = session->api().transcribes().entry(item);
		if (entry.requestId || entry.pending) {
			return;
		}
		*finished = true;
		if (entry.toolong) {
			call->fail(u"This message is too long for Telegram's"
				" transcription. Try listen_audio."_q);
		} else if (entry.failed || entry.result.isEmpty()) {
			call->fail(u"Telegram could not transcribe this message."
				" Try listen_audio."_q);
		} else {
			auto result = json;
			result.insert("text", entry.result);
			call->done(std::move(result));
		}
	};
	rpl::merge(
		session->data().itemResizeRequest(),
		session->data().itemLayoutChanged()
	) | rpl::filter([=](not_null<const HistoryItem*> changed) {
		return (changed == item);
	}) | rpl::on_next([=] {
		check();
	}, call->lifetime);
	session->data().itemRemoved(
		item->fullId()
	) | rpl::on_next([=] {
		if (!*finished) {
			*finished = true;
			call->fail(u"The message was deleted."_q);
		}
	}, call->lifetime);
	auto &transcribes = session->api().transcribes();
	const auto &entry = transcribes.entry(item);
	if (!entry.requestId && !entry.pending && entry.result.isEmpty()) {
		transcribes.toggle(item);
	}
	check();
}

void RunTranscribeVoice(not_null<PendingCall*> call) {
	WithSource(call, false, [=](Source source) {
		const auto type = source.media->type;
		if (type != MessageMediaType::Voice
			&& type != MessageMediaType::RoundVideo) {
			call->fail(u"Telegram transcribes only voice and round video"
				" messages. Use listen_audio for other audio."_q);
			return;
		}
		const auto item = not_null(source.item);
		const auto session = call->session;
		auto &transcribes = session->api().transcribes();
		const auto &entry = transcribes.entry(item);
		if (!entry.result.isEmpty() && !entry.pending) {
			auto json = source.json;
			json.insert("text", entry.result);
			call->done(std::move(json));
			return;
		} else if (entry.failed) {
			call->fail(entry.toolong
				? u"This message is too long for Telegram's"
					" transcription. Try listen_audio."_q
				: u"Telegram could not transcribe this message."
					" Try listen_audio."_q);
			return;
		} else if (session->premium()
			|| transcribes.freeFor(item)
			|| entry.requestId
			|| entry.pending) {
			WaitTranscript(call, item, source.json);
			return;
		} else if (!transcribes.trialsSupport()
			|| transcribes.trialsCount() <= 0) {
			call->fail(u"Telegram Premium is needed to transcribe, and no"
				" trial transcriptions are left this week. Try"
				" listen_audio."_q);
			return;
		} else if (source.media->duration * crl::time(1000)
			> transcribes.trialsMaxLengthMs()) {
			call->fail(u"Trial transcriptions take messages up to %1"
				" seconds; this one is longer. Try listen_audio."_q
				.arg(transcribes.trialsMaxLengthMs() / 1000));
			return;
		}
		const auto peer = item->history()->peer;
		auto card = Card{
			.kind = CardKind::Transcribe,
			.targets = { CardTarget{
				.peer = peer->id.value,
				.messageId = item->id.bare,
				.title = peer->name(),
				.preview = VoicePreview(*source.media),
			} },
			.amount = transcribes.trialsCount(),
			.quota = session->appConfig().get<int>(
				u"transcribe_audio_trial_weekly_number"_q,
				0),
		};
		const auto json = source.json;
		call->confirm(std::move(card), [=](Card decided) {
			WaitTranscript(call, item, json);
		});
	});
}

void DeliverText(
		not_null<PendingCall*> call,
		const Source &source,
		const QString &type,
		const QString &text) {
	const auto offset = std::max(call->input.value("offset").toInt(), 0);
	const auto page = PageText(text, offset, kTextPageChars);
	auto json = source.json;
	json.insert("name", SourceName(source));
	json.insert("type", type);
	json.insert("offset", page.offset);
	json.insert("total", page.total);
	json.insert("next_offset", (page.nextOffset >= 0)
		? QJsonValue(page.nextOffset)
		: QJsonValue(QJsonValue::Null));
	json.insert("text", page.text);
	call->done(std::move(json));
}

void ReadPdf(
		not_null<PendingCall*> call,
		const ViewSink &sink,
		const Source &source,
		QByteArray bytes) {
	const auto route = RouteFor(call, MediaKind::File);
	if (route == MediaRoute::None) {
		call->fail(RouteFailText(MediaKind::File));
		return;
	} else if (bytes.size() > kModelMediaMaxBytes) {
		call->fail(u"The PDF is %1; models take files up to %2."_q.arg(
			FormatByteSize(bytes.size()),
			FormatByteSize(kModelMediaMaxBytes)));
		return;
	}
	auto block = SourceBlock(
		source,
		MediaKind::File,
		std::move(bytes),
		u"application/pdf"_q);
	auto json = source.json;
	json.insert("name", SourceName(source));
	if (route == MediaRoute::Main) {
		json.insert("shown", u"file"_q);
		sink.media(std::move(json), { std::move(block) });
	} else {
		DeliverFromHelper(call, sink, std::move(json), std::move(block), u"text"_q);
	}
}

void RunReadFile(not_null<PendingCall*> call) {
	const auto sink = SingleSink(call);
	WithSource(call, true, [=](Source source) {
		const auto kind = SourceKind(source);
		if (kind == MediaKind::Image) {
			call->fail(u"This is an image. Use view_media."_q);
			return;
		} else if (kind != MediaKind::File) {
			call->fail(u"This is audio or video. Use listen_audio or"
				" view_media."_q);
			return;
		}
		const auto document = DetectDocument(
			SourceName(source),
			SourceMime(source));
		if (document == DocumentKind::Other) {
			call->fail(u"Can't read files of this type (%1)."_q.arg(
				SourceMime(source)));
			return;
		}
		LoadSource(call, sink, source, MediaPart::Full, false,
			[=](QByteArray bytes) {
			if (document == DocumentKind::Pdf) {
				ReadPdf(call, sink, source, std::move(bytes));
			} else if (document == DocumentKind::Text) {
				const auto text = DecodeText(bytes);
				if (!text) {
					call->fail(u"The file is not readable text."_q);
					return;
				}
				DeliverText(call, source, u"text"_q, *text);
			} else {
				const auto entries = UnzipOffice(bytes, document);
				const auto text = entries
					? ExtractOfficeText(document, *entries)
					: QString();
				if (text.trimmed().isEmpty()) {
					call->fail(u"No text was found in the document."_q);
					return;
				}
				DeliverText(call, source, u"office"_q, text);
			}
		});
	});
}

[[nodiscard]] QJsonObject SourceProperties(bool withAttachment) {
	auto result = QJsonObject();
	result.insert("message_id", MessageIdProperty(
		u"Id of the message with the media."_q));
	if (withAttachment) {
		result.insert("attachment", StringProperty(
			u"Instead of message_id: a user attachment id like \"a1\"."_q));
	}
	return result;
}

} // namespace

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	const auto question = StringProperty(
		u"Optional: what you want to know. It helps when a separate media"
		" model looks at the media for you."_q);
	auto viewProperties = SourceProperties(true);
	viewProperties.insert("question", question);
	auto viewIds = MessageIdsProperty(
		u"View media of 1 to %1 messages in one call. Outside a chat,"
		" pass chat too."_q.arg(kMaxViewMessageIds));
	viewIds.insert("maxItems", QJsonValue(kMaxViewMessageIds));
	viewProperties.insert("message_ids", viewIds);
	auto listenProperties = SourceProperties(true);
	listenProperties.insert("from_seconds", IntProperty(
		0,
		std::numeric_limits<int>::max(),
		u"Start listening at this second, default 0. Each call gives at"
		" most 5 minutes."_q));
	listenProperties.insert("question", question);
	auto readProperties = SourceProperties(true);
	readProperties.insert("offset", IntProperty(
		0,
		std::numeric_limits<int>::max(),
		u"Character offset of the page to read, default 0; pass"
		" next_offset to continue."_q));
	readProperties.insert("question", question);
	result.push_back(ToolEntry{
		.name = u"view_media"_q,
		.description = u"Look at a photo, image file, sticker, GIF, video"
			" or round video of a message in this chat, or at an image"
			" or video the user attached. You see it until the end of"
			" this request. Pass message_ids (1 to 10) with chat to view"
			" media of several messages at once."_q,
		.generalDescription = u"Look at a photo, image file, sticker,"
			" GIF, video or round video of a message, or at an image or"
			" video the user attached. You see it until the end of this"
			" request. Pass message_ids (1 to 10) with chat to view media"
			" of several messages at once."_q,
		.inputSchema = ToolSchema(std::move(viewProperties), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::OptionalOne,
		.needs = MediaNeed::Images,
		.run = RunViewMedia,
	});
	result.push_back(ToolEntry{
		.name = u"listen_audio"_q,
		.description = u"Listen to a voice message, audio file, round"
			" video or video of a message in this chat, or to audio or"
			" video the user attached, 5 minutes per call."_q,
		.generalDescription = u"Listen to a voice message, audio file,"
			" round video or video of a message, or to audio or video the"
			" user attached, 5 minutes per call."_q,
		.inputSchema = ToolSchema(
			std::move(listenProperties),
			QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::OptionalOne,
		.needs = MediaNeed::Audio,
		.run = RunListenAudio,
	});
	result.push_back(ToolEntry{
		.name = u"transcribe_voice"_q,
		.description = u"Get Telegram's own transcription of a voice or"
			" round video message in this chat. The transcript also"
			" shows under the message in the chat. Without Telegram"
			" Premium the user confirms spending a trial"
			" transcription."_q,
		.generalDescription = u"Get Telegram's own transcription of a"
			" voice or round video message. The transcript also shows"
			" under the message in the chat. Without Telegram Premium the"
			" user confirms spending a trial transcription."_q,
		.inputSchema = ToolSchema(
			SourceProperties(false),
			QJsonArray{ QJsonValue(u"message_id"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::One,
		.run = RunTranscribeVoice,
	});
	result.push_back(ToolEntry{
		.name = u"read_file"_q,
		.description = u"Read a document of a message in this chat, or a"
			" file the user attached: text and code files, docx, xlsx,"
			" pptx, odt, ods, odp and PDF. Long text comes in pages."_q,
		.generalDescription = u"Read a document of a message, or a file"
			" the user attached: text and code files, docx, xlsx, pptx,"
			" odt, ods, odp and PDF. Long text comes in pages."_q,
		.inputSchema = ToolSchema(std::move(readProperties), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::OptionalOne,
		.run = RunReadFile,
	});
	return result;
}

} // namespace Agent::MediaTools
