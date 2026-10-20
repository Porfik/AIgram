/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_items.h"

#include "agent/core/agent_prompts.h"
#include "agent/ui/agent_feed_media.h"
#include "base/assertion.h"
#include "base/variant.h"
#include "core/click_handler_types.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_math_renderer.h"
#include "iv/markdown/iv_markdown_parse.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/markdown/iv_markdown_view_widget.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/text/format_values.h"
#include "ui/basic_click_handlers.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/selecting_scroll.h"
#include "window/window_session_controller.h"

#include "styles/style_agent.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonDocument>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

namespace Agent::Ui {
namespace {

constexpr auto kStreamRenderDelay = crl::time(100);
constexpr auto kMaxResultChars = 3000;

[[nodiscard]] QJsonObject ToolResultObject(
		const std::optional<ToolResultBlock> &result) {
	if (!result || result->error) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(
		UnwrapUntrusted(result->content).toUtf8());
	return document.isObject() ? document.object() : QJsonObject();
}

[[nodiscard]] int64 JsonIdValue(const QJsonValue &value) {
	return value.isString()
		? value.toString().toLongLong()
		: int64(value.toDouble());
}

[[nodiscard]] HistoryItem *ToolResultMessage(
		Main::Session *session,
		const QJsonObject &input,
		const QJsonObject &object) {
	if (!session) {
		return nullptr;
	}
	const auto chat = object.contains(u"chat"_q)
		? object.value(u"chat"_q)
		: input.value(u"chat"_q);
	if (chat.isString() && !chat.toString().isEmpty()
		&& !chat.toString().at(0).isDigit()) {
		return nullptr;
	}
	const auto chatId = chat.isUndefined() ? 0 : JsonIdValue(chat);
	const auto messageId = object.contains(u"message"_q)
		? object.value(u"message"_q)
		: input.value(u"message_id"_q);
	if (!chatId || messageId.isUndefined()) {
		return nullptr;
	}
	return session->data().message(
		PeerId(uint64(chatId)),
		MsgId(JsonIdValue(messageId)));
}

[[nodiscard]] int64 MediaDurationSeconds(HistoryItem *item) {
	if (!item || !item->media()) {
		return 0;
	}
	const auto document = item->media()->document();
	return (document && document->hasDuration())
		? (document->duration() + 500) / 1000
		: 0;
}

[[nodiscard]] int64 ResultDurationSeconds(
		const QJsonObject &object,
		HistoryItem *item) {
	const auto value = object.value(u"duration"_q);
	return value.isDouble() ? int64(value.toDouble()) : MediaDurationSeconds(item);
}

[[nodiscard]] QString WithDuration(QString title, int64 seconds) {
	if (seconds <= 0) {
		return title;
	}
	const auto hours = seconds / 3600;
	const auto minutes = (seconds % 3600) / 60;
	const auto rest = seconds % 60;
	const auto text = hours
		? u"%1:%2:%3"_q.arg(hours)
			.arg(minutes, 2, 10, QChar('0'))
			.arg(rest, 2, 10, QChar('0'))
		: u"%1:%2"_q.arg(minutes).arg(rest, 2, 10, QChar('0'));
	return title + ' ' + text;
}

[[nodiscard]] bool ResultFromMediaModel(const QJsonObject &object) {
	return object.value(u"media_model"_q).toBool();
}

[[nodiscard]] QString ToolTitleBaseText(
		const ToolUseBlock &call,
		const std::optional<ToolResultBlock> &result,
		Main::Session *session) {
	const auto &name = call.name;
	if (name == u"get_context"_q) {
		return tr::lng_agent_tool_get_context(tr::now);
	} else if (name == u"read_history"_q) {
		return tr::lng_agent_tool_read_history(tr::now);
	} else if (name == u"search_messages"_q) {
		return call.input.value("chat").isUndefined()
			? tr::lng_agent_tool_search_global(tr::now)
			: tr::lng_agent_tool_search_messages(tr::now);
	} else if (name == u"get_peer_info"_q) {
		return tr::lng_agent_tool_get_peer_info(tr::now);
	} else if (name == u"list_members"_q) {
		const auto filter = call.input.value(u"filter"_q).toString();
		if (filter == u"admins"_q) {
			return tr::lng_agent_tool_list_admins(tr::now);
		} else if (filter == u"banned"_q
			|| filter == u"restricted"_q) {
			return tr::lng_agent_tool_list_banned(tr::now);
		}
		return tr::lng_agent_tool_list_members(tr::now);
	} else if (name == u"list_chats"_q) {
		return tr::lng_agent_tool_list_chats(tr::now);
	} else if (name == u"find_setting"_q) {
		return tr::lng_agent_tool_find_setting(tr::now);
	} else if (name == u"open"_q) {
		return tr::lng_agent_tool_open(tr::now);
	} else if (name == u"load_tools"_q) {
		return tr::lng_agent_tool_load_tools(tr::now);
	} else if (name == u"list_folders"_q) {
		return tr::lng_agent_tool_list_folders(tr::now);
	} else if (name == u"read_chat_memory"_q) {
		return tr::lng_agent_tool_read_chat_memory(tr::now);
	} else if (name == u"view_media"_q) {
		const auto object = ToolResultObject(result);
		const auto type = object.value(u"type"_q).toString();
		if (type == u"photo"_q) {
			return tr::lng_agent_tool_view_photo(tr::now);
		} else if (type == u"video"_q
			|| type == u"round_video"_q
			|| type == u"gif"_q) {
			return tr::lng_agent_tool_view_video(tr::now);
		} else if (!object.value(u"attachment"_q).toString().isEmpty()) {
			return tr::lng_agent_tool_view_attachment(tr::now);
		}
		return tr::lng_agent_tool_view_media(tr::now);
	} else if (name == u"listen_audio"_q) {
		const auto object = ToolResultObject(result);
		const auto type = object.value(u"type"_q).toString();
		const auto title = (type == u"voice"_q || type == u"round_video"_q)
			? tr::lng_agent_tool_listen_voice(tr::now)
			: tr::lng_agent_tool_listen_audio(tr::now);
		return WithDuration(
			std::move(title),
			ResultDurationSeconds(
				object,
				ToolResultMessage(session, call.input, object)));
	} else if (name == u"transcribe_voice"_q) {
		const auto object = ToolResultObject(result);
		return WithDuration(
			tr::lng_agent_tool_transcribe(tr::now),
			MediaDurationSeconds(
				ToolResultMessage(session, call.input, object)));
	} else if (name == u"read_file"_q) {
		const auto fileName = ToolResultObject(result)
			.value(u"name"_q).toString();
		return fileName.isEmpty()
			? tr::lng_agent_tool_read_file_unknown(tr::now)
			: tr::lng_agent_tool_read_file(tr::now, lt_file, fileName);
	} else if (name == u"list_agent_files"_q) {
		return tr::lng_agent_tool_list_agent_files(tr::now);
	} else if (name == u"draft_files"_q) {
		return tr::lng_agent_tool_draft_files(tr::now);
	} else if (name == u"list_tasks"_q) {
		return tr::lng_agent_tool_list_tasks(tr::now);
	} else if (name == u"notify"_q) {
		return tr::lng_agent_tool_notify(tr::now);
	} else if (name == u"list_invite_links"_q) {
		return tr::lng_agent_tool_list_invite_links(tr::now);
	} else if (name == u"list_join_requests"_q) {
		return tr::lng_agent_tool_list_join_requests(tr::now);
	} else if (name == u"list_contacts"_q) {
		return tr::lng_agent_tool_list_contacts(tr::now);
	} else if (name == u"list_blocked"_q) {
		return tr::lng_agent_tool_list_blocked(tr::now);
	} else if (name == u"list_emoji_statuses"_q) {
		return tr::lng_agent_tool_list_emoji_statuses(tr::now);
	} else if (name == u"summarize_message"_q) {
		return tr::lng_agent_tool_summarize(tr::now);
	} else if (name == u"translate"_q) {
		return tr::lng_agent_tool_translate(tr::now);
	} else if (name == u"ai_compose"_q) {
		return tr::lng_agent_tool_ai_compose(tr::now);
	} else if (name == u"find_methods"_q) {
		return tr::lng_agent_tool_find_methods(tr::now);
	} else if (name == u"describe_method"_q) {
		const auto method = call.input.value(u"name"_q).toString().trimmed();
		return method.isEmpty()
			? tr::lng_agent_tool_other(tr::now, lt_name, name)
			: tr::lng_agent_tool_describe_method(
				tr::now,
				lt_name,
				method.left(64));
	} else if (name == u"call_method"_q) {
		const auto method = call.input.value(u"method"_q).toString().trimmed();
		return method.isEmpty()
			? tr::lng_agent_tool_other(tr::now, lt_name, name)
			: tr::lng_agent_tool_call_method(
				tr::now,
				lt_name,
				method.left(64));
	}
	return tr::lng_agent_tool_other(tr::now, lt_name, name);
}

[[nodiscard]] QString ToolTitleText(
		const ToolUseBlock &call,
		const std::optional<ToolResultBlock> &result,
		Main::Session *session) {
	auto title = ToolTitleBaseText(call, result, session);
	if (ResultFromMediaModel(ToolResultObject(result))) {
		title += u" "_q + tr::lng_agent_tool_media_model(tr::now);
	}
	return title;
}

[[nodiscard]] bool IsMediaTool(const QString &name) {
	return name == u"view_media"_q
		|| name == u"listen_audio"_q
		|| name == u"transcribe_voice"_q
		|| name == u"read_file"_q;
}

[[nodiscard]] MediaBlock ResultMediaBlock(const QJsonObject &object) {
	auto block = MediaBlock();
	block.attachment = object.value(u"attachment"_q).toString();
	if (!block.attachment.isEmpty()) {
		return block;
	}
	const auto chat = object.value(u"chat"_q);
	if (chat.isString() && !chat.toString().isEmpty()
		&& !chat.toString().at(0).isDigit()) {
		return block;
	}
	block.peer = chat.isUndefined() ? 0 : uint64(JsonIdValue(chat));
	block.messageId = object.value(u"message"_q).isUndefined()
		? 0
		: JsonIdValue(object.value(u"message"_q));
	return block;
}

[[nodiscard]] QString ResultTranscriptText(
		const QString &name,
		const QJsonObject &object) {
	if (name == u"listen_audio"_q) {
		return object.value(u"transcript"_q).toString();
	} else if (name == u"view_media"_q) {
		return object.value(u"description"_q).toString();
	} else if (name == u"transcribe_voice"_q) {
		return object.value(u"text"_q).toString();
	} else if (name == u"read_file"_q
		&& object.value(u"media_model"_q).toBool()) {
		return object.value(u"text"_q).toString();
	}
	return QString();
}

[[nodiscard]] QString CutForDisplay(const QString &text) {
	if (text.size() <= kMaxResultChars) {
		return text;
	}
	auto cut = text.left(kMaxResultChars);
	while (!cut.isEmpty() && cut.back().isHighSurrogate()) {
		cut.chop(1);
	}
	return cut + QChar(0x2026);
}

[[nodiscard]] QString IndentedJson(const QJsonDocument &document) {
	const auto lines = QString::fromUtf8(
		document.toJson(QJsonDocument::Indented)).trimmed().split('\n');
	auto result = QStringList();
	result.reserve(lines.size());
	for (const auto &line : lines) {
		auto spaces = 0;
		while (spaces < line.size() && line[spaces] == ' ') {
			++spaces;
		}
		result.push_back(QString(spaces / 2, ' ') + line.mid(spaces));
	}
	return result.join('\n');
}

[[nodiscard]] QString PrettyJson(const QString &text) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || document.isNull()) {
		return text;
	}
	return IndentedJson(document);
}

[[nodiscard]] QString ArgumentsText(const ToolUseBlock &call) {
	if (!call.inputError.isEmpty()) {
		return QString::fromUtf8(call.rawInput);
	} else if (call.input.isEmpty()) {
		return QString();
	}
	return IndentedJson(QJsonDocument(call.input));
}

[[nodiscard]] QString ResultText(const ToolResultBlock &result) {
	return CutForDisplay(PrettyJson(UnwrapUntrusted(result.content)));
}

[[nodiscard]] TextWithEntities Monospace(QString text) {
	auto entities = EntitiesInText();
	if (!text.isEmpty()) {
		entities.push_back(EntityInText(EntityType::Code, 0, text.size()));
	}
	return { std::move(text), std::move(entities) };
}

void PaintChevron(QPainter &p, QRect rect, bool expanded, QColor color) {
	auto pen = QPen(color);
	pen.setWidthF(st::agentStepsChevronStroke);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto w = rect.width() / 2.;
	const auto h = rect.height() / 2.;
	const auto center = QRectF(rect).center();
	auto path = QPainterPath();
	if (expanded) {
		path.moveTo(center.x() - w, center.y() - h / 2.);
		path.lineTo(center.x(), center.y() + h / 2.);
		path.lineTo(center.x() + w, center.y() - h / 2.);
	} else {
		path.moveTo(center.x() - w / 2., center.y() - h);
		path.lineTo(center.x() + w / 2., center.y());
		path.lineTo(center.x() - w / 2., center.y() + h);
	}
	p.drawPath(path);
}

void ActivateFeedLink(
		not_null<Iv::Markdown::MarkdownDocumentWidget*> body,
		const Iv::Markdown::PreparedLink &link,
		Qt::MouseButton button,
		const QVariant &context) {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return;
	}
	using Kind = Iv::Markdown::PreparedLinkKind;
	switch (link.kind) {
	case Kind::External: {
		if (link.target.isEmpty()) {
			return;
		}
		switch (link.entityType) {
		case EntityType::Url:
		case EntityType::CustomUrl:
		case EntityType::Email:
			break;
		default:
			return;
		}
		const auto handler = Ui::Integration::Instance().createLinkHandler(
			EntityLinkData{
				.text = (!link.copyText.isEmpty()
					? link.copyText
					: link.target),
				.data = link.target,
				.type = link.entityType,
				.shown = link.shown,
			},
			Ui::Text::MarkedContext());
		if (handler) {
			auto click = ClickContext();
			click.button = button;
			click.other = context;
			handler->onClick(std::move(click));
		}
	} break;
	case Kind::InstantViewPage: {
		auto target = link.target;
		if (!link.fragment.isEmpty()) {
			target += u"#"_q + link.fragment;
		}
		UrlClickHandler::Open(target, context);
	} break;
	case Kind::ToggleDetails:
		body->toggleDetails(link.target);
		break;
	case Kind::ToggleBlockquote:
		body->toggleBlockquote(link.target);
		break;
	default:
		break;
	}
}

[[nodiscard]] bool ActivateFeedMedia(
		const Iv::Markdown::MediaActivation &activation,
		Qt::MouseButton button,
		const QVariant &context) {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return false;
	}
	if (activation.kind == Iv::Markdown::MediaActivationKind::ExternalUrl
		&& !activation.url.isEmpty()) {
		HiddenUrlClickHandler::Open(activation.url, context);
		return true;
	}
	return false;
}

class CodeBox final : public RpWidget {
public:
	CodeBox(QWidget *parent, const QString &text);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<FlatLabel> _label;

};

CodeBox::CodeBox(QWidget *parent, const QString &text)
: RpWidget(parent)
, _label(this, QString(), st::agentStepsCodeLabel) {
	_label->setMarkedText(Monospace(text));
	_label->setSelectable(true);
}

int CodeBox::resizeGetHeight(int newWidth) {
	const auto &padding = st::agentStepsCodePadding;
	_label->resizeToWidth(
		std::max(newWidth - padding.left() - padding.right(), 1));
	_label->moveToLeft(padding.left(), padding.top(), newWidth);
	return padding.top() + _label->height() + padding.bottom();
}

void CodeBox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::agentStepsCodeBg);
	const auto radius = st::agentStepsCodeRadius;
	p.drawRoundedRect(rect(), radius, radius);
}

} // namespace

object_ptr<RpWidget> MakeCodeBlock(QWidget *parent, const QString &text) {
	return object_ptr<CodeBox>(parent, text);
}

class ToggleRow final : public AbstractButton {
public:
	ToggleRow(QWidget *parent, int height);

	void setTexts(QString title, QString failed);
	void setExpanded(bool expanded);
	void setActive(bool active);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	const int _height = 0;
	QString _title;
	QString _failed;
	InfiniteRadialAnimation _spinner;
	bool _expanded = false;
	bool _active = false;

};

ToggleRow::ToggleRow(QWidget *parent, int height)
: AbstractButton(parent)
, _height(height)
, _spinner([=] { update(); }, st::agentStepsSpinner) {
	resize(width(), _height);
}

void ToggleRow::setTexts(QString title, QString failed) {
	_title = std::move(title);
	_failed = std::move(failed);
	update();
}

void ToggleRow::setExpanded(bool expanded) {
	_expanded = expanded;
	update();
}

void ToggleRow::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	if (active) {
		_spinner.start();
	} else {
		_spinner.stop(anim::type::instant);
	}
	update();
}

int ToggleRow::resizeGetHeight(int newWidth) {
	return _height;
}

void ToggleRow::onStateChanged(State was, StateChangeSource source) {
	update();
}

void ToggleRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto fg = isOver()
		? st::agentStepsHeaderFgOver->c
		: st::agentStepsHeaderFg->c;
	const auto h = height();
	if (_active) {
		const auto size = st::agentStepsSpinnerSize;
		_spinner.draw(
			p,
			QPoint(0, (h - size.height()) / 2),
			size,
			width());
	} else {
		const auto size = st::agentStepsChevronSize;
		PaintChevron(
			p,
			QRect(
				(st::agentStepsSpinnerSize.width() - size) / 2,
				(h - size) / 2,
				size,
				size),
			_expanded,
			fg);
	}
	const auto &font = st::normalFont;
	p.setFont(font);
	const auto left = st::agentStepsTitleLeft;
	const auto available = std::max(width() - left, 0);
	const auto failed = _failed.isEmpty()
		? QString()
		: (u" · "_q + _failed);
	const auto failedWidth = font->width(failed);
	const auto titleWidth = std::min(
		font->width(_title),
		std::max(available - failedWidth, available / 2));
	const auto baseline = (h - font->height) / 2 + font->ascent;
	p.setPen(fg);
	p.drawText(left, baseline, font->elided(_title, titleWidth));
	if (!failed.isEmpty()) {
		p.setPen(st::agentStepsFailedFg);
		p.drawText(
			left + titleWidth,
			baseline,
			font->elided(failed, available - titleWidth));
	}
}

