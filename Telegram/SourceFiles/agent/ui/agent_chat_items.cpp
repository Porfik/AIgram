/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_chat_items.h"

#include "agent/agent_chat.h"
#include "agent/agent_manager.h"
#include "agent/core/agent_card.h"
#include "agent/core/agent_media.h"
#include "agent/core/agent_prompts.h"
#include "agent/ui/agent_feed_format.h"
#include "base/unixtime.h"
#include "ui/image/image_location.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "base/random.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_reply_markup.h"
#include "iv/editor/iv_editor_clipboard_import.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"

#include <QtCore/QBuffer>
#include <QtGui/QImageReader>

namespace Agent::Ui {
namespace {

using Block = Iv::RichPage::Block;
using BlockKind = Iv::RichPage::BlockKind;
using Button = HistoryMessageMarkupButton;
using ButtonColor = HistoryMessageMarkupButton::Color;

constexpr auto kThinkingPreview = 400;
constexpr auto kButtonPrefix = "agent:";

base::flat_map<FullMsgId, ChatItems*> &Registry() {
	static auto result = base::flat_map<FullMsgId, ChatItems*>();
	return result;
}

[[nodiscard]] Iv::RichPage::RichText Rich(const QString &text) {
	return { .text = TextWithEntities{ text } };
}

[[nodiscard]] Iv::RichPage::RichText Rich(TextWithEntities text) {
	return { .text = std::move(text) };
}

[[nodiscard]] Block Paragraph(TextWithEntities text) {
	return { .kind = BlockKind::Paragraph, .text = Rich(std::move(text)) };
}

[[nodiscard]] Block Code(const QString &text) {
	return {
		.kind = BlockKind::Code,
		.text = Rich(text),
		.language = u"json"_q,
	};
}

[[nodiscard]] Block Details(
		TextWithEntities title,
		std::vector<Block> blocks) {
	return {
		.kind = BlockKind::Details,
		.text = Rich(std::move(title)),
		.blocks = std::move(blocks),
	};
}

[[nodiscard]] Iv::RichMessageLimits DisplayLimits() {
	auto result = Iv::RichMessageLimits();
	result.lengthLimit = 1 << 20;
	result.maxBlocks = 5000;
	result.maxMedia = 0;
	return result;
}

void Linkify(Iv::RichPage::RichText &text) {
	auto &value = text.text;
	TextUtilities::ParseEntities(value, TextParseLinks | TextParseMentions);
	for (auto &entity : value.entities) {
		const auto part = value.text.mid(entity.offset(), entity.length());
		if (entity.type() == EntityType::Url) {
			const auto url = part.contains(u"://"_q)
				? part
				: (u"https://"_q + part);
			entity = EntityInText(
				EntityType::CustomUrl,
				entity.offset(),
				entity.length(),
				url);
		} else if (entity.type() == EntityType::Mention) {
			entity = EntityInText(
				EntityType::CustomUrl,
				entity.offset(),
				entity.length(),
				u"https://t.me/"_q + part.mid(1));
		}
	}
}

void Linkify(std::vector<Block> &blocks) {
	for (auto &block : blocks) {
		if (block.kind == BlockKind::Code) {
			continue;
		}
		Linkify(block.text);
		Linkify(block.caption);
		for (auto &item : block.listItems) {
			Linkify(item.text);
			Linkify(item.blocks);
		}
		for (auto &row : block.tableRows) {
			for (auto &cell : row.cells) {
				Linkify(cell.text);
			}
		}
		Linkify(block.blocks);
	}
}

[[nodiscard]] std::vector<Block> MarkdownBlocks(const QString &text) {
	auto imported = Iv::Editor::BlocksFromMarkdown(
		text,
		DisplayLimits(),
		0);
	auto result = (imported && !imported->blocks.empty())
		? std::move(imported->blocks)
		: Iv::SplitTextIntoRichPage(TextWithEntities{ text }).blocks;
	Linkify(result);
	return result;
}

[[nodiscard]] QString ThoughtText(int durationMs) {
	if (durationMs <= 0) {
		return tr::lng_agent_thinking(tr::now);
	}
	const auto seconds = std::max((durationMs + 500) / 1000, 1);
	return (seconds < 60)
		? tr::lng_agent_thought_seconds(tr::now, lt_count, seconds)
		: tr::lng_agent_thought_minutes(
			tr::now,
			lt_count,
			(seconds + 30) / 60);
}

[[nodiscard]] QString TurnEndText(TurnEnd end) {
	switch (end) {
	case TurnEnd::Interrupted: return tr::lng_agent_turn_interrupted(tr::now);
	case TurnEnd::StepLimit: return tr::lng_agent_turn_step_limit(tr::now);
	case TurnEnd::MaxTokens: return tr::lng_agent_turn_max_tokens(tr::now);
	case TurnEnd::Refusal: return tr::lng_agent_turn_refusal(tr::now);
	case TurnEnd::Completed:
	case TurnEnd::Failed:
		break;
	}
	return QString();
}

[[nodiscard]] QString AttachmentLine(const MediaBlock &attachment) {
	return QString::fromUtf8("\xF0\x9F\x93\x8E ")
		+ (attachment.name.isEmpty()
			? attachment.mimeType
			: attachment.name)
		+ (attachment.size > 0
			? (u" · "_q + FormatByteSize(attachment.size))
			: QString());
}

[[nodiscard]] bool StepFailed(const FeedStep &step) {
	const auto tool = std::get_if<FeedTool>(&step);
	return tool && tool->result && tool->result->error;
}

[[nodiscard]] QString StepTitle(
		const FeedStep &step,
		Main::Session *session) {
	return v::match(step, [](const FeedThinking &data) {
		return ThoughtText(data.durationMs);
	}, [&](const FeedTool &data) {
		return ToolTitleText(data.call, data.result, session);
	});
}

using AttachmentPhoto = Fn<PhotoData*(const MediaBlock &media)>;

[[nodiscard]] Block PhotoBlock(not_null<PhotoData*> photo) {
	return {
		.kind = BlockKind::Photo,
		.width = photo->width(),
		.height = photo->height(),
		.photoId = photo->id,
		.photo = photo,
	};
}

[[nodiscard]] std::optional<Block> StepMediaBlock(
		const MediaBlock &media,
		Main::Session *session,
		const AttachmentPhoto &attachmentPhoto) {
	if (!media.attachment.isEmpty()) {
		const auto photo = attachmentPhoto
			? attachmentPhoto(media)
			: nullptr;
		return photo ? PhotoBlock(photo) : std::optional<Block>();
	} else if (!session || !media.peer || !media.messageId) {
		return std::nullopt;
	}
	const auto item = session->data().message(
		FullMsgId(PeerId(media.peer), MsgId(media.messageId)));
	const auto itemMedia = item ? item->media() : nullptr;
	if (!itemMedia) {
		return std::nullopt;
	} else if (const auto photo = itemMedia->photo()) {
		return PhotoBlock(photo);
	} else if (const auto document = itemMedia->document()) {
		const auto video = document->isVideoFile()
			|| document->isAnimation()
			|| document->isVideoMessage();
		const auto size = document->dimensions;
		return Block{
			.kind = video ? BlockKind::Video : BlockKind::File,
			.fileName = document->filename(),
			.width = size.width(),
			.height = size.height(),
			.documentId = document->id,
			.document = document,
		};
	}
	return std::nullopt;
}

[[nodiscard]] Block StepBlock(
		const FeedStep &step,
		Main::Session *session,
		const AttachmentPhoto &attachmentPhoto) {
	return v::match(step, [&](const FeedThinking &data) {
		return Details(
			TextWithEntities{ ThoughtText(data.durationMs) },
			{ Paragraph(TextWithEntities{ data.text }) });
	}, [&](const FeedTool &data) {
		auto title = TextWithEntities{ ToolTitleText(
			data.call,
			data.result,
			session) };
		if (data.result && data.result->error) {
			title.append(u" — "_q).append(
				tr::lng_agent_tool_failed(tr::now));
		}
		auto blocks = std::vector<Block>();
		if (IsMediaTool(data.call.name)) {
			const auto object = ToolResultObject(data.result);
			if (data.call.name == u"view_media"_q) {
				if (auto media = StepMediaBlock(
						ResultMediaBlock(object),
						session,
						attachmentPhoto)) {
					blocks.push_back(std::move(*media));
				}
			}
			const auto transcript = ResultTranscriptText(
				data.call.name,
				object);
			if (!transcript.isEmpty()) {
				blocks.push_back(Paragraph(::Ui::Text::Bold(
					tr::lng_agent_tool_transcript(tr::now))));
				blocks.push_back(Paragraph(TextWithEntities{ transcript }));
			}
		}
		const auto arguments = ArgumentsText(data.call);
		if (!arguments.isEmpty()) {
			blocks.push_back(Paragraph(::Ui::Text::Bold(
				tr::lng_agent_tool_arguments(tr::now))));
			blocks.push_back(Code(arguments));
		}
		if (data.result) {
			blocks.push_back(Paragraph(::Ui::Text::Bold(
				tr::lng_agent_tool_result(tr::now))));
			blocks.push_back(Code(ResultText(*data.result)));
		}
		if (blocks.empty()) {
			blocks.push_back(Paragraph(TextWithEntities{
				tr::lng_agent_card_running(tr::now) }));
		}
		return Details(std::move(title), std::move(blocks));
	});
}

[[nodiscard]] Block StepsBlock(
		const std::vector<FeedStep> &steps,
		bool live,
		Main::Session *session,
		const AttachmentPhoto &attachmentPhoto) {
	auto title = QString();
	auto failed = 0;
	auto thought = 0;
	for (const auto &step : steps) {
		if (std::holds_alternative<FeedTool>(step)) {
			title = StepTitle(step, session);
		} else if (const auto thinking = std::get_if<FeedThinking>(&step)) {
			thought += thinking->durationMs;
		}
		if (StepFailed(step)) {
			++failed;
		}
	}
	if (live && !steps.empty()) {
		title = StepTitle(steps.back(), session);
	} else if (title.isEmpty()) {
		title = ThoughtText(thought);
	}
	if (steps.size() > 1) {
		title += u" · "_q
			+ tr::lng_agent_steps(tr::now, lt_count, int(steps.size()));
	}
	if (failed) {
		title += u" · "_q
			+ tr::lng_agent_steps_failed(tr::now, lt_count, failed);
	}
	auto blocks = std::vector<Block>();
	blocks.reserve(steps.size());
	for (const auto &step : steps) {
		blocks.push_back(StepBlock(step, session, attachmentPhoto));
	}
	return Details(TextWithEntities{ title }, std::move(blocks));
}

[[nodiscard]] QString ActionLine(const FeedTool &tool) {
	const auto parsed = ParseActionResult(tool.result);
	auto result = QString(QChar(parsed.failed.isEmpty() ? 0x2713 : 0x2717))
		+ ' '
		+ ActionMainText(
			tool.call.name,
			tool.call.input,
			parsed.chats(),
			parsed.folder,
			parsed.fact,
			parsed);
	if (!parsed.failed.isEmpty()) {
		result += '\n' + parsed.failed.join('\n');
	}
	return result;
}

[[nodiscard]] QByteArray ButtonData(
		ChatButtonAction action,
		const QString &id) {
	return QByteArray(kButtonPrefix)
		+ QByteArray::number(int(action))
		+ ':'
		+ id.toUtf8();
}

[[nodiscard]] std::optional<ChatButton> ParseButtonData(
		const QByteArray &data) {
	if (!data.startsWith(kButtonPrefix)) {
		return std::nullopt;
	}
	const auto rest = data.mid(int(strlen(kButtonPrefix)));
	const auto colon = rest.indexOf(':');
	if (colon <= 0) {
		return std::nullopt;
	}
	auto ok = false;
	const auto action = rest.left(colon).toInt(&ok);
	if (!ok
		|| action < 0
		|| action > int(ChatButtonAction::ShowInFolder)) {
		return std::nullopt;
	}
	return ChatButton{
		.action = ChatButtonAction(action),
		.id = QString::fromUtf8(rest.mid(colon + 1)),
	};
}

[[nodiscard]] Button MakeButton(
		const QString &text,
		ChatButtonAction action,
		const QString &id,
		ButtonColor color = ButtonColor::Normal) {
	return Button(
		Button::Type::Callback,
		text,
		{ .color = color },
		ButtonData(action, id));
}

[[nodiscard]] QString CardTitle(const Card &card) {
	if (card.kind == CardKind::Change && !card.text.isEmpty()) {
		return card.text;
	}
	return CardTitleText(card.kind);
}

[[nodiscard]] QString CardExecuteText(const Card &card) {
	const auto count = CheckedCount(card);
	const auto many = (card.targets.size() > 1);
	switch (card.kind) {
	case CardKind::CreateTask: return tr::lng_agent_card_create(tr::now);
	case CardKind::UpdateTask: return tr::lng_agent_card_save(tr::now);
	case CardKind::Transcribe: return tr::lng_agent_card_transcribe(tr::now);
	case CardKind::Download:
		return tr::lng_agent_card_download_count(tr::now, lt_count, count);
	case CardKind::Send:
	case CardKind::SendFiles:
		return many
			? tr::lng_agent_card_send_count(tr::now, lt_count, count)
			: tr::lng_agent_card_send(tr::now);
	case CardKind::Draft: return tr::lng_agent_card_replace(tr::now);
	default: break;
	}
	return many
		? tr::lng_agent_card_confirm_count(tr::now, lt_count, count)
		: tr::lng_agent_card_confirm(tr::now);
}

[[nodiscard]] QString CardDetailText(const Card &card) {
	if (card.detail.isEmpty()) {
		return QString();
	}
	switch (card.kind) {
	case CardKind::Send:
		return tr::lng_agent_card_reply_to(tr::now, lt_text, card.detail);
	case CardKind::Forward:
		return tr::lng_agent_card_from_chat(tr::now, lt_chat, card.detail);
	case CardKind::Edit:
		return tr::lng_agent_card_current_text(tr::now)
			+ u":\n"_q
			+ card.detail;
	case CardKind::Draft:
		return tr::lng_agent_card_field_text(tr::now)
			+ u":\n"_q
			+ card.detail;
	case CardKind::Download:
		return tr::lng_agent_card_download_to(tr::now, lt_path, card.detail);
	default:
		return QString();
	}
}

[[nodiscard]] TextWithEntities CardBody(const Card &card) {
	auto result = ::Ui::Text::Bold(CardTitle(card));
	auto lines = QStringList();
	for (const auto &target : card.targets) {
		auto line = target.title;
		if (!target.preview.isEmpty()) {
			line += u": "_q + target.preview;
		}
		if (!target.error.isEmpty()) {
			line += u" — "_q + target.error;
		}
		lines.push_back(line);
	}
	for (const auto &row : card.rows) {
		auto line = row.label + u": "_q + row.value;
		if (!row.previous.isEmpty()) {
			line += u" ("_q
				+ tr::lng_agent_card_task_was(tr::now, lt_text, row.previous)
				+ ')';
		}
		lines.push_back(line);
	}
	for (const auto &file : card.files) {
		if (!ranges::contains(card.rows, file.name, &CardRow::value)) {
			lines.push_back(file.name);
		}
	}
	if (!lines.isEmpty()) {
		result.append('\n').append(lines.join('\n'));
	}
	const auto shown = card.text.isEmpty() ? card.proposedText : card.text;
	if (card.kind != CardKind::Change && !shown.isEmpty()) {
		result.append('\n').append(::Ui::Text::Wrapped(
			TextWithEntities{ shown },
			EntityType::Blockquote));
	}
	if (const auto detail = CardDetailText(card); !detail.isEmpty()) {
		result.append('\n').append(detail);
	}
	return result;
}

[[nodiscard]] QString CardStatusLine(const Card &card, bool run) {
	auto failed = 0;
	for (const auto &target : card.targets) {
		if (!target.error.isEmpty()) {
			++failed;
		}
	}
	const auto count = int(card.targets.size());
	auto result = QString();
	switch (card.status) {
	case CardStatus::Running:
		result = tr::lng_agent_card_running(tr::now);
		break;
	case CardStatus::Done:
		result = failed
			? tr::lng_agent_card_done_partial(
				tr::now,
				lt_done,
				QString::number(count - failed),
				lt_total,
				QString::number(count))
			: (card.kind == CardKind::Download)
			? (card.detail.isEmpty()
				? tr::lng_agent_card_downloaded(tr::now)
				: tr::lng_agent_card_saved(tr::now))
			: (card.kind == CardKind::CreateTask)
			? tr::lng_agent_card_task_created(tr::now)
			: (card.kind == CardKind::UpdateTask)
			? tr::lng_agent_card_task_updated(tr::now)
			: tr::lng_agent_card_done(tr::now);
		break;
	case CardStatus::Drafted:
		result = tr::lng_agent_card_drafted(tr::now);
		break;
	case CardStatus::Declined:
		result = tr::lng_agent_card_declined(tr::now);
		break;
	case CardStatus::Failed:
		result = tr::lng_agent_card_failed(tr::now);
		break;
	case CardStatus::NotDone:
		result = tr::lng_agent_card_not_done(tr::now);
		break;
	case CardStatus::Expired:
		result = tr::lng_agent_card_expired(tr::now);
		break;
	case CardStatus::Pending:
		break;
	}
	if (card.autoApproved && !result.isEmpty()) {
		result += u" · "_q + (run
			? tr::lng_agent_card_auto_task(tr::now)
			: tr::lng_agent_card_auto(tr::now));
	}
	return result;
}

[[nodiscard]] bool CardNeedsBox(const Card &card) {
	return CardTextEditable(card.kind)
		|| (card.targets.size() > 1)
		|| card.optionAvailable
		|| card.canAllowForTurn
		|| (card.kind == CardKind::SendFiles)
		|| (card.kind == CardKind::Download)
		|| (card.kind == CardKind::CreateTask)
		|| (card.kind == CardKind::UpdateTask)
		|| (card.kind == CardKind::Method);
}

[[nodiscard]] HistoryMessageMarkupData CardMarkup(const Card &card) {
	auto result = HistoryMessageMarkupData();
	result.flags = ReplyMarkupFlag::Inline;
	const auto &id = card.toolUseId;
	if (card.kind == CardKind::Handoff) {
		if (card.status == CardStatus::Pending) {
			result.rows.push_back({ MakeButton(
				tr::lng_agent_card_handoff_button(tr::now),
				ChatButtonAction::Handoff,
				id,
				ButtonColor::Primary) });
		}
		return result;
	} else if (card.status != CardStatus::Pending) {
		if (card.kind == CardKind::Download
			&& card.status == CardStatus::Done
			&& !card.detail.isEmpty()) {
			result.rows.push_back({ MakeButton(
				tr::lng_context_show_in_folder(tr::now),
				ChatButtonAction::ShowInFolder,
				card.detail) });
		}
		return result;
	}
	auto first = std::vector<Button>{ MakeButton(
		CardExecuteText(card),
		ChatButtonAction::Execute,
		id,
		ButtonColor::Primary) };
	if (CardHasAlternate(card.kind)) {
		first.push_back(MakeButton(
			((card.kind == CardKind::Send)
				? tr::lng_agent_card_to_draft(tr::now)
				: tr::lng_agent_card_append(tr::now)),
			ChatButtonAction::Alternate,
			id));
	}
	result.rows.push_back(std::move(first));
	auto second = std::vector<Button>();
	if (CardNeedsBox(card)) {
		second.push_back(MakeButton(
			(CardTextEditable(card.kind)
				? tr::lng_agent_chat_card_edit(tr::now)
				: tr::lng_agent_chat_card_details(tr::now)),
			ChatButtonAction::Edit,
			id));
	}
	second.push_back(MakeButton(
		((card.kind == CardKind::Draft)
			? tr::lng_agent_card_cancel(tr::now)
			: tr::lng_agent_card_decline(tr::now)),
		ChatButtonAction::Decline,
		id,
		ButtonColor::Danger));
	result.rows.push_back(std::move(second));
	return result;
}

[[nodiscard]] PhotoData *LocalPhoto(
		not_null<Data::Session*> owner,
		const MediaBlock &block,
		TimeId date,
		const Fn<std::optional<MediaBlock>(const QString&)> &read) {
	auto bytes = block.bytes;
	if (bytes.isEmpty() && read && !block.attachment.isEmpty()) {
		if (const auto full = read(block.attachment)) {
			bytes = full->bytes;
		}
	}
	if (bytes.isEmpty()) {
		return nullptr;
	}
	auto buffer = QBuffer(&bytes);
	auto reader = QImageReader(&buffer);
	const auto size = reader.size();
	if (!size.isValid() || size.isEmpty()) {
		return nullptr;
	}
	const auto large = ImageWithLocation{
		.location = ImageLocation(
			{ .data = InMemoryLocation{ .bytes = bytes } },
			size.width(),
			size.height()),
		.bytes = bytes,
		.bytesCount = int(bytes.size()),
	};
	return owner->photo(
		base::RandomValue<PhotoId>(),
		uint64(0),
		QByteArray(),
		date,
		0,
		false,
		QByteArray(),
		ImageWithLocation(),
		ImageWithLocation(),
		large,
		ImageWithLocation(),
		ImageWithLocation(),
		crl::time(0)).get();
}

} // namespace

struct ChatItems::Prepared {
	QString key;
	QString signature;
	bool out = false;
	bool service = false;
	bool sending = false;
	TimeId date = 0;
	TextWithEntities text;
	std::shared_ptr<const Iv::RichPage> page;
	HistoryMessageMarkupData markup;
	std::vector<QString> undoAll;
	PhotoData *photo = nullptr;
	HistoryItem *forward = nullptr;
	uint64 groupedId = 0;
	QString replyTo;
	QString replyQuote;
	QString replyKey;
	QString modelText;
	TextWithEntities copyText;
};

ChatItems::ChatItems(not_null<History*> history, ChatItemsDelegate delegate)
: _history(history)
, _delegate(std::move(delegate)) {
}

ChatItems::~ChatItems() {
	clear();
}

const std::vector<FullMsgId> &ChatItems::ids() const {
	return _ids;
}

bool ChatItems::contains(FullMsgId id) const {
	return ranges::contains(_ids, id);
}

rpl::producer<ChatButton> ChatItems::buttons() const {
	return _buttons.events();
}

bool ChatItems::apply(
		const std::vector<ChatMessage> &messages,
		const ChatExtras &extras) {
	const auto session = &_history->session();
	const auto run = _delegate.isRun && _delegate.isRun();
	auto prepared = std::vector<Prepared>();
	prepared.reserve(messages.size() + 4);
	auto date = TimeId(0);
	const auto dateFor = [&](int index) {
		const auto known = _delegate.date ? _delegate.date(index) : 0;
		date = std::max(date, known ? known : base::unixtime::now());
		return date;
	};
	const auto lastDate = [&] {
		return date ? date : base::unixtime::now();
	};
	if (!extras.note.isEmpty()) {
		prepared.push_back({
			.key = u"note"_q,
			.signature = extras.note,
			.service = true,
			.date = dateFor(0),
			.text = TextWithEntities{ extras.note },
		});
	}
	const auto count = int(messages.size());
	for (auto i = 0; i != count; ++i) {
		const auto &message = messages[i];
		auto entry = Prepared{
			.key = message.key,
			.date = dateFor(message.index),
		};
		auto missing = 0;
		auto forwards = std::vector<Prepared>();
		auto album = std::vector<Prepared>();
		if (message.kind == ChatMessageKind::User && _delegate.passed) {
			auto k = 0;
			for (const auto &id : _delegate.passed(message.index)) {
				const auto original = _history->owner().message(id);
				if (!original) {
					++missing;
					continue;
				}
				forwards.push_back({
					.key = message.key + u".p"_q + QString::number(k++),
					.signature = u"p"_q + QString::number(id.msg.bare),
					.out = true,
					.date = entry.date,
					.forward = original,
				});
			}
		}
		const auto lastLive = extras.live && (i + 1 == count);
		switch (message.kind) {
		case ChatMessageKind::User: {
			entry.out = true;
			entry.text = TextWithEntities{ message.text };
			entry.signature = u"u"_q + message.text
				+ u"|r"_q + message.replyTo + u"|"_q + message.replyQuote;
			entry.replyTo = message.replyTo;
			entry.replyQuote = message.replyQuote;
			entry.replyKey = message.key;
			entry.modelText = message.text;
			entry.copyText = entry.text;
			auto photos = std::vector<PhotoData*>();
			auto files = QStringList();
			for (const auto &attachment : message.attachments) {
				entry.signature += u"|"_q + attachment.attachment;
				const auto photo = (attachment.kind == MediaKind::Image)
					? localPhoto(attachment, entry.date)
					: nullptr;
				if (photo) {
					photos.push_back(photo);
				} else {
					files.push_back(AttachmentLine(attachment));
				}
			}
			if (missing) {
				files.push_front(QString(QChar(0x21AA)) + ' '
					+ tr::lng_forward_messages(tr::now, lt_count, missing));
			}
			if (!files.isEmpty()) {
				auto lines = TextWithEntities{ files.join('\n') };
				entry.text = entry.text.empty()
					? lines
					: lines.append(u"\n\n"_q).append(entry.text);
			}
			if (!photos.empty()) {
				entry.photo = photos.front();
			}
			if (photos.size() > 1) {
				entry.groupedId = groupIdFor(message.key);
				for (auto k = 1; k != int(photos.size()); ++k) {
					album.push_back({
						.key = message.key + u".m"_q + QString::number(k),
						.signature = entry.signature,
						.out = true,
						.date = entry.date,
						.photo = photos[k],
						.groupedId = entry.groupedId,
						.replyKey = message.key,
						.modelText = message.text,
					});
				}
			}
		} break;
		case ChatMessageKind::TurnEnd: {
			entry.service = true;
			entry.text = TextWithEntities{ TurnEndText(message.turnEnd) };
			entry.signature = u"e"_q + entry.text.text;
		} break;
		case ChatMessageKind::Summary: {
			entry.service = true;
			entry.text = TextWithEntities{ tr::lng_agent_compacted(tr::now) };
			entry.signature = u"s"_q;
		} break;
		case ChatMessageKind::Card: {
			const auto &card = message.card->card;
			entry.text = CardBody(card);
			const auto status = CardStatusLine(card, run);
			if (!status.isEmpty()) {
				entry.text.append('\n').append(::Ui::Text::Italic(status));
			}
			entry.markup = CardMarkup(card);
			entry.replyKey = message.key;
			entry.modelText = entry.text.text;
			entry.signature = u"c"_q
				+ QString::number(int(card.status))
				+ entry.text.text
				+ QString::number(CheckedCount(card));
		} break;
		case ChatMessageKind::Agent: {
			auto signature = u"a"_q + message.text
				+ (lastLive ? u"~"_q : QString());
			for (const auto &step : message.steps) {
				v::match(step, [&](const FeedThinking &data) {
					signature += u"|t"_q + QString::number(data.text.size());
				}, [&](const FeedTool &data) {
					signature += u"|"_q + data.call.id
						+ (data.call.input.isEmpty() ? u"-"_q : u"+"_q)
						+ (data.result
							? (data.result->error ? u"!"_q : u"="_q)
							: QString());
				});
			}
			auto undoable = std::vector<QString>();
			auto lines = QStringList();
			for (const auto &action : message.actions) {
				const auto id = action.call.id;
				const auto undone = _delegate.isUndone
					&& _delegate.isUndone(id);
				const auto can = !undone
					&& _delegate.canUndo
					&& _delegate.canUndo(id);
				signature += u"|x"_q + id
					+ (action.result ? u"="_q : QString())
					+ (undone ? u"u"_q : can ? u"c"_q : QString());
				auto line = ActionLine(action);
				if (undone) {
					line += u" — "_q + tr::lng_agent_undone(tr::now);
				}
				lines.push_back(line);
				if (can) {
					undoable.push_back(id);
				}
			}
			entry.signature = signature;
			const auto thinking = (lastLive
				&& !message.steps.empty()
				&& message.text.isEmpty()
				&& std::holds_alternative<FeedThinking>(
					message.steps.back()));
			auto blocks = std::vector<Block>();
			if (!message.steps.empty()) {
				blocks.push_back(StepsBlock(
					message.steps,
					lastLive,
					session,
					[&](const MediaBlock &media) {
						return localPhoto(media, entry.date);
					}));
			}
			if (thinking) {
				const auto &text = std::get<FeedThinking>(
					message.steps.back()).text;
				blocks.push_back({
					.kind = BlockKind::Thinking,
					.text = Rich(text.right(kThinkingPreview)),
				});
			}
			if (!lines.isEmpty()) {
				blocks.push_back(Paragraph(TextWithEntities{
					lines.join('\n') }));
			}
			if (!message.text.isEmpty()) {
				auto markdown = MarkdownBlocks(message.text);
				blocks.insert(
					end(blocks),
					std::make_move_iterator(begin(markdown)),
					std::make_move_iterator(end(markdown)));
			}
			auto page = Iv::RichPage();
			page.blocks = std::move(blocks);
			const auto simple = (message.steps.empty()
				&& lines.isEmpty()
				&& Iv::CanSerializeAsSimple(page, session))
				? Iv::SerializeAsSimple(page, session)
				: std::nullopt;
			if (simple) {
				entry.text = *simple;
			} else if (!page.blocks.empty()) {
				entry.page = std::make_shared<Iv::RichPage>(std::move(page));
				if (message.text.isEmpty()) {
					entry.text = Iv::FlattenRichPageSummary(entry.page);
				} else {
					auto answer = Iv::RichPage();
					answer.blocks = MarkdownBlocks(message.text);
					entry.text = Iv::FlattenRichPageSummary(answer);
				}
			} else {
				entry.text = TextWithEntities{
					tr::lng_agent_thinking(tr::now) };
			}
			if (!undoable.empty()) {
				entry.markup.flags = ReplyMarkupFlag::Inline;
				if (undoable.size() == 1) {
					entry.markup.rows.push_back({ MakeButton(
						tr::lng_agent_undo(tr::now),
						ChatButtonAction::Undo,
						undoable.front()) });
				} else {
					entry.markup.rows.push_back({ MakeButton(
						tr::lng_agent_undo_all(tr::now),
						ChatButtonAction::UndoAll,
						message.key) });
					entry.undoAll = undoable;
				}
			}
			entry.replyKey = message.key;
			entry.modelText = message.text.isEmpty()
				? entry.text.text
				: message.text;
			if (!message.text.isEmpty()) {
				auto answer = Iv::RichPage();
				answer.blocks = MarkdownBlocks(message.text);
				entry.copyText = Iv::FlattenRichPageToSimpleText(answer);
			}
		} break;
		}
		if (message.kind != ChatMessageKind::User
			|| !message.text.isEmpty()
			|| !message.attachments.empty()
			|| missing) {
			prepared.push_back(std::move(entry));
		}
		for (auto &part : album) {
			prepared.push_back(std::move(part));
		}
		for (auto &forward : forwards) {
			prepared.push_back(std::move(forward));
		}
	}
	if (extras.compacting) {
		prepared.push_back({
			.key = u"compacting"_q,
			.signature = u"compacting"_q,
			.service = true,
			.date = dateFor(-1),
			.text = TextWithEntities{ tr::lng_agent_compacting(tr::now) },
		});
	}
	for (auto i = 0; i != int(extras.queued.size()); ++i) {
		prepared.push_back({
			.key = u"q"_q + QString::number(i),
			.signature = u"q"_q + extras.queued[i],
			.out = true,
			.sending = true,
			.date = dateFor(-1),
			.text = TextWithEntities{ extras.queued[i] },
		});
	}
	if (!extras.error.isEmpty()) {
		auto entry = Prepared{
			.key = u"error"_q,
			.signature = extras.error
				+ (extras.errorSettings ? u"+"_q : QString()),
			.date = lastDate(),
			.text = TextWithEntities{ extras.error },
		};
		entry.markup.flags = ReplyMarkupFlag::Inline;
		auto row = std::vector<Button>{ MakeButton(
			tr::lng_agent_retry(tr::now),
			ChatButtonAction::Retry,
			QString(),
			ButtonColor::Primary) };
		if (extras.errorSettings) {
			row.push_back(MakeButton(
				tr::lng_agent_model_settings(tr::now),
				ChatButtonAction::ModelSettings,
				QString()));
		}
		entry.markup.rows.push_back(std::move(row));
		prepared.push_back(std::move(entry));
	}
	if (!extras.spending.isEmpty()) {
		prepared.push_back({
			.key = u"spending"_q,
			.signature = extras.spending,
			.service = true,
			.date = lastDate(),
			.text = TextWithEntities{ extras.spending },
		});
	}

	auto entries = std::vector<Entry>();
	entries.reserve(prepared.size());
	_undoAll.clear();
	const auto findId = [&](const QString &key) {
		if (key.isEmpty()) {
			return FullMsgId();
		}
		const auto i = ranges::find(entries, key, &Entry::key);
		return (i != end(entries)) ? i->id : FullMsgId();
	};
	for (const auto &data : prepared) {
		if (!data.undoAll.empty()) {
			_undoAll.emplace(data.key, data.undoAll);
		}
		const auto i = ranges::find(_entries, data.key, &Entry::key);
		if (i != end(_entries)) {
			const auto item = _history->owner().message(i->id);
			if (item
				&& i->service == data.service
				&& i->out == data.out
				&& i->photo == data.photo
				&& i->groupedId == data.groupedId
				&& !(item->isSending() && !data.sending)) {
				if (i->signature != data.signature) {
					update(item, data);
					i->signature = data.signature;
				}
				i->replyKey = data.replyKey;
				i->modelText = data.modelText;
				i->copyText = data.copyText;
				entries.push_back(base::take(*i));
				_entries.erase(i);
				continue;
			}
			destroy(*i);
			_entries.erase(i);
		}
		if (const auto item = create(data, findId(data.replyTo))) {
			entries.push_back({
				.key = data.key,
				.signature = data.signature,
				.id = item->fullId(),
				.service = data.service,
				.out = data.out,
				.photo = data.photo,
				.groupedId = data.groupedId,
				.replyKey = data.replyKey,
				.modelText = data.modelText,
				.copyText = data.copyText,
			});
		}
	}
	for (const auto &entry : base::take(_entries)) {
		destroy(entry);
	}
	_entries = std::move(entries);
	auto ids = ranges::views::all(
		_entries
	) | ranges::views::transform(&Entry::id) | ranges::to_vector;
	if (ids == _ids) {
		return false;
	}
	_ids = std::move(ids);
	return true;
}

PhotoData *ChatItems::localPhoto(const MediaBlock &block, TimeId date) {
	const auto i = _photos.find(block.attachment);
	if (i != end(_photos)) {
		return i->second;
	}
	const auto result = LocalPhoto(
		&_history->owner(),
		block,
		date,
		_delegate.attachment);
	if (result) {
		_photos.emplace(block.attachment, result);
	}
	return result;
}

uint64 ChatItems::groupIdFor(const QString &key) {
	const auto i = _groupIds.find(key);
	if (i != end(_groupIds)) {
		return i->second;
	}
	return _groupIds.emplace(
		key,
		base::RandomValue<uint64>() | 1).first->second;
}

std::optional<ChatItemInfo> ChatItems::Info(
		not_null<const HistoryItem*> item) {
	const auto i = Registry().find(item->fullId());
	if (i == end(Registry())) {
		return std::nullopt;
	}
	const auto &entries = i->second->_entries;
	const auto j = ranges::find(entries, item->fullId(), &Entry::id);
	if (j == end(entries) || j->replyKey.isEmpty()) {
		return std::nullopt;
	}
	const auto &delegate = i->second->_delegate;
	return ChatItemInfo{
		.key = j->replyKey,
		.modelText = j->modelText,
		.copyText = j->copyText,
		.readOnly = delegate.isRun && delegate.isRun(),
	};
}

HistoryItem *ChatItems::create(const Prepared &prepared, FullMsgId replyTo) {
	const auto session = &_history->session();
	const auto agent = session->agent().chat().user();
	auto flags = MessageFlag::Local | MessageFlag::FakeHistoryItem;
	if (prepared.service) {
		const auto item = _history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = flags,
			.date = prepared.date,
		}, PreparedServiceText{ prepared.text });
		Registry().emplace(item->fullId(), this);
		return item;
	}
	flags |= MessageFlag::HasFromId;
	if (prepared.out) {
		flags |= MessageFlag::Outgoing;
	}
	if (prepared.sending) {
		flags |= MessageFlag::BeingSent;
	}
	if (!prepared.markup.isNull()) {
		flags |= MessageFlag::HasReplyMarkup;
	}
	if (replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	auto markup = prepared.markup;
	auto fields = HistoryItemCommonFields{
		.id = _history->nextNonHistoryEntryId(),
		.flags = flags,
		.from = prepared.out ? session->userPeerId() : agent->id,
		.replyTo = FullReplyTo{
			.messageId = replyTo,
			.quote = TextWithEntities{ prepared.replyQuote },
		},
		.date = prepared.date,
		.groupedId = prepared.groupedId,
		.markup = std::move(markup),
	};
	const auto item = prepared.forward
		? _history->makeMessage(
			std::move(fields),
			not_null<HistoryItem*>(prepared.forward))
		: prepared.photo
		? _history->makeMessage(
			std::move(fields),
			not_null<PhotoData*>(prepared.photo),
			prepared.text)
		: _history->makeMessage(
			std::move(fields),
			prepared.text,
			MTP_messageMediaEmpty());
	if (prepared.page) {
		item->setRichPage(prepared.page);
	}
	Registry().emplace(item->fullId(), this);
	return item;
}

