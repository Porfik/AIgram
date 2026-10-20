/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "agent/tools/agent_tool_format.h"
#include "api/api_compose_with_ai.h"
#include "api/api_text_entities.h"
#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "data/data_ai_compose_tones.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"

namespace Agent::TelegramAiTools {
namespace {

constexpr auto kSummaryTimeout = crl::time(60 * 1000);
constexpr auto kTonesTimeout = crl::time(10 * 1000);
constexpr auto kMaxComposeText = 4096;

} // namespace

namespace {

void WaitSummary(
		not_null<PendingCall*> call,
		not_null<HistoryItem*> item) {
	const auto session = call->session;
	const auto finished = std::make_shared<bool>(false);
	const auto check = [=] {
		if (*finished) {
			return;
		}
		const auto &entry = session->api().transcribes().summary(item);
		if (entry.premiumRequired) {
			*finished = true;
			call->fail(u"Telegram's free summaries are used up; more need"
				" Telegram Premium."_q);
			return;
		}
		if (!entry.loading) {
			if (!entry.result.empty()) {
				*finished = true;
				auto json = QJsonObject();
				json.insert("chat", QJsonValue(qint64(
					item->history()->peer->id.value)));
				json.insert("message", QJsonValue(qint64(item->id.bare)));
				json.insert("summary", ToolFormat::EntitiesToMarkdown(
					entry.result));
				call->done(std::move(json));
			} else {
				*finished = true;
				call->fail(u"Telegram could not summarize this message."_q);
			}
		}
	};
	const auto timer = call->lifetime.make_state<base::Timer>([=] {
		if (*finished) {
			return;
		}
		*finished = true;
		call->fail(u"Summarizing is taking too long, try again later."_q);
	});
	rpl::merge(
		session->data().itemResizeRequest(),
		session->data().itemLayoutChanged()
	) | rpl::on_next([=] {
		check();
	}, call->lifetime);
	timer->callOnce(kSummaryTimeout);
	auto &transcribes = session->api().transcribes();
	const auto &entry = transcribes.summary(item);
	if (!entry.loading && entry.result.empty()) {
		transcribes.toggleSummary(item);
	}
	check();
}

void RunSummarizeMessage(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = not_null(call->peer);
	const auto messageValue = call->input.value("message_id");
	const auto id = MsgId(int64(messageValue.toDouble()));
	const auto finish = crl::guard(call.get(), [=] {
		const auto item = session->data().message(peer->id, id);
		if (!item) {
			call->fail(u"Message %1 was not found in %2. Read the history"
				" to get valid message ids."_q
				.arg(id.bare)
				.arg(peer->name()));
			return;
		}
		if (!item->canBeSummarized()) {
			call->fail(u"This message can't be summarized."_q);
			return;
		}
		WaitSummary(call, item);
	});
	if (session->data().message(peer->id, id)) {
		finish();
	} else {
		session->api().requestMessageData(
			peer,
			id,
			crl::guard(call.get(), finish));
	}
}

void RunTranslate(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto toLang = call->input.value("to_lang").toString();
	if (toLang.size() != 2) {
		call->fail(u"to_lang must be a two-letter language code."_q);
		return;
	}
	const auto messageIds = call->input.value("message_ids");
	const auto text = call->input.value("text").toString();
	auto ids = std::vector<MsgId>();
	if (messageIds.isArray()) {
		if (!text.isEmpty()) {
			call->fail(u"Give exactly one of message_ids and text."_q);
			return;
		}
		const auto list = messageIds.toArray();
		if (list.isEmpty() || list.size() > 20) {
			call->fail(u"message_ids must contain 1 to 20 ids."_q);
			return;
		}
		for (const auto &value : list) {
			ids.push_back(MsgId(int64(value.toDouble())));
		}
		if (!call->peer) {
			call->fail(u"message_ids needs a chat."_q);
			return;
		}
	} else if (text.isEmpty()) {
		call->fail(u"Give exactly one of message_ids and text."_q);
		return;
	} else if (text.size() > 4096) {
		call->fail(u"text must be at most 4096 characters."_q);
		return;
	}
	const auto finish = [=](
			const QVector<MTPTextWithEntities> &list) {
		auto json = QJsonObject();
		json.insert("to_lang", toLang);
		auto translations = QJsonArray();
		for (auto i = 0, count = int(list.size()); i != count; ++i) {
			auto entry = QJsonObject();
			if (i < int(ids.size())) {
				entry.insert("message", QJsonValue(qint64(ids[i].bare)));
			}
			entry.insert("text", ToolFormat::EntitiesToMarkdown(
				Api::ParseTextWithEntities(session, list[i])));
			translations.append(entry);
		}
		json.insert("translations", translations);
		call->done(std::move(json));
	};
	if (!ids.empty()) {
		const auto peer = not_null(call->peer);
		auto mtpIds = QVector<MTPint>();
		mtpIds.reserve(ids.size());
		for (const auto &id : ids) {
			mtpIds.push_back(MTP_int(id));
		}
		call->api.request(MTPmessages_TranslateText(
			MTP_flags(MTPmessages_TranslateText::Flag::f_peer
				| MTPmessages_TranslateText::Flag::f_id),
			peer->input(),
			MTP_vector<MTPint>(mtpIds),
			MTPVector<MTPTextWithEntities>(),
			MTP_string(toLang),
			MTPstring() // tone
		)).done([=](const MTPmessages_TranslatedText &result) {
			finish(result.data().vresult().v);
		}).fail([=](const MTP::Error &error) {
			call->fail(error.type() == u"TO_LANG_INVALID"_q
				? u"Unknown target language."_q
				: MtpErrorText(error));
		}).send();
	} else {
		auto texts = QVector<MTPTextWithEntities>();
		texts.push_back(MTP_textWithEntities(
			MTP_string(text),
			MTP_vector<MTPMessageEntity>()));
		call->api.request(MTPmessages_TranslateText(
			MTP_flags(MTPmessages_TranslateText::Flag::f_text),
			MTP_inputPeerEmpty(),
			MTPVector<MTPint>(),
			MTP_vector<MTPTextWithEntities>(texts),
			MTP_string(toLang),
			MTPstring() // tone
		)).done([=](const MTPmessages_TranslatedText &result) {
			finish(result.data().vresult().v);
		}).fail([=](const MTP::Error &error) {
			call->fail(error.type() == u"TO_LANG_INVALID"_q
				? u"Unknown target language."_q
				: MtpErrorText(error));
		}).send();
	}
}

[[nodiscard]] const Data::AiComposeTone *FindTone(
		const std::vector<Data::AiComposeTone> &list,
		const QString &name) {
	const auto lower = name.toLower();
	for (const auto &tone : list) {
		if (tone.title.toLower() == lower
			|| (!tone.slug.isEmpty() && tone.slug.toLower() == lower)
			|| (tone.isDefault && tone.defaultType.toLower() == lower)) {
			return &tone;
		}
	}
	return nullptr;
}

[[nodiscard]] QString ToneNames(const std::vector<Data::AiComposeTone> &list) {
	auto names = QStringList();
	for (const auto &tone : list) {
		names.push_back(tone.isDefault ? tone.defaultType : tone.title);
	}
	return names.join(u", "_q);
}

void SendCompose(
		not_null<PendingCall*> call,
		Api::ComposeWithAi::Request request) {
	const auto session = call->session;
	const auto requestId = std::make_shared<mtpRequestId>(0);
	*requestId = session->api().composeWithAi().request(
		std::move(request),
		crl::guard(call.get(), [=](Api::ComposeWithAi::Result &&result) {
			*requestId = 0;
			auto json = QJsonObject();
			json.insert("text", ToolFormat::EntitiesToMarkdown(
				result.resultText));
			call->done(std::move(json));
		}),
		crl::guard(call.get(), [=](const MTP::Error &error) {
			*requestId = 0;
			call->fail(MtpErrorText(error));
		}));
	call->lifetime.add([=] {
		if (const auto id = *requestId) {
			session->api().composeWithAi().cancel(id);
		}
	});
}

void ComposeWithTone(
		not_null<PendingCall*> call,
		Api::ComposeWithAi::Request request,
		const QString &name) {
	const auto &list = call->session->data().aiComposeTones().list();
	const auto tone = FindTone(list, name);
	if (!tone) {
		call->fail(u"Unknown tone: %1. The tones are: %2."_q.arg(
			name.left(64),
			ToneNames(list)));
		return;
	} else if (tone->isDefault) {
		request.setDefaultTone(tone->defaultType);
	} else {
		request.setCustomTone(tone->id, tone->accessHash);
	}
	SendCompose(call, std::move(request));
}

void RunAiCompose(not_null<PendingCall*> call) {
	const auto &input = call->input;
	const auto text = input.value("text").toString();
	if (text.trimmed().isEmpty()) {
		call->fail(u"text is required."_q);
		return;
	} else if (text.size() > kMaxComposeText) {
		call->fail(u"text must be at most %1 characters."_q.arg(
			kMaxComposeText));
		return;
	}
	auto request = Api::ComposeWithAi::Request{
		.text = TextWithEntities{ text },
		.translateToLang = input.value("translate_to").toString().trimmed(),
		.proofread = input.value("proofread").toBool(),
		.emojify = input.value("emojify").toBool(),
	};
	const auto tone = input.value("tone").toString().trimmed();
	const auto custom = input.value("custom_tone").toString().trimmed();
	if (!tone.isEmpty() && !custom.isEmpty()) {
		call->fail(u"Give tone or custom_tone, not both."_q);
		return;
	} else if (!request.proofread
		&& !request.emojify
		&& request.translateToLang.isEmpty()
		&& tone.isEmpty()
		&& custom.isEmpty()) {
		call->fail(u"Choose at least one of proofread, emojify,"
			" translate_to, tone and custom_tone."_q);
		return;
	} else if (!custom.isEmpty()) {
		request.setCustomPrompt(custom);
	}
	if (tone.isEmpty()) {
		SendCompose(call, std::move(request));
		return;
	}
	auto &tones = call->session->data().aiComposeTones();
	if (!tones.list().empty()) {
		ComposeWithTone(call, std::move(request), tone);
		return;
	}
	const auto finished = std::make_shared<bool>(false);
	const auto finish = [=] {
		if (!*finished) {
			*finished = true;
			ComposeWithTone(call, request, tone);
		}
	};
	tones.updated(
	) | rpl::on_next(finish, call->lifetime);
	const auto timer = call->lifetime.make_state<base::Timer>(finish);
	timer->callOnce(kTonesTimeout);
	tones.refresh();
}

} // namespace

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	auto summarize = QJsonObject();
	summarize.insert("message_id", MessageIdProperty(
		u"Id of the message to summarize."_q));
	result.push_back({
		.name = u"summarize_message"_q,
		.description = u"Summarize a message using Telegram's own AI."
			" The summary appears in the chat as you read it."_q,
		.inputSchema = ToolSchema(
			std::move(summarize),
			QJsonArray{ QJsonValue(u"message_id"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::One,
		.group = u"telegram_ai"_q,
		.run = RunSummarizeMessage,
	});
	auto translate = QJsonObject();
	translate.insert("to_lang", StringProperty(
		u"Two-letter language code to translate into."_q));
	translate.insert("message_ids", MessageIdsProperty(
		u"Ids of the messages to translate, 1 to 20."_q));
	translate.insert("text", StringProperty(
		u"Text to translate, 1 to 4096 characters."_q));
	result.push_back({
		.name = u"translate"_q,
		.description = u"Translate messages or text with Telegram AI."_q,
		.inputSchema = ToolSchema(
			std::move(translate),
			QJsonArray{ QJsonValue(u"to_lang"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::OptionalOne,
		.group = u"telegram_ai"_q,
		.run = RunTranslate,
	});
	auto compose = QJsonObject();
	compose.insert("text", StringProperty(
		u"The text to rewrite, 1 to 4096 characters."_q));
	compose.insert("proofread", BoolProperty(
		u"Fix spelling and grammar."_q));
	compose.insert("emojify", BoolProperty(u"Add fitting emoji."_q));
	compose.insert("translate_to", StringProperty(
		u"Two-letter language code to translate the text into."_q));
	compose.insert("tone", StringProperty(
		u"A tone of Telegram's AI by name (an unknown name gets the list"
		" of tones back)."_q));
	compose.insert("custom_tone", StringProperty(
		u"A free description of the wanted style instead of tone."_q));
	result.push_back({
		.name = u"ai_compose"_q,
		.description = u"Rewrite a text with Telegram's own AI: proofread"
			" it, add emoji, translate it or change its tone. Returns the"
			" new text; nothing is sent."_q,
		.inputSchema = ToolSchema(
			std::move(compose),
			QJsonArray{ QJsonValue(u"text"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.group = u"telegram_ai"_q,
		.untrusted = false,
		.run = RunAiCompose,
	});
	return result;
}

} // namespace Agent::TelegramAiTools