BubbleWidget::BubbleWidget(
	QWidget *parent,
	const style::FlatLabel &st,
	style::color bg,
	int radius,
	const style::margins &padding,
	int maxWidth)
: FeedItemWidget(parent)
, _bg(bg)
, _radius(radius)
, _padding(padding)
, _maxWidth(maxWidth)
, _label(this, QString(), st) {
}

BubbleWidget *BubbleWidget::setText(const QString &text) {
	_label->setText(text);
	setNaturalWidth(std::min(
		_label->naturalWidth() + _padding.left() + _padding.right(),
		_maxWidth));
	resizeToWidth(naturalWidth());
	return this;
}

int BubbleWidget::resizeGetHeight(int newWidth) {
	const auto inner = std::max(
		newWidth - _padding.left() - _padding.right(),
		1);
	_label->resizeToWidth(inner);
	_label->moveToLeft(_padding.left(), _padding.top(), newWidth);
	return _padding.top() + _label->height() + _padding.bottom();
}

void BubbleWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_bg);
	p.drawRoundedRect(rect(), _radius, _radius);
}

FeedAssistantWidget::FeedAssistantWidget(
	QWidget *parent,
	FeedContext context,
	QString text)
: FeedItemWidget(parent)
, _context(std::move(context))
, _source(std::move(text))
, _body(this)
, _fallback(this, QString(), st::agentFeedTextLabel)
, _renderTimer([=] { renderNow(); }) {
	_body->hide();
	_fallback->hide();
	_fallback->setSelectable(true);
	auto clickContext = ClickHandlerContext();
	clickContext.sessionWindow = base::make_weak(_context.controller);
	clickContext.show = _context.controller->uiShow();
	const auto clickVariant = QVariant::fromValue(clickContext);
	const auto body = _body.data();
	body->setClickHandlerContext(clickVariant);
	body->setLinkActivationCallback([=](
			const Iv::Markdown::PreparedLink &link,
			Qt::MouseButton button) {
		ActivateFeedLink(body, link, button, clickVariant);
	});
	body->setMediaActivationCallback([=](
			const Iv::Markdown::MediaActivation &activation,
			Qt::MouseButton button) {
		return ActivateFeedMedia(activation, button, clickVariant);
	});
	style::PaletteChanged(
	) | rpl::on_next([=] {
		_body->refreshPalette();
	}, _body->lifetime());
	if (_context.scrollBy) {
		Ui::SetupSelectingScroll(_body, _context.scrollBy);
	}
	if (!_source.isEmpty()) {
		renderNow();
	}
}

FeedAssistantWidget::~FeedAssistantWidget() = default;

void FeedAssistantWidget::streamDelta(QString delta) {
	_source += std::move(delta);
	if (!_renderScheduled) {
		_renderScheduled = true;
		_renderTimer.callOnce(kStreamRenderDelay);
	}
}

void FeedAssistantWidget::renderNow() {
	_renderScheduled = false;
	if (_source.isEmpty()) {
		return;
	}
	const auto validated = Iv::Markdown::ValidateMarkdownSourceForIv(
		_source.toUtf8(),
		Iv::Markdown::ParseOptions{ u"agent"_q });
	if (!validated.ok) {
		showFallback(_source);
		return;
	}
	const auto parsed = Iv::Markdown::ParseMarkdownForIv(
		std::move(validated.source));
	if (!parsed.ok) {
		showFallback(_source);
		return;
	}
	auto prepared = Iv::Markdown::PrepareSynchronously({
		.document = std::make_shared<const Iv::Markdown::PreparedDocument>(
			std::move(parsed.document)),
		.renderer = _context.renderer,
		.dimensions = Iv::Markdown::CaptureMarkdownPrepareDimensions(
			st::messageMarkdown),
	});
	if (prepared.failure.failed()) {
		showFallback(_source);
		return;
	}
	const auto restore = !_article;
	if (restore) {
		_article = std::make_shared<Iv::Markdown::MarkdownArticle>(
			st::messageMarkdown,
			_context.renderer);
	}
	_article->setContent(std::move(prepared));
	if (restore) {
		_body->setArticle(_article);
	} else {
		_body->articleContentChanged();
	}
	_fallback->hide();
	_body->show();
	resize(width(), resizeGetHeight(width()));
}

void FeedAssistantWidget::showFallback(const QString &text) {
	_body->hide();
	_fallback->show();
	_fallback->setText(text);
	resize(width(), resizeGetHeight(width()));
}

int FeedAssistantWidget::resizeGetHeight(int newWidth) {
	if (!_body->isHidden()) {
		_body->resizeToWidth(newWidth);
		_body->setGeometry(0, 0, newWidth, _body->height());
		return _body->height();
	} else if (!_fallback->isHidden()) {
		_fallback->resizeToWidth(newWidth);
		_fallback->moveToLeft(0, 0, newWidth);
		return _fallback->height();
	}
	return 0;
}

class FeedStepsWidget::Step final : public VerticalLayout {
public:
	Step(
		QWidget *parent,
		const FeedStep &data,
		const FeedContext &context,
		Main::Session *session,
		Fn<void()> toggled);

	void setData(const FeedStep &data);
	void appendThinking(const QString &text);
	void applyResult(const ToolResultBlock &result);
	void setActive(bool active);

	[[nodiscard]] bool isTool() const;
	[[nodiscard]] bool failed() const;
	[[nodiscard]] QString title() const;

private:
	void refreshRow();
	void refreshDetails();
	void fillDetails();
	void toggleExpanded();

	FeedStep _data;
	const FeedContext *_context = nullptr;
	Main::Session *_session = nullptr;
	Fn<void()> _toggled;
	ToggleRow *_row = nullptr;
	SlideWrap<VerticalLayout> *_details = nullptr;
	bool _expanded = false;
	bool _filled = false;
	bool _active = false;

};

FeedStepsWidget::Step::Step(
	QWidget *parent,
	const FeedStep &data,
	const FeedContext &context,
	Main::Session *session,
	Fn<void()> toggled)
: VerticalLayout(parent)
, _data(data)
, _context(&context)
, _session(session)
, _toggled(std::move(toggled)) {
	_row = add(object_ptr<ToggleRow>(this, st::agentStepsRowHeight));
	_details = add(object_ptr<SlideWrap<VerticalLayout>>(
		this,
		object_ptr<VerticalLayout>(this),
		st::agentStepsDetailsMargin));
	_details->hide(anim::type::instant);
	_row->setClickedCallback([=] {
		toggleExpanded();
	});
	refreshRow();
}

void FeedStepsWidget::Step::setData(const FeedStep &data) {
	_data = data;
	refreshRow();
	refreshDetails();
}

void FeedStepsWidget::Step::appendThinking(const QString &text) {
	if (const auto thinking = std::get_if<FeedThinking>(&_data)) {
		thinking->text += text;
		refreshDetails();
	}
}

void FeedStepsWidget::Step::applyResult(const ToolResultBlock &result) {
	if (const auto tool = std::get_if<FeedTool>(&_data)) {
		tool->result = result;
		refreshRow();
		refreshDetails();
	}
}

void FeedStepsWidget::Step::setActive(bool active) {
	if (_active != active) {
		_active = active;
		refreshRow();
	}
}

bool FeedStepsWidget::Step::isTool() const {
	return std::holds_alternative<FeedTool>(_data);
}

bool FeedStepsWidget::Step::failed() const {
	const auto tool = std::get_if<FeedTool>(&_data);
	return tool && tool->result && tool->result->error;
}

QString FeedStepsWidget::Step::title() const {
	return v::match(_data, [](const FeedThinking &) {
		return tr::lng_agent_thinking(tr::now);
	}, [&](const FeedTool &data) {
		return ToolTitleText(data.call, data.result, _session);
	});
}

void FeedStepsWidget::Step::refreshRow() {
	const auto tool = std::get_if<FeedTool>(&_data);
	_row->setTexts(
		title(),
		failed() ? tr::lng_agent_tool_failed(tr::now) : QString());
	_row->setActive(_active && tool && !tool->result);
	_row->setExpanded(_expanded);
}

void FeedStepsWidget::Step::refreshDetails() {
	if (!_filled) {
		return;
	}
	_details->entity()->clear();
	_filled = false;
	if (_expanded) {
		fillDetails();
	}
}

void FeedStepsWidget::Step::fillDetails() {
	if (_filled) {
		return;
	}
	_filled = true;
	const auto content = _details->entity();
	const auto addSection = [&](const QString &text) {
		content->add(object_ptr<FlatLabel>(
			content,
			text,
			st::agentStepsSectionLabel));
	};
	const auto addCode = [&](const QString &text) {
		content->add(
			object_ptr<CodeBox>(content, text),
			style::margins(0, 0, 0, st::agentStepsDetailsGap));
	};
	v::match(_data, [&](const FeedThinking &data) {
		const auto label = content->add(object_ptr<FlatLabel>(
			content,
			data.text,
			st::agentStepsThinkingLabel));
		label->setSelectable(true);
	}, [&](const FeedTool &data) {
		const auto object = ToolResultObject(data.result);
		if (IsMediaTool(data.call.name)) {
			auto preview = (data.call.name == u"view_media"_q)
				? MakeFeedMediaPreview(
					content,
					*_context,
					ResultMediaBlock(object))
				: nullptr;
			if (preview) {
				content->add(
					std::move(preview),
					style::margins(0, 0, 0, st::agentStepsDetailsGap));
			}
			const auto transcript = ResultTranscriptText(
				data.call.name,
				object);
			if (!transcript.isEmpty()) {
				addSection(tr::lng_agent_tool_transcript(tr::now));
				const auto label = content->add(object_ptr<FlatLabel>(
					content,
					CutForDisplay(transcript),
					st::agentStepsThinkingLabel));
				label->setSelectable(true);
			}
		}
		const auto arguments = ArgumentsText(data.call);
		if (!arguments.isEmpty()) {
			addSection(tr::lng_agent_tool_arguments(tr::now));
			addCode(arguments);
		}
		if (data.result) {
			addSection(tr::lng_agent_tool_result(tr::now));
			addCode(ResultText(*data.result));
		} else {
			addSection(tr::lng_agent_tool_running(tr::now));
		}
	});
}

void FeedStepsWidget::Step::toggleExpanded() {
	_expanded = !_expanded;
	if (_expanded) {
		fillDetails();
	}
	_toggled();
	_details->toggle(_expanded, anim::type::normal);
	refreshRow();
}

FeedStepsWidget::FeedStepsWidget(
	QWidget *parent,
	FeedContext context,
	const FeedSteps &steps)
: FeedItemWidget(parent)
, _context(std::move(context))
, _content(CreateChild<VerticalLayout>(this)) {
	_header = _content->add(
		object_ptr<ToggleRow>(_content, st::agentStepsHeaderHeight));
	_list = _content->add(object_ptr<SlideWrap<VerticalLayout>>(
		_content,
		object_ptr<VerticalLayout>(_content),
		st::agentStepsListMargin));
	_list->hide(anim::type::instant);
	_header->setClickedCallback([=] {
		toggleExpanded();
	});
	_content->heightValue(
	) | rpl::on_next([=](int height) {
		resize(width(), height);
		update();
	}, lifetime());
	applySteps(steps.steps);
}

FeedStepsWidget::~FeedStepsWidget() = default;

void FeedStepsWidget::updatePayload(const FeedItem &item) {
	const auto steps = std::get_if<FeedSteps>(&item);
	Expects(steps != nullptr);

	applySteps(steps->steps);
}

void FeedStepsWidget::applySteps(const std::vector<FeedStep> &steps) {
	auto keys = std::vector<QString>();
	keys.reserve(steps.size());
	auto thinkingIndex = -1;
	auto thinkingOrdinal = 0;
	for (const auto &step : steps) {
		keys.push_back(v::match(step, [&](const FeedThinking &data) {
			if (data.index != thinkingIndex) {
				thinkingIndex = data.index;
				thinkingOrdinal = 0;
			}
			return u"t:%1:%2"_q.arg(data.index).arg(thinkingOrdinal++);
		}, [](const FeedTool &data) {
			return u"c:"_q + data.call.id;
		}));
	}
	auto same = 0;
	const auto keep = std::min(_entries.size(), keys.size());
	for (; same != int(keep); ++same) {
		if (_entries[same].key != keys[same]) {
			break;
		}
		_entries[same].step->setData(steps[same]);
	}
	for (auto i = same; i != int(_entries.size()); ++i) {
		delete _entries[i].step;
	}
	_entries.resize(same);
	_liveThinking = 0;
	for (auto i = same; i != int(steps.size()); ++i) {
		addStep(keys[i], steps[i]);
	}
	refreshHeader();
}

void FeedStepsWidget::addStep(QString key, const FeedStep &step) {
	const auto list = _list->entity();
	const auto raw = list->add(
		object_ptr<Step>(
			list,
			step,
			_context,
			&_context.controller->session(),
			[=] { notifyManualToggle(); }),
		st::agentStepsRowMargin);
	_entries.push_back({ std::move(key), raw });
}

void FeedStepsWidget::appendThinking(const QString &text) {
	if (!_entries.empty() && _entries.back().key.startsWith(u"lt:"_q)) {
		_entries.back().step->appendThinking(text);
		return;
	}
	addStep(
		u"lt:%1"_q.arg(_liveThinking++),
		FeedThinking{ .index = -1, .text = text });
	refreshHeader();
}

void FeedStepsWidget::appendTool(const QString &id, const QString &name) {
	addStep(u"c:"_q + id, FeedTool{
		.index = -1,
		.call = ToolUseBlock{ .id = id, .name = name },
		.result = std::nullopt,
	});
	refreshHeader();
}

bool FeedStepsWidget::applyResult(const ToolResultBlock &result) {
	const auto key = u"c:"_q + result.toolUseId;
	for (const auto &entry : _entries) {
		if (entry.key == key) {
			entry.step->applyResult(result);
			refreshHeader();
			return true;
		}
	}
	return false;
}

void FeedStepsWidget::setActive(bool active) {
	if (_active != active) {
		_active = active;
		refreshHeader();
	}
}

void FeedStepsWidget::refreshHeader() {
	auto title = QString();
	auto failed = 0;
	for (const auto &entry : _entries) {
		if (entry.step->isTool()) {
			title = entry.step->title();
		}
		if (entry.step->failed()) {
			++failed;
		}
		entry.step->setActive(_active);
	}
	if (_active && !_entries.empty()) {
		title = _entries.back().step->title();
	} else if (title.isEmpty()) {
		title = tr::lng_agent_thinking(tr::now);
	}
	const auto count = int(_entries.size());
	if (count > 1) {
		title += u" · "_q
			+ tr::lng_agent_steps(tr::now, lt_count, count);
	}
	_header->setTexts(
		title,
		(failed
			? tr::lng_agent_steps_failed(tr::now, lt_count, failed)
			: QString()));
	_header->setActive(_active);
	_header->setExpanded(_expanded);
}

void FeedStepsWidget::toggleExpanded() {
	notifyManualToggle();
	_expanded = !_expanded;
	_list->toggle(_expanded, anim::type::normal);
	_header->setExpanded(_expanded);
}

int FeedStepsWidget::resizeGetHeight(int newWidth) {
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0, newWidth);
	return _content->height();
}

void FeedStepsWidget::paintEvent(QPaintEvent *e) {
	const auto skip = st::agentStepsLineSkip;
	const auto height = _list->height() - 2 * skip;
	if (height <= 0) {
		return;
	}
	auto p = QPainter(this);
	p.fillRect(
		(st::agentStepsSpinnerSize.width() - st::lineWidth) / 2,
		_list->y() + skip,
		st::lineWidth,
		height,
		st::agentStepsLineFg);
}

FeedSummaryWidget::FeedSummaryWidget(QWidget *parent)
: FeedItemWidget(parent)
, _label(
	this,
	tr::lng_agent_compacted(tr::now),
	st::agentFeedSummaryLabel) {
}

int FeedSummaryWidget::resizeGetHeight(int newWidth) {
	_label->resizeToWidth(newWidth);
	_label->moveToLeft(0, 0, newWidth);
	return _label->height();
}

FeedStatusWidget::FeedStatusWidget(QWidget *parent)
: RpWidget(parent)
, _label(this, QString(), st::agentFeedStatusLabel)
, _retry(this, tr::lng_agent_retry(), st::agentFeedRetry)
, _animation([=] { update(); }, st::agentFeedWorking) {
	_retry->setClickedCallback([=] {
		_retryClicks.fire({});
	});
	hide();
}

void FeedStatusWidget::showStatus(Kind kind, QString text) {
	_kind = kind;
	_label->setText(std::move(text));
	_retry->setVisible(kind == Kind::Failed);
	if (kind == Kind::Working) {
		_animation.start();
	} else {
		_animation.stop(anim::type::instant);
	}
	show();
	resizeToWidth(width());
	update();
}

rpl::producer<> FeedStatusWidget::retryClicks() const {
	return _retryClicks.events();
}

int FeedStatusWidget::resizeGetHeight(int newWidth) {
	const auto &padding = st::agentFeedStatusPadding;
	const auto available = std::max(
		newWidth - padding.left() - padding.right(),
		1);
	if (_kind == Kind::Working) {
		const auto spinner = st::agentFeedWorkingSize;
		const auto skip = spinner.width() + st::agentFeedWorkingGap;
		_label->resizeToNaturalWidth(std::max(available - skip, 1));
		const auto total = skip + _label->width();
		const auto left = padding.left() + (available - total) / 2;
		const auto height = std::max(_label->height(), spinner.height());
		_label->moveToLeft(
			left + skip,
			padding.top() + (height - _label->height()) / 2,
			newWidth);
		return padding.top() + height + padding.bottom();
	}
	_label->resizeToWidth(available);
	_label->moveToLeft(padding.left(), padding.top(), newWidth);
	auto height = padding.top() + _label->height();
	if (_kind == Kind::Failed) {
		height += st::agentFeedRetryMargin.top();
		_retry->moveToLeft((newWidth - _retry->width()) / 2, height, newWidth);
		height += _retry->height();
	}
	return height + padding.bottom();
}

void FeedStatusWidget::paintEvent(QPaintEvent *e) {
	if (_kind != Kind::Working) {
		return;
	}
	auto p = QPainter(this);
	const auto size = st::agentFeedWorkingSize;
	const auto position = QPoint(
		_label->x() - st::agentFeedWorkingGap - size.width(),
		_label->y() + (_label->height() - size.height()) / 2);
	_animation.draw(p, position, size, width());
}

} // namespace Agent::Ui
