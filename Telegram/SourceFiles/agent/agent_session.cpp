/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_session.h"

#include "agent/agent_claude.h"
#include "agent/agent_manager.h"
#include "agent/agent_media_convert.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_memories.h"
#include "agent/agent_spending_tracker.h"
#include "agent/agent_task_runner.h"
#include "agent/agent_tasks.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_loop.h"
#include "agent/core/agent_media.h"
#include "agent/core/agent_memory.h"
#include "agent/core/agent_policy.h"
#include "agent/core/agent_prompts.h"
#include "agent/core/agent_request.h"
#include "agent/core/agent_spending.h"
#include "agent/core/agent_tool_catalog.h"
#include "agent/tools/agent_tools.h"
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "crl/crl_on_main.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <rpl/producer.h>

#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>

namespace Agent {
namespace {

[[nodiscard]] QString ZoneText() {
	const auto offset = QDateTime::currentDateTime().offsetFromUtc();
	const auto minutes = std::abs(offset) / 60;
	const auto utc = u"UTC%1%2:%3"_q
		.arg((offset < 0) ? '-' : '+')
		.arg(minutes / 60, 2, 10, QChar('0'))
		.arg(minutes % 60, 2, 10, QChar('0'));
	const auto id = QString::fromUtf8(QTimeZone::systemTimeZoneId());
	return id.isEmpty() ? utc : (id + u", "_q + utc);
}

constexpr auto kProvisionalTitleLimit = 60;

[[nodiscard]] QString ProvisionalTitle(const QString &text) {
	for (const auto &line : text.split('\n')) {
		const auto trimmed = line.trimmed();
		if (trimmed.size() > kProvisionalTitleLimit) {
			return trimmed.left(kProvisionalTitleLimit - 1).trimmed()
				+ QChar(0x2026);
		} else if (!trimmed.isEmpty()) {
			return trimmed;
		}
	}
	return {};
}

void AppendPart(QString &to, const QString &part, const QString &separator) {
	if (part.isEmpty()) {
		return;
	}
	to = to.isEmpty() ? part : (to + separator + part);
}

[[nodiscard]] std::vector<SendRequest> MergeQueue(
		std::vector<SendRequest> queue) {
	auto result = std::vector<SendRequest>();
	for (auto &request : queue) {
		const auto fits = !result.empty()
			&& (result.back().attachments.size()
				+ request.attachments.size()
				<= kAttachmentsPerMessage);
		if (!fits) {
			result.push_back(std::move(request));
			continue;
		}
		auto &merged = result.back();
		AppendPart(merged.text, request.text, u"\n\n"_q);
		AppendPart(merged.hint, request.hint, u"\n"_q);
		AppendPart(merged.untrusted, request.untrusted, u"\n\n"_q);
		for (const auto &peer : request.mentions) {
			if (!ranges::contains(merged.mentions, peer)) {
				merged.mentions.push_back(peer);
			}
		}
		merged.attachments.insert(
			end(merged.attachments),
			std::make_move_iterator(begin(request.attachments)),
			std::make_move_iterator(end(request.attachments)));
		if (merged.replyTo.isEmpty()) {
			merged.replyTo = request.replyTo;
			merged.replyQuote = request.replyQuote;
			merged.replyText = request.replyText;
		}
	}
	return result;
}

[[nodiscard]] QString ReplyContextText(const SendRequest &request) {
	auto quoted = QStringList();
	for (const auto &line : request.replyText.split('\n')) {
		quoted.push_back(u"> "_q + line);
	}
	return (request.replyQuote.isEmpty()
		? u"The user replies to this earlier message of the session:\n"_q
		: u"The user replies to this part of an earlier message:\n"_q)
		+ quoted.join('\n');
}

[[nodiscard]] int CountPendingCards(const std::vector<Card> &cards) {
	auto result = 0;
	for (const auto &card : cards) {
		if (card.status == CardStatus::Pending) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] std::unique_ptr<PolicyGate> MakeGate(
		not_null<Manager*> manager,
		const SessionRecord &record,
		const std::optional<RunSetup> &run) {
	auto excluded = [=](PeerKey peer) {
		return manager->exclusions().excluded(peer);
	};
	if (!run) {
		return std::make_unique<PolicyGate>(
			record.info.scope,
			std::move(excluded),
			record.tainted);
	}
	const auto session = &manager->session();
	const auto chats = run->task.frame.chats;
	const auto triggers = run->triggerPeers;
	return std::make_unique<PolicyGate>(
		record.info.scope,
		std::move(excluded),
		record.tainted,
		GateFrame{
			.tools = run->task.frame.tools,
			.contains = [=](PeerKey peer) {
				return (chats.triggerChat
						&& ranges::contains(triggers, peer))
					|| ChatSetContains(
						chats,
						MakeChatFacts(session, peer));
			},
		});
}

} // namespace

Session::Session(
	not_null<Manager*> manager,
	SessionRecord record,
	std::optional<RunSetup> run)
: _manager(manager)
, _run(std::move(run))
, _record(std::move(record))
, _gate(MakeGate(manager, _record, _run))
, _tools(std::make_unique<ToolRegistry>(
	&manager->session(),
	_gate.get(),
	_record.loadedGroups,
	_record.info.id)) {
	_tools->setProvider([=] {
		return (_loop && _loop->busy() && _loopProvider)
			? _loopProvider
			: currentProvider();
	});
	if (_run) {
		_tools->setBackground(_run->notify);
	}
	_repairedOnLoad = CloseInterruptedTurn(_record);
	_undoneReported = int(_record.undone.size());
	_state.pendingCards = CountPendingCards(_record.cards);
	_tools->touched(
	) | rpl::on_next([=](PeerKey peer) {
		if (!ranges::contains(_record.info.touched, peer)) {
			_record.info.touched.push_back(peer);
		}
	}, _lifetime);
	_tools->helperUsage(
	) | rpl::on_next([=](HelperUsage helper) {
		accountUsage(helper.usage, helper.provider);
	}, _lifetime);
	_tools->cardUpdates(
	) | rpl::on_next([=](Card card) {
		applyCard(std::move(card));
	}, _lifetime);
	_tools->loadedGroupsChanges(
	) | rpl::on_next([=] {
		_record.loadedGroups = _tools->loadedGroups();
		(void)publishRecord();
	}, _lifetime);
}

void Session::finishOpen() {
	if (_repairedOnLoad) {
		_repairedOnLoad = false;
		(void)publishRecord();
	}
}

void Session::applyCard(Card card) {
	const auto i = ranges::find(
		_record.cards,
		card.toolUseId,
		&Card::toolUseId);
	if (i == _record.cards.end()) {
		_record.cards.push_back(std::move(card));
	} else {
		*i = std::move(card);
	}
	_state.pendingCards = CountPendingCards(_record.cards);
	if (!publishRecord()) {
		return;
	}
	_stateChanges.fire({});
}

Session::~Session() = default;

Manager &Session::manager() const {
	return *_manager;
}

const QString &Session::id() const {
	return _record.info.id;
}

const Scope &Session::scope() const {
	return _record.info.scope;
}

bool Session::isRun() const {
	return !_record.info.task.isEmpty();
}

std::optional<ProviderSettings> Session::currentProvider() const {
	if (_run) {
		return _run->provider;
	} else if (isRun()) {
		return std::nullopt;
	} else if (const auto provider = SessionProvider(
			_manager->providers(),
			_record.info.profileId)) {
		return *provider;
	}
	return std::nullopt;
}

QString Session::profileId() const {
	if (const auto provider = currentProvider()) {
		return provider->id;
	}
	return QString();
}

void Session::setProfile(const QString &profileId) {
	if (isRun() || _record.info.profileId == profileId) {
		return;
	}
	_record.info.profileId = profileId;
	if (!_record.messages.empty()) {
		const auto weak = base::make_weak(this);
		_manager->save(this);
		if (!weak) {
			return;
		}
	}
	_stateChanges.fire({});
}

QString Session::cacheKey() const {
	return _run ? _run->task.id : _record.info.id;
}

const SessionRecord &Session::record() const {
	return _record;
}

rpl::producer<> Session::recordChanges() const {
	return _recordChanges.events();
}

rpl::producer<EngineEvent> Session::live() const {
	return _live.events();
}

const SessionState &Session::state() const {
	return _state;
}

rpl::producer<> Session::stateChanges() const {
	return _stateChanges.events();
}

void Session::send(SendRequest request) {
	if (_state.status == SessionStatus::Working
		|| _state.status == SessionStatus::Compacting) {
		_queue.push_back(std::move(request));
		_state.queued.push_back(_queue.back().text);
		const auto weak = base::make_weak(this);
		_stateChanges.fire({});
		return;
	}
	dispatch(std::move(request));
}

void Session::stop() {
	_queue.clear();
	_state.queued.clear();
	halt();
}

void Session::interrupt() {
	_queue = MergeQueue(base::take(_queue));
	_state.queued.clear();
	for (const auto &request : _queue) {
		_state.queued.push_back(request.text);
	}
	halt();
}

void Session::halt() {
	const auto weak = base::make_weak(this);
	_compaction = nullptr;
	if (base::take(_titleRequest)) {
		_titleRequested = false;
	}
	const auto busy = _loop && _loop->busy();
	if (busy) {
		_loop->interrupt();
		if (!weak) {
			return;
		}
	}
	if (_state.status != SessionStatus::Idle) {
		_state.status = SessionStatus::Idle;
		_stateChanges.fire({});
		if (!weak) {
			return;
		}
	}
	if (!busy && !_queue.empty()) {
		crl::on_main(this, [=] {
			processQueue();
		});
	}
}

void Session::retry() {
	if (_state.status != SessionStatus::Failed) {
		return;
	}
	if (_record.messages.empty()
		|| _record.messages.back().role != Role::User) {
		return;
	}
	_overflowRetried = false;
	const auto provider = currentProvider();
	if (!provider) {
		failWithoutProvider();
		return;
	}
	const auto until = planCompaction(*provider);
	if (until != _record.summaryUntil) {
		startCompaction(until);
	} else {
		runTurn();
	}
}

void Session::decideCard(
		const QString &toolUseId,
		CardDecision decision) {
	_tools->decide(toolUseId, decision);
}

void Session::expireCards() {
	auto ids = std::vector<QString>();
	for (const auto &card : _record.cards) {
		if (card.status == CardStatus::Pending) {
			ids.push_back(card.toolUseId);
		}
	}
	const auto weak = base::make_weak(this);
	for (const auto &id : ids) {
		_tools->expire(id);
		if (!weak) {
			return;
		}
	}
}

bool Session::canUndo(const QString &toolUseId) const {
	return _tools->canUndo(toolUseId);
}

void Session::undo(const std::vector<QString> &toolUseIds) {
	for (const auto &toolUseId : toolUseIds) {
		if (!_tools->canUndo(toolUseId)) {
			continue;
		}
		const auto weak = base::make_weak(this);
		_tools->undo(toolUseId, [=](QString error) {
			if (!weak) {
				return;
			} else if (error.isEmpty()) {
				if (!ranges::contains(_record.undone, toolUseId)) {
					_record.undone.push_back(toolUseId);
				}
				if (!publishRecord()) {
					return;
				}
			} else {
				_undoErrors.fire_copy(std::move(error));
				if (!weak) {
					return;
				}
			}
			_undoChanges.fire({});
		});
	}
}

QString Session::undoHint() {
	if (_record.undone.size() <= _undoneReported) {
		return {};
	}
	auto parts = QStringList();
	for (auto i = _undoneReported; i < int(_record.undone.size()); ++i) {
		const auto &toolUseId = _record.undone[i];
		auto name = QString();
		for (const auto &message : _record.messages) {
			for (const auto &block : message.blocks) {
				if (const auto use = std::get_if<ToolUseBlock>(&block)) {
					if (use->id == toolUseId) {
						name = use->name;
						break;
					}
				}
			}
			if (!name.isEmpty()) {
				break;
			}
		}
		parts.push_back(name.isEmpty()
			? toolUseId
			: u"%1 (%2)"_q.arg(name, toolUseId));
	}
	_undoneReported = int(_record.undone.size());
	return u"The user undid: %1."_q.arg(parts.join(u", "_q));
}

rpl::producer<> Session::undoChanges() const {
	return _undoChanges.events();
}

rpl::producer<QString> Session::undoErrors() const {
	return _undoErrors.events();
}

void Session::dispatch(SendRequest request) {
	Expects(!request.text.isEmpty()
		|| !request.hint.isEmpty()
		|| !request.attachments.empty());

	auto message = Message{
		.role = Role::User,
		.blocks = {},
		.replyTo = request.replyTo,
		.replyQuote = request.replyQuote,
	};
	if (!request.text.isEmpty()) {
		message.blocks.push_back(TextBlock{ request.text });
	}
	if (!request.replyText.isEmpty()) {
		message.blocks.push_back(
			TextBlock{ ReplyContextText(request), true });
	}
	for (const auto &input : request.attachments) {
		message.blocks.push_back(_manager->attachments().add(
			id(),
			++_record.info.attachments,
			input));
	}
	if (!request.untrusted.isEmpty()) {
		message.blocks.push_back(
			TextBlock{ WrapUntrusted(request.untrusted), true });
	}
	auto hint = request.hint;
	const auto addHint = [&](const QString &line) {
		if (!line.isEmpty()) {
			hint = hint.isEmpty() ? line : hint + '\n' + line;
		}
	};
	addHint(undoHint());
	addHint(FormatTurnContext({
		.now = QDateTime::currentDateTime(),
		.zone = ZoneText(),
		.loadedGroups = _tools->loadedGroups(),
		.pendingCards = int(ranges::count(
			_record.cards,
			CardStatus::Pending,
			&Card::status)),
	}));
	if (_record.info.scope.kind == ScopeKind::Chat) {
		if (const auto peer = _manager->session().data().peerLoaded(
				PeerId(_record.info.scope.peer))) {
			addHint(FormatChatCounters(MakeChatBrief(peer)));
		}
	}
	if (!hint.isEmpty()) {
		message.blocks.push_back(TextBlock{ HintText(hint), true });
	}
	Expects(!message.blocks.empty());

	if (_record.messages.empty() && !isRun()) {
		_record.info.title = ProvisionalTitle(request.text);
	}
	if (_record.info.profileId.isEmpty() && !isRun()) {
		if (const auto provider = DefaultProvider(_manager->providers())) {
			_record.info.profileId = provider->id;
		}
	}
	auto named = request.mentions;
	for (const auto &peer : FindNamedPeers(
			request.text,
			NameCandidates(&_manager->session()))) {
		if (!ranges::contains(named, peer)) {
			named.push_back(peer);
		}
	}
	_gate->beginTurn(std::move(named));
	_tools->beginTurn();
	if (!request.untrusted.isEmpty()
		|| !request.attachments.empty()) {
		_gate->markTainted();
	}
	_overflowRetried = false;
	_state.error.reset();
	_state.lastEnd.reset();

	const auto weak = base::make_weak(this);
	message.date = base::unixtime::now();
	_record.messages.push_back(std::move(message));
	_recordChanges.fire({});
	if (!weak) {
		return;
	}
	if (!publishRecord()) {
		return;
	}
	const auto provider = currentProvider();
	if (!provider) {
		failWithoutProvider();
		return;
	}
	const auto until = planCompaction(*provider);
	if (until != _record.summaryUntil) {
		startCompaction(until);
	} else {
		runTurn();
	}
}

void Session::failWithoutProvider() {
	(void)applyState(
		SessionStatus::Failed,
		Error{
			ErrorKind::BadRequest,
			0,
			u"No AI provider is set up."_q,
		},
		TurnEnd::Failed);
}

void Session::failWithoutClaude() {
	(void)applyState(
		SessionStatus::Failed,
		Error{
			ErrorKind::Auth,
			0,
			ClaudeMissingText(),
		},
		TurnEnd::Failed);
}

void Session::accountUsage(
		const Usage &usage,
		const ProviderSettings &provider) {
	const auto priced = PriceUsage(usage, provider);
	_record.info.usage += priced;
	_manager->spending().add(priced);
	if (isRun()) {
		_manager->tasks().addUsage(_record.info.task, priced);
	}
}

QString Session::buildSystem() const {
	auto brief = std::optional<ChatBrief>();
	if (_record.info.scope.kind == ScopeKind::Chat) {
		if (const auto peer = _manager->session().data().peerLoaded(
				PeerId(_record.info.scope.peer))) {
			brief = MakeChatBrief(peer);
		}
	}
	const auto provider = currentProvider();
	const auto family = provider
		? PromptFamilyForModel(provider->model)
		: PromptFamily::Generic;
	if (_run) {
		return TaskSystemPrompt({
			.scope = _record.info.scope,
			.account = AccountLine(&_manager->session()),
			.instructions = _manager->providers().instructions,
			.title = _run->task.title,
			.task = _run->task.instruction,
			.frame = _run->frame,
			.chat = brief,
			.catalog = _tools->catalog(),
			.family = family,
		});
	}
	return SystemPrompt({
		.scope = _record.info.scope,
		.account = AccountLine(&_manager->session()),
		.instructions = _manager->providers().instructions,
		.memory = FormatMemory(
			_manager->memories().get(_record.info.scope.peer)),
		.chat = brief,
		.catalog = _tools->catalog(),
		.family = family,
	});
}

int Session::planCompaction(
		const ProviderSettings &provider,
		bool force) const {
	auto request = Request{
		.model = provider.model,
		.system = buildSystem(),
		.messages = {},
		.tools = _tools->tools(),
		.maxOutputTokens = 0,
	};
	const auto fixed = EstimateTokens(request);
	auto window = _manager->contextWindow(provider);
	if (force) {
		request.messages = ContextMessages(_record);
		window = std::min(window, EstimateTokens(request));
	}
	return PlanCompaction(_record, fixed, window);
}

void Session::startCompaction(int until) {
	const auto provider = currentProvider();
	if (!provider || !_manager->canRun(*provider)) {
		failWithoutProvider();
		return;
	}
	const auto providerCopy = *provider;
	auto request = _manager->single(
		providerCopy,
		CompactionRequest(_record, until, provider->model),
		[=](Response response) {
			base::take(_compaction);
			const auto weak = base::make_weak(this);
			if (response.error) {
				LOG(("Agent Warning: session compaction failed."));
			} else {
				accountUsage(response.usage, providerCopy);
				const auto text = ResponseText(response.message);
				if (!text.isEmpty()) {
					_record.summary = text;
					_record.summaryUntil = until;
					(void)publishRecord();
				}
			}
			if (!weak) {
				return;
			}
			runTurn();
		});
	if (!request) {
		failWithoutClaude();
		return;
	} else if (!applyState(SessionStatus::Compacting)) {
		return;
	}
	_compaction = std::move(request);
}

void Session::runTurn() {
	const auto provider = currentProvider();
	if (!provider) {
		failWithoutProvider();
		return;
	}
	runLoop(*provider);
}

std::unique_ptr<Engine> Session::createEngine(
		const ProviderSettings &provider,
		std::vector<Message> messages) {
	const auto maxSteps = _run ? kTaskMaxSteps : 25;
	if (UsesClaudeCode(provider)) {
		const auto program = FindClaudeProgram(
			_manager->providers().claudePath);
		if (program.isEmpty()) {
			return nullptr;
		}
		return std::make_unique<Claude::Engine>(
			&_manager->claudeLauncher(),
			_run ? nullptr : &_manager->claudePool(),
			_tools.get(),
			Claude::EngineOptions{
				.session = {
					.model = provider.model,
					.effort = provider.effort,
					.system = buildSystem(),
					.maxTurns = maxSteps,
				},
				.launch = ClaudeLaunchSettings(program),
				.owner = id(),
				.recordMessages = int(_record.messages.size()),
				.summaryUntil = _record.summaryUntil,
				.keepWarm = !_run,
				.limits = [](Claude::Limits limits) {
					UpdateClaudeLimits(std::move(limits));
				},
			},
			std::move(messages));
	}
	auto adapter = CreateAdapter(provider);
	if (!adapter) {
		return nullptr;
	}
	return std::make_unique<Loop>(
		std::move(adapter),
		&_manager->transportFor(provider),
		_tools.get(),
		LoopOptions{
			.model = provider.model,
			.system = buildSystem(),
			.maxOutputTokens = 0,
			.maxSteps = maxSteps,
			.pdfEngine = (IsOpenRouter(provider)
				&& !_manager->abilities(provider).files
				? u"pdf-text"_q
				: QString()),
			.cacheKey = cacheKey(),
		},
		std::move(messages));
}

void Session::runLoop(const ProviderSettings &provider) {
	if (!_manager->canRun(provider)) {
		failWithoutProvider();
		return;
	}
	auto messages = _manager->abilities(provider).images
		? ContextMessages(withAttachedImages())
		: ContextMessages(_record);
	_loopLifetime.destroy();
	_loop = nullptr;
	_thinkingStarted = 0;
	_stepStarted = crl::now();
	_thinkingDurations.clear();
	_loop = createEngine(provider, std::move(messages));
	if (!_loop) {
		failWithoutClaude();
		return;
	} else if (!applyState(SessionStatus::Working)) {
		return;
	}
	_loopContext = int(_loop->history().size());
	_loopBase = int(_record.messages.size());
	_loopProvider = provider;
	_loop->events(
	) | rpl::on_next([=](EngineEvent event) {
		handleEvent(std::move(event));
	}, _loopLifetime);
	_loop->resume();
}

SessionRecord Session::withAttachedImages() const {
	auto result = _record;
	auto &messages = result.messages;
	for (auto i = int(messages.size()); i > result.summaryUntil;) {
		auto &message = messages[--i];
		const auto start = (message.role == Role::User)
			&& ranges::any_of(message.blocks, [](const Block &block) {
				return std::holds_alternative<TextBlock>(block)
					|| std::holds_alternative<MediaBlock>(block);
			});
		if (!start) {
			continue;
		}
		for (auto &block : message.blocks) {
			const auto media = std::get_if<MediaBlock>(&block);
			if (!media
				|| media->attachment.isEmpty()
				|| media->kind != MediaKind::Image
				|| !media->bytes.isEmpty()) {
				continue;
			}
			if (const auto read = _manager->attachments().read(
					id(),
					media->attachment)) {
				media->bytes = PrepareImageForModel(read->bytes);
				media->mimeType = u"image/jpeg"_q;
			}
		}
		break;
	}
	return result;
}

void Session::trackThinking(const EngineEvent &event) {
	if (std::get_if<ThinkingDelta>(&event)) {
		if (!_thinkingStarted) {
			_thinkingStarted = base::take(_stepStarted);
			if (!_thinkingStarted) {
				_thinkingStarted = crl::now();
			}
		}
	} else if (std::get_if<ToolCallFinished>(&event)) {
		_stepStarted = crl::now();
	} else if (std::get_if<TextDelta>(&event)
		|| std::get_if<ToolUseStarted>(&event)
		|| std::get_if<ToolCallRequested>(&event)
		|| std::get_if<TurnFinished>(&event)) {
		_stepStarted = 0;
		if (_thinkingStarted) {
			_thinkingDurations.push_back(
				int(crl::now() - base::take(_thinkingStarted)));
		}
	}
}

void Session::markTurnEnd(TurnEnd end) {
	if (end != TurnEnd::Interrupted
		&& end != TurnEnd::StepLimit
		&& end != TurnEnd::MaxTokens
		&& end != TurnEnd::Refusal) {
		return;
	}
	const auto after = int(_record.messages.size());
	if (!after) {
		return;
	}
	auto &marks = _record.turnEnds;
	while (!marks.empty() && marks.back().after >= after) {
		marks.pop_back();
	}
	marks.push_back({ .after = after, .end = end });
}

void Session::handleEvent(EngineEvent event) {
	const auto weak = base::make_weak(this);
	trackThinking(event);
	if (std::get_if<ToolCallRequested>(&event)
		|| std::get_if<ToolCallFinished>(&event)) {
		copyLoopMessages();
		_recordChanges.fire({});
		if (!weak) {
			return;
		}
	} else if (const auto usage = std::get_if<UsageReported>(&event)) {
		Expects(_loopProvider.has_value());
		accountUsage(usage->usage, *_loopProvider);
	} else if (const auto finished = std::get_if<TurnFinished>(&event)) {
		const auto turn = *finished;
		if (!handleTurnFinished(turn)) {
			return;
		}
		_live.fire(std::move(event));
		if (!weak) {
			return;
		}
		crl::on_main(this, [=] {
			continueAfterTurn(turn);
		});
		return;
	}
	_live.fire(std::move(event));
}

bool Session::handleTurnFinished(const TurnFinished &turn) {
	copyLoopMessages();
	markTurnEnd(turn.end);
	const auto failed = (turn.end == TurnEnd::Failed);
	if (!applyState(
			failed ? SessionStatus::Failed : SessionStatus::Idle,
			turn.error,
			turn.end)) {
		return false;
	}
	DropMediaBytes(_record);
	return publishRecord();
}

void Session::continueAfterTurn(const TurnFinished &turn) {
	if (turn.end == TurnEnd::Failed) {
		if (_state.status == SessionStatus::Failed
			&& turn.error
			&& IsContextOverflow(*turn.error)
			&& !_overflowRetried) {
			_overflowRetried = true;
			compactAndResume();
		}
		return;
	}
	if (turn.end == TurnEnd::Completed
		&& !isRun()
		&& !_record.info.titleFinal
		&& !_titleRequested) {
		startTitleRequest();
	}
	processQueue();
}

void Session::compactAndResume() {
	const auto provider = currentProvider();
	if (!provider) {
		return;
	}
	const auto until = planCompaction(*provider, true);
	if (until != _record.summaryUntil) {
		startCompaction(until);
	}
}

void Session::processQueue() {
	if (_queue.empty() || _state.status != SessionStatus::Idle) {
		return;
	}
	auto request = std::move(_queue.front());
	_queue.erase(_queue.begin());
	Expects(!_state.queued.empty());
	_state.queued.erase(_state.queued.begin());
	const auto weak = base::make_weak(this);
	_stateChanges.fire({});
	if (!weak) {
		return;
	}
	dispatch(std::move(request));
}

void Session::copyLoopMessages() {
	if (!_loop) {
		return;
	}
	const auto &history = _loop->history();
	Expects(int(history.size()) >= _loopContext);
	auto dates = std::vector<qint64>();
	for (auto i = _loopBase; i < int(_record.messages.size()); ++i) {
		dates.push_back(_record.messages[i].date);
	}
	const auto now = qint64(base::unixtime::now());
	_record.messages.resize(_loopBase);
	_record.messages.reserve(_loopBase + history.size() - _loopContext);
	for (auto i = _loopContext; i < int(history.size()); ++i) {
		auto &message = _record.messages.emplace_back(history[i]);
		const auto known = (i - _loopContext);
		message.date = (known < int(dates.size()) && dates[known])
			? dates[known]
			: now;
	}
	auto thought = 0;
	for (auto i = _loopBase; i < int(_record.messages.size()); ++i) {
		for (auto &block : _record.messages[i].blocks) {
			const auto thinking = std::get_if<ThinkingBlock>(&block);
			if (!thinking
				|| thinking->redacted
				|| thinking->text.trimmed().isEmpty()) {
				continue;
			} else if (thought < int(_thinkingDurations.size())) {
				thinking->durationMs = _thinkingDurations[thought];
			}
			++thought;
		}
	}
}

bool Session::applyState(SessionStatus status) {
	_state.status = status;
	const auto weak = base::make_weak(this);
	_stateChanges.fire({});
	return static_cast<bool>(weak);
}

bool Session::applyState(
		SessionStatus status,
		std::optional<Error> error,
		std::optional<TurnEnd> lastEnd) {
	_state.status = status;
	_state.error = std::move(error);
	_state.lastEnd = lastEnd;
	const auto weak = base::make_weak(this);
	_stateChanges.fire({});
	return static_cast<bool>(weak);
}

bool Session::publishRecord() {
	_record.tainted = _gate->tainted();
	_record.info.updated = base::unixtime::now();
	const auto preview = MakeSessionPreview(_record);
	_record.info.preview = preview.text;
	_record.info.previewOutgoing = preview.outgoing;
	const auto weak = base::make_weak(this);
	_manager->save(this);
	if (!weak) {
		return false;
	}
	_recordChanges.fire({});
	return true;
}

void Session::startTitleRequest() {
	const auto provider = currentProvider();
	if (!provider || !_manager->canRun(*provider)) {
		return;
	}
	_titleRequested = true;
	const auto providerCopy = *provider;
	_titleRequest = _manager->single(
		providerCopy,
		TitleRequest(_record, provider->model),
		[=](Response response) {
			base::take(_titleRequest);
			const auto weak = base::make_weak(this);
			if (!response.error) {
				accountUsage(response.usage, providerCopy);
				if (!weak) {
					return;
				}
			}
			if (response.error || _record.info.titleFinal) {
				return;
			}
			const auto title = CleanTitle(ResponseText(response.message));
			if (title.isEmpty()) {
				return;
			}
			_record.info.title = title;
			_record.info.titleFinal = true;
			(void)publishRecord();
		});
}

void Session::applyRename(const QString &title) {
	_record.info.title = title;
	_record.info.titleFinal = true;
	(void)publishRecord();
}

} // namespace Agent