void ChatItems::update(
		not_null<HistoryItem*> item,
		const Prepared &prepared) {
	if (prepared.service) {
		item->updateServiceText(PreparedServiceText{ prepared.text });
		return;
	}
	if (prepared.page) {
		item->setRichPage(prepared.page);
	} else {
		item->clearRichPage();
	}
	item->setText(prepared.text);
	auto markup = prepared.markup;
	item->updateReplyMarkup(std::move(markup));
	_history->owner().requestItemTextRefresh(item);
}

void ChatItems::destroy(const Entry &entry) {
	Registry().remove(entry.id);
	if (const auto item = _history->owner().message(entry.id)) {
		item->destroy();
	}
}

void ChatItems::clear() {
	for (const auto &entry : base::take(_entries)) {
		destroy(entry);
	}
	_ids.clear();
	_undoAll.clear();
	_photos.clear();
	_groupIds.clear();
}

bool ChatItems::Activate(
		not_null<HistoryItem*> item,
		const HistoryMessageMarkupButton &button) {
	if (!IsAgentHistory(item->history())) {
		return false;
	}
	const auto parsed = ParseButtonData(button.data);
	const auto i = Registry().find(item->fullId());
	if (!parsed || i == end(Registry())) {
		return true;
	}
	const auto items = i->second;
	if (parsed->action == ChatButtonAction::UndoAll) {
		const auto j = items->_undoAll.find(parsed->id);
		if (j != end(items->_undoAll)) {
			auto all = QStringList();
			for (const auto &id : j->second) {
				all.push_back(id);
			}
			items->_buttons.fire({
				.action = ChatButtonAction::UndoAll,
				.id = all.join(','),
			});
		}
		return true;
	}
	items->_buttons.fire_copy(*parsed);
	return true;
}

} // namespace Agent::Ui
