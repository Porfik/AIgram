/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_session.h"

#include "agent/agent_manager.h"
#include "agent/agent_exclusions.h"
#include "agent/agent_memories.h"
#include "agent/agent_spending_tracker.h"
#include "agent/core/agent_context.h"
#include "agent/core/agent_loop.h"
#include "agent/core/agent_memory.h"
#include "agent/core/agent_policy.h"
#include "agent/core/agent_prompts.h"
#include "agent/core/agent_request.h"
#include "agent/core/agent_spending.h"
#include "agent/tools/agent_tools.h"
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "crl/crl_on_main.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <rpl/producer.h>

namespace Agent {
namespace {

constexpr auto kProvisionalTitleLimit = 60;

[[nodiscard]] QString ProvisionalTitle(const QString &text) {
	for (const auto &line : text.split('\n')) {
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			return trimmed.left(kProvisionalTitleLimit);
		}
	}
	return {};
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

} // namespace

Session::Session(not_null<Manager*> manager, SessionRecord record)
: _manager(manager)
, _record(std::move(record))
, _gate(std::make_unique<PolicyGate>(
	_record.info.scope,
	[=](PeerKey peer) { return manager->exclusions().excluded(peer); },
	_record.tainted))
, _tools(std::make_unique<ToolRegistry>(
	&manager->session(),
	_gate.get(),
	_record.loadedGroups)) {
	_repairedOnLoad = CloseInterruptedTurn(_record);
	_undoneReported = int(_record.undone.size());
	_state.pendingCards = CountPendingCards(_record.cards);
	_tools->touched(
	) | rpl::on_next([=](PeerKey peer) {
		if (!ranges::contains(_record.info.touched, peer)) {
			_record.info.touched.push_back(peer);
		}
	}, _lifetime);
	_tools->cardUpdates(
	) | rpl::on_next([=](Card card) {
		applyCard(std::move(card));
	}, _lifetime);
	_tools->loadedGroupsChanges(
	) | rpl::on_next([=] {
		_record.loadedGroups = _tools->loadedGroups();
		publishRecord();
	}, _lifetime);
}

void Session::finishOpen() {
	if (_repairedOnLoad) {
		_repairedOnLoad = false;
		publishRecord();
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
	const auto weak = base::make_weak(this);
	publishRecord();
	if (!weak) {
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
	const auto weak = base::make_weak(this);
	_queue.clear();
	_state.queued.clear();
	_compaction = nullptr;
	_titleRequest = nullptr;
	if (_loop && _loop->busy()) {
		_loop->interrupt();
		if (!weak) {
			return;
		}
	}
	if (_state.status != SessionStatus::Idle) {
		_state.status = SessionStatus::Idle;
		_stateChanges.fire({});
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
	const auto provider = ActiveProvider(_manager->providers());
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
				publishRecord();
				if (!weak) {
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
	Expects(!request.text.isEmpty() || !request.hint.isEmpty());

	auto message = Message{ .role = Role::User, .blocks = {} };
	if (!request.text.isEmpty()) {
		message.blocks.push_back(TextBlock{ request.text });
	}
	if (!request.untrusted.isEmpty()) {
		message.blocks.push_back(
			TextBlock{ WrapUntrusted(request.untrusted), true });
	}
	auto hint = request.hint;
	if (const auto undone = undoHint(); !undone.isEmpty()) {
		hint = hint.isEmpty() ? undone : hint + '\n' + undone;
	}
	if (!hint.isEmpty()) {
		message.blocks.push_back(TextBlock{ HintText(hint), true });
	}
	Expects(!message.blocks.empty());

	if (_record.messages.empty()) {
		_record.info.title = ProvisionalTitle(request.text);
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
	if (!request.untrusted.isEmpty()) {
		_gate->markTainted();
	}
	_overflowRetried = false;
	_state.error.reset();
	_state.lastEnd.reset();

	const auto weak = base::make_weak(this);
	_record.messages.push_back(std::move(message));
	_recordChanges.fire({});
	if (!weak) {
		return;
	}
	publishRecord();
	if (!weak) {
		return;
	}
	const auto provider = ActiveProvider(_manager->providers());
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
	applyState(
		SessionStatus::Failed,
		Error{
			ErrorKind::BadRequest,
			0,
			u"No AI provider is set up."_q,
		},
		TurnEnd::Failed);
}

void Session::accountUsage(
		const Usage &usage,
		const ProviderSettings &provider) {
	const auto priced = PriceUsage(usage, provider);
	_record.info.usage += priced;
	_manager->spending().add(priced);
}

QString Session::buildSystem() const {
	auto brief = std::optional<ChatBrief>();
	if (_record.info.scope.kind == ScopeKind::Chat) {
		if (const auto peer = _manager->session().data().peerLoaded(
				PeerId(_record.info.scope.peer))) {
			brief = MakeChatBrief(peer);
		}
	}
	return SystemPrompt({
		.scope = _record.info.scope,
		.account = AccountLine(&_manager->session()),
		.instructions = _manager->providers().instructions,
		.memory = FormatMemory(
			_manager->memories().get(_record.info.scope.peer)),
		.chat = brief,
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
	auto window = _manager->contextWindow();
	if (force) {
		request.messages = ContextMessages(_record);
		window = std::min(window, EstimateTokens(request));
	}
	return PlanCompaction(_record, fixed, window);
}

void Session::startCompaction(int until) {
	const auto provider = ActiveProvider(_manager->providers());
	auto adapter = provider ? CreateAdapter(*provider) : nullptr;
	if (!provider || !adapter) {
		failWithoutProvider();
		return;
	}
	if (!applyState(SessionStatus::Compacting)) {
		return;
	}
	const auto providerCopy = *provider;
	_compaction = std::make_unique<SingleRequest>(
		std::move(adapter),
		&_manager->transport(),
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
					publishRecord();
				}
			}
			if (!weak) {
				return;
			}
			runTurn();
		});
}

void Session::runTurn() {
	const auto provider = ActiveProvider(_manager->providers());
	if (!provider) {
		failWithoutProvider();
		return;
	}
	runLoop(*provider);
}

void Session::runLoop(const ProviderSettings &provider) {
	auto adapter = CreateAdapter(provider);
	if (!adapter) {
		failWithoutProvider();
		return;
	}
	if (!applyState(SessionStatus::Working)) {
		return;
	}
	_loopLifetime.destroy();
	_loop = nullptr;
	_loop = std::make_unique<Loop>(
		std::move(adapter),
		&_manager->transport(),
		_tools.get(),
		LoopOptions{
			.model = provider.model,
			.system = buildSystem(),
			.maxOutputTokens = 0,
			.maxSteps = 25,
		},
		ContextMessages(_record));
	_loopContext = int(_loop->history().size());
	_loopBase = int(_record.messages.size());
	_loopProvider = provider;
	_loop->events(
	) | rpl::on_next([=](EngineEvent event) {
		handleEvent(std::move(event));
	}, _loopLifetime);
	_loop->resume();
}

void Session::handleEvent(EngineEvent event) {
	const auto weak = base::make_weak(this);
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
	const auto failed = (turn.end == TurnEnd::Failed);
	if (!applyState(
			failed ? SessionStatus::Failed : SessionStatus::Idle,
			turn.error,
			turn.end)) {
		return false;
	}
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
		&& !_record.info.titleFinal
		&& !_titleRequested) {
		startTitleRequest();
	}
	processQueue();
}

void Session::compactAndResume() {
	const auto provider = ActiveProvider(_manager->providers());
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
	_record.messages.resize(_loopBase);
	_record.messages.reserve(_loopBase + history.size() - _loopContext);
	for (auto i = _loopContext; i < int(history.size()); ++i) {
		_record.messages.push_back(history[i]);
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
	const auto weak = base::make_weak(this);
	_manager->save(this);
	if (!weak) {
		return false;
	}
	_recordChanges.fire({});
	return true;
}

void Session::startTitleRequest() {
	const auto provider = ActiveProvider(_manager->providers());
	auto adapter = provider ? CreateAdapter(*provider) : nullptr;
	if (!provider || !adapter) {
		return;
	}
	_titleRequested = true;
	const auto providerCopy = *provider;
	_titleRequest = std::make_unique<SingleRequest>(
		std::move(adapter),
		&_manager->transport(),
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
			publishRecord();
		});
}

void Session::applyRename(const QString &title) {
	_record.info.title = title;
	_record.info.titleFinal = true;
	publishRecord();
}

} // namespace Agent
