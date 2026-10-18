/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_feed_items.h"

#include "base/assertion.h"
#include "core/click_handler_types.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_math_renderer.h"
#include "iv/markdown/iv_markdown_parse.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/markdown/iv_markdown_view_widget.h"
#include "lang/lang_keys.h"
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
constexpr auto kMaxResultChars = 2000;

[[nodiscard]] QString ToolTitleText(const QString &name) {
	if (name == u"get_context"_q) {
		return tr::lng_agent_tool_get_context(tr::now);
	} else if (name == u"read_history"_q) {
		return tr::lng_agent_tool_read_history(tr::now);
	} else if (name == u"search_messages"_q) {
		return tr::lng_agent_tool_search_messages(tr::now);
	} else if (name == u"get_peer_info"_q) {
		return tr::lng_agent_tool_get_peer_info(tr::now);
	} else if (name == u"list_members"_q) {
		return tr::lng_agent_tool_list_members(tr::now);
	}
	return tr::lng_agent_tool_other(tr::now, lt_name, name);
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

[[nodiscard]] TextWithEntities Monospace(QString text) {
	auto entities = EntitiesInText();
	if (!text.isEmpty()) {
		entities.push_back(EntityInText(EntityType::Code, 0, text.size()));
	}
	return { std::move(text), std::move(entities) };
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

} // namespace

BubbleWidget::BubbleWidget(
	QWidget *parent,
	const style::FlatLabel &st,
	style::color bg,
	int radius,
	const style::margins &padding,
	int maxWidth)
: FeedItemWidget(parent)
, _st(st)
, _bg(bg)
, _radius(radius)
, _padding(padding)
, _maxWidth(maxWidth)
, _label(this, QString(), st) {
}

BubbleWidget *BubbleWidget::setText(const QString &text) {
	_label->setText(text);
	refreshBubble();
	return this;
}

void BubbleWidget::refreshBubble() {
	const auto available = _maxWidth
		- _padding.left()
		- _padding.right();
	const auto natural = _label->naturalWidth();
	const auto inner = std::min(
		(natural > 0 ? natural : available),
		available);
	_label->resizeToWidth(inner);
	_label->move(_padding.left(), _padding.top());
	const auto width = std::min(
		_maxWidth,
		inner + _padding.left() + _padding.right());
	const auto height = _padding.top() + _label->height() + _padding.bottom();
	resize(width, height);
	setNaturalWidth(width);
}

int BubbleWidget::resizeGetHeight(int newWidth) {
	const auto inner = std::max(
		newWidth - _padding.left() - _padding.right(),
		0);
	_label->resizeToWidth(inner);
	_label->move(_padding.left(), _padding.top());
	return _padding.top() + _label->height() + _padding.bottom();
}

void BubbleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);
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
, _fallback(this, QString(), st::defaultFlatLabel)
, _renderTimer([=] { renderNow(); }) {
	_body->hide();
	_fallback->hide();
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
	if (_fallback->isHidden()) {
		_fallback->show();
	}
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
		_fallback->setGeometry(0, 0, newWidth, _fallback->height());
		return _fallback->height();
	}
	return 0;
}

FeedThinkingWidget::FeedThinkingWidget(QWidget *parent, QString text)
: FeedItemWidget(parent)
, _title(this, tr::lng_agent_thinking(tr::now), st::agentFeedDimLabel)
, _text(this, QString(), st::agentFeedDimLabel) {
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_text->setAttribute(Qt::WA_TransparentForMouseEvents);
	setCursor(style::cur_pointer);
	setText(std::move(text));
}

void FeedThinkingWidget::setText(QString text) {
	_text->setText(std::move(text));
	_expanded = false;
	refresh();
}

void FeedThinkingWidget::refresh(bool toggle) {
	if (toggle) {
		_expanded = !_expanded;
	}
	resize(width(), resizeGetHeight(width()));
}

int FeedThinkingWidget::resizeGetHeight(int newWidth) {
	_title->move(
		st::agentFeedToggleIconSize.width() + st::agentFeedToggleIconGap,
		0);
	const auto textTop = _title->height()
		+ st::agentFeedTogglePadding.bottom();
	_text->setVisible(_expanded);
	auto height = _title->height();
	if (_expanded) {
		_text->resizeToWidth(newWidth);
		_text->move(0, textTop);
		height = textTop + _text->height();
	}
	return height;
}

void FeedThinkingWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto size = st::agentFeedToggleIconSize;
	const auto y = (_title->height() - size.height()) / 2;
	const auto midY = y + size.height() / 2;
	const auto midX = size.width() / 2;
	auto polygon = QPolygonF();
	if (_expanded) {
		polygon = QPolygonF()
			<< QPointF(0, y)
			<< QPointF(size.width(), y)
			<< QPointF(midX, y + size.height());
	} else {
		polygon = QPolygonF()
			<< QPointF(0, y)
			<< QPointF(size.width(), midY)
			<< QPointF(0, y + size.height());
	}
	p.setPen(Qt::NoPen);
	p.setBrush(st::agentFeedToggleIconFg);
	p.drawPolygon(polygon);
}

void FeedThinkingWidget::mousePressEvent(QMouseEvent *e) {
	refresh(true);
}

FeedToolWidget::FeedToolWidget(QWidget *parent, const FeedTool &tool)
: FeedItemWidget(parent)
, _callId(tool.call.id)
, _title(this, ToolTitleText(tool.call.name), st::agentFeedDimLabel)
, _state(this, tr::lng_agent_tool_running(tr::now), st::agentFeedDimLabel)
, _argumentsHeader(
	this,
	tr::lng_agent_tool_arguments(tr::now),
	st::agentFeedDimLabel)
, _arguments(this, QString(), st::agentFeedDimLabel)
, _resultHeader(
	this,
	tr::lng_agent_tool_result(tr::now),
	st::agentFeedDimLabel)
, _result(this, QString(), st::agentFeedDimLabel) {
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_state->setAttribute(Qt::WA_TransparentForMouseEvents);
	_argumentsHeader->setAttribute(Qt::WA_TransparentForMouseEvents);
	_resultHeader->setAttribute(Qt::WA_TransparentForMouseEvents);
	setCursor(style::cur_pointer);
	const auto arguments = tool.call.inputError.isEmpty()
		? QString::fromUtf8(
			QJsonDocument(tool.call.input).toJson(QJsonDocument::Indented))
		: QString::fromUtf8(tool.call.rawInput);
	_arguments->setMarkedText(Monospace(arguments));
	applyResult(tool.result);
}

void FeedToolWidget::updatePayload(const FeedItem &item) {
	const auto tool = std::get_if<FeedTool>(&item);
	Expects(tool != nullptr);
	Expects(tool->call.id == _callId);
	applyResult(tool->result);
}

void FeedToolWidget::showFinished(ToolResultBlock result) {
	applyResult(std::move(result));
}

void FeedToolWidget::applyResult(std::optional<ToolResultBlock> result) {
	_running = !result.has_value();
	_failed = result.has_value() && result->error;
	if (_running) {
		_state->setText(tr::lng_agent_tool_running(tr::now));
		_state->setTextColorOverride(std::nullopt);
	} else if (_failed) {
		_state->setText(tr::lng_agent_tool_failed(tr::now));
		_state->setTextColorOverride(st::agentFeedToolFailedFg->c);
	} else {
		_state->setText(QString());
	}
	_result->setMarkedText(Monospace(result.has_value()
		? CutForDisplay(result->content)
		: QString()));
	refresh();
}

void FeedToolWidget::refresh() {
	resize(width(), resizeGetHeight(width()));
}

int FeedToolWidget::resizeGetHeight(int newWidth) {
	_title->move(0, 0);
	_state->move(newWidth - _state->width(), 0);
	auto height = _title->height();
	auto top = height + st::agentFeedToolExpandPadding.top();
	_argumentsHeader->setVisible(_expanded);
	_arguments->setVisible(_expanded);
	_resultHeader->setVisible(_expanded);
	_result->setVisible(_expanded);
	if (_expanded) {
		_argumentsHeader->move(st::agentFeedToggleIconSize.width(), top);
		top += _argumentsHeader->height();
		_arguments->resizeToWidth(newWidth);
		_arguments->move(st::agentFeedToggleIconSize.width(), top);
		top += _arguments->height();
		if (!_running) {
			_resultHeader->move(st::agentFeedToggleIconSize.width(), top);
			top += _resultHeader->height();
			_result->resizeToWidth(newWidth);
			_result->move(st::agentFeedToggleIconSize.width(), top);
			top += _result->height();
		}
		height = top;
	}
	return height;
}

void FeedToolWidget::mousePressEvent(QMouseEvent *e) {
	_expanded = !_expanded;
	refresh();
}

FeedSummaryWidget::FeedSummaryWidget(QWidget *parent)
: FeedItemWidget(parent)
, _label(this, tr::lng_agent_compacted(tr::now), st::agentFeedStatusLabel) {
}

int FeedSummaryWidget::resizeGetHeight(int newWidth) {
	const auto natural = _label->naturalWidth();
	_label->resizeToWidth(std::min(
		(natural > 0 ? natural : newWidth),
		newWidth));
	_label->move((newWidth - _label->width()) / 2, 0);
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
		_animation.stop();
	}
	relayout();
	show();
}

rpl::producer<> FeedStatusWidget::retryClicks() const {
	return _retryClicks.events();
}

void FeedStatusWidget::resizeEvent(QResizeEvent *e) {
	relayout();
}

void FeedStatusWidget::relayout() {
	const auto w = width();
	const auto labelHeight = _label->height();
	const auto workingHeight = st::agentFeedWorkingSize.height();
	if (_kind == Kind::Working) {
		const auto total = st::agentFeedWorkingSize.width()
			+ st::agentFeedWorkingGap
			+ _label->width();
		const auto left = std::max((w - total) / 2, 0);
		_label->move(
			left + st::agentFeedWorkingSize.width()
				+ st::agentFeedWorkingGap,
			(workingHeight - labelHeight) / 2);
		resize(w, std::max(labelHeight, workingHeight));
		return;
	}
	_label->move((w - _label->width()) / 2, 0);
	auto height = labelHeight;
	if (_kind == Kind::Failed) {
		_retry->move(
			(w - _retry->width()) / 2,
			labelHeight + st::agentFeedRetryMargin.top());
		height += st::agentFeedRetryMargin.top() + _retry->height();
	}
	resize(w, height);
}

void FeedStatusWidget::paintEvent(QPaintEvent *e) {
	if (_kind != Kind::Working) {
		return;
	}
	Painter p(this);
	const auto position = QPoint(
		std::max(
			(_label->x() - st::agentFeedWorkingGap
				- st::agentFeedWorkingSize.width()) / 2,
			0),
		(height() - st::agentFeedWorkingSize.height()) / 2);
	_animation.draw(p, position, st::agentFeedWorkingSize, width());
}

} // namespace Agent::Ui