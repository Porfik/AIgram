/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_loop.h"

#include <deque>
#include <memory>
#include <utility>

namespace Agent::Tests {
namespace {

struct FakeParserState {
	std::deque<StreamDelta> deltas;
	std::vector<HttpHead> heads;
	Response response;
	QString finishNetworkError;
	std::vector<QByteArray> chunks;
	int finishCount = 0;
};

class FakeParser final : public ResponseParser {
public:
	explicit FakeParser(std::shared_ptr<FakeParserState> state)
	: _state(std::move(state)) {
	}

	void head(const HttpHead &head) override {
		_state->heads.push_back(head);
	}
	void feed(const QByteArray &chunk) override {
		_state->chunks.push_back(chunk);
	}
	[[nodiscard]] std::vector<StreamDelta> takeDeltas() override {
		auto result = std::vector<StreamDelta>(
			std::make_move_iterator(_state->deltas.begin()),
			std::make_move_iterator(_state->deltas.end()));
		_state->deltas.clear();
		return result;
	}
	[[nodiscard]] Response finish(const QString &networkError) override {
		++_state->finishCount;
		_state->finishNetworkError = networkError;
		return _state->response;
	}

private:
	std::shared_ptr<FakeParserState> _state;

};

class FakeAdapter final : public Adapter {
public:
	[[nodiscard]] HttpRequest prepare(const Request &request) const override {
		prepared.push_back(request);
		return HttpRequest{
			u"https://provider.test/v1/chat/completions"_q,
			{},
			QByteArray("body"),
		};
	}
	[[nodiscard]] std::unique_ptr<ResponseParser> parser() const override {
		auto state = std::make_shared<FakeParserState>();
		states.push_back(state);
		return std::make_unique<FakeParser>(state);
	}

	mutable std::vector<Request> prepared;
	mutable std::vector<std::shared_ptr<FakeParserState>> states;

};

class FakeTransport final : public Transport {
public:
	struct Sent {
		uint64 id = 0;
		HttpRequest request;
		TransportHandlers handlers;
	};

	[[nodiscard]] uint64 start(
			HttpRequest request,
			TransportHandlers handlers) override {
		sent.push_back({ nextId++, std::move(request), std::move(handlers) });
		return sent.back().id;
	}
	void cancel(uint64 requestId) override {
		cancelled.push_back(requestId);
	}

	void head(int status = 200) {
		auto handler = sent.back().handlers.head;
		handler(HttpHead{ status, QByteArray() });
	}
	void data(const QByteArray &chunk) {
		auto handler = sent.back().handlers.data;
		handler(chunk);
	}
	void finished(const QString &error = QString()) {
		auto handler = sent.back().handlers.finished;
		handler(error);
	}

	std::vector<Sent> sent;
	std::vector<uint64> cancelled;

private:
	uint64 nextId = 1;

};

class FakeExecutor final : public ToolExecutor {
public:
	struct Pending {
		QString id;
		Fn<void(ToolResultBlock)> done;
	};

	explicit FakeExecutor(std::vector<ToolSpec> specs, bool synchronous = false)
	: _specs(std::move(specs))
	, _synchronous(synchronous) {
	}

	[[nodiscard]] std::vector<ToolSpec> tools() const override {
		return _specs;
	}
	void execute(ToolUseBlock call, Fn<void(ToolResultBlock)> done) override {
		if (_synchronous) {
			done(ToolResultBlock{ call.id, u"sync result"_q, false });
			return;
		}
		pending.push_back({ call.id, std::move(done) });
	}
	void cancel(const QString &toolUseId) override {
		cancelled.push_back(toolUseId);
	}

	std::vector<Pending> pending;
	std::vector<QString> cancelled;

private:
	std::vector<ToolSpec> _specs;
	bool _synchronous = false;

};

struct Harness {
	FakeAdapter *adapter = nullptr;
	std::unique_ptr<FakeTransport> transport;
	std::unique_ptr<FakeExecutor> executor;
	std::unique_ptr<Loop> loop;
	rpl::lifetime lifetime;
	std::vector<EngineEvent> events;

	explicit Harness(
			LoopOptions options = LoopOptions(),
			bool synchronousTools = false)
	: transport(std::make_unique<FakeTransport>())
	, executor(std::make_unique<FakeExecutor>(
		std::vector<ToolSpec>{
			ToolSpec{ u"echo"_q, u"Echoes the input."_q, QJsonObject() },
		},
		synchronousTools)) {
		auto ownedAdapter = std::make_unique<FakeAdapter>();
		adapter = ownedAdapter.get();
		loop = std::make_unique<Loop>(
			std::move(ownedAdapter),
			transport.get(),
			executor.get(),
			std::move(options));
		loop->events() | rpl::on_next([&](EngineEvent &&event) {
			events.push_back(std::move(event));
		}, lifetime);
	}

	[[nodiscard]] FakeParserState &state(int step = 0) {
		return *adapter->states[step];
	}

};

[[nodiscard]] Message UserText(QString text) {
	return Message{ Role::User, { TextBlock{ std::move(text) } } };
}

[[nodiscard]] Message AssistantText(QString text) {
	return Message{ Role::Assistant, { TextBlock{ std::move(text) } } };
}

[[nodiscard]] ToolUseBlock ToolUse(QString id, QString name = u"echo"_q) {
	return ToolUseBlock{
		std::move(id),
		std::move(name),
		QJsonObject(),
		QString(),
		QByteArray(),
	};
}

[[nodiscard]] ToolUseBlock BadToolUse(QString id) {
	return ToolUseBlock{
		std::move(id),
		u"echo"_q,
		QJsonObject(),
		u"not json"_q,
		QByteArray("garbage"),
	};
}

[[nodiscard]] QString ToolUseId(const EngineEvent &event) {
	if (const auto call = std::get_if<ToolCallRequested>(&event)) {
		return call->call.id;
	} else if (const auto done = std::get_if<ToolCallFinished>(&event)) {
		return done->result.toolUseId;
	}
	return QString();
}

[[nodiscard]] std::vector<int> AlternativeIndices(
		const std::vector<EngineEvent> &events) {
	auto result = std::vector<int>();
	result.reserve(events.size());
	for (const auto &event : events) {
		result.push_back(event.index());
	}
	return result;
}

[[nodiscard]] QString EventList(const std::vector<EngineEvent> &events) {
	auto result = QString();
	for (const auto &event : events) {
		if (!result.isEmpty()) {
			result += u", "_q;
		}
		result += QString::number(event.index());
	}
	return result;
}

[[nodiscard]] const TurnFinished *FindTurnFinished(
		const std::vector<EngineEvent> &events) {
	for (auto i = events.rbegin(); i != events.rend(); ++i) {
		if (const auto finished = std::get_if<TurnFinished>(&*i)) {
			return finished;
		}
	}
	return nullptr;
}

[[nodiscard]] QString ExpectOneTurnFinished(
		const std::vector<EngineEvent> &events,
		TurnEnd end,
		bool hasError = false,
		int from = 0) {
	auto count = 0;
	for (auto i = from, total = int(events.size()); i != total; ++i) {
		if (std::get_if<TurnFinished>(&events[i])) {
			++count;
		}
	}
	if (count != 1) {
		return u"expected one TurnFinished, got "_q + QString::number(count);
	}
	const auto finished = FindTurnFinished(events);
	if (finished->end != end) {
		return u"expected TurnEnd "_q
			+ QString::number(int(end))
			+ u", got "_q
			+ QString::number(int(finished->end));
	} else if (finished->error.has_value() != hasError) {
		return u"unexpected error presence"_q;
	} else if (!std::get_if<TurnFinished>(&events.back())) {
		return u"TurnFinished is not the last event"_q;
	}
	return QString();
}

[[nodiscard]] QString ExpectUsage(
		const std::vector<EngineEvent> &events,
		int input,
		int output,
		int cached,
		int position) {
	if (position >= int(events.size())) {
		return u"no UsageReported at "_q + QString::number(position);
	}
	const auto reported = std::get_if<UsageReported>(&events[position]);
	if (!reported) {
		return u"expected UsageReported at "_q + QString::number(position);
	} else if (reported->usage.inputTokens != input
		|| reported->usage.outputTokens != output
		|| reported->usage.cachedInputTokens != cached) {
		return u"usage differs"_q;
	}
	return QString();
}

[[nodiscard]] bool SameBlock(const Block &a, const Block &b) {
	if (a.index() != b.index()) {
		return false;
	}
	switch (a.index()) {
	case 0: return std::get<TextBlock>(a).text
		== std::get<TextBlock>(b).text;
	case 1: {
		const auto &x = std::get<ImageBlock>(a);
		const auto &y = std::get<ImageBlock>(b);
		return x.bytes == y.bytes && x.mimeType == y.mimeType;
	}
	case 2: {
		const auto &x = std::get<ThinkingBlock>(a);
		const auto &y = std::get<ThinkingBlock>(b);
		return x.text == y.text && x.signature == y.signature;
	}
	case 3: {
		const auto &x = std::get<ToolUseBlock>(a);
		const auto &y = std::get<ToolUseBlock>(b);
		return x.id == y.id
			&& x.name == y.name
			&& x.input == y.input
			&& x.inputError == y.inputError
			&& x.rawInput == y.rawInput;
	}
	case 4: {
		const auto &x = std::get<ToolResultBlock>(a);
		const auto &y = std::get<ToolResultBlock>(b);
		return x.toolUseId == y.toolUseId
			&& x.content == y.content
			&& x.error == y.error;
	}
	}
	return false;
}

[[nodiscard]] QString SameMessages(
		const std::vector<Message> &a,
		const std::vector<Message> &b) {
	if (a.size() != b.size()) {
		return u"message count differs"_q;
	}
	for (auto i = 0, count = int(a.size()); i != count; ++i) {
		if (a[i].role != b[i].role) {
			return u"role of message "_q
				+ QString::number(i)
				+ u" differs"_q;
		} else if (a[i].blocks.size() != b[i].blocks.size()) {
			return u"block count of message "_q
				+ QString::number(i)
				+ u" differs"_q;
		}
		for (auto j = 0, blocks = int(a[i].blocks.size()); j != blocks; ++j) {
			if (!SameBlock(a[i].blocks[j], b[i].blocks[j])) {
				return u"block "_q + QString::number(j)
					+ u" of message "_q + QString::number(i)
					+ u" differs"_q;
			}
		}
	}
	return QString();
}

} // namespace

std::vector<Check> LoopChecks() {
	return {
		{ u"loop_plain_answer"_q, [] {
			auto h = Harness(LoopOptions{ u"model-x"_q, u"system-x"_q, 100 });
			h.loop->send(UserText(u"hi"_q));
			if (!h.loop->busy()) {
				return u"expected busy after send"_q;
			}
			if (h.transport->sent.size() != 1) {
				return u"expected one request"_q;
			}
			if (h.adapter->prepared.size() != 1) {
				return u"expected one prepared request"_q;
			}
			const auto &request = h.adapter->prepared[0];
			if (request.model != u"model-x"_q
				|| request.system != u"system-x"_q
				|| request.maxOutputTokens != 100
				|| request.tools.size() != 1
				|| request.tools[0].name != u"echo"_q) {
				return u"request options differ"_q;
			}
			const auto historyDiff = SameMessages(
				request.messages,
				{ UserText(u"hi"_q) });
			if (!historyDiff.isEmpty()) {
				return u"first request history: "_q + historyDiff;
			}
			auto &state = h.state();
			state.deltas = {
				TextDelta{ u"Hel"_q },
				TextDelta{ u"lo"_q },
			};
			state.response = Response{
				std::nullopt,
				AssistantText(u"hello"_q),
				StopReason::EndTurn,
				Usage{ 3, 5, 1 },
			};
			h.transport->data("chunk");
			h.transport->finished();
			if (h.loop->busy()) {
				return u"expected not busy after the answer"_q;
			}
			const auto indices = AlternativeIndices(h.events);
			const auto expected = std::vector<int>{
				0, 0, 5, 6,
			};
			if (indices != expected) {
				return u"events: "_q + EventList(h.events);
			}
			const auto diff = ExpectUsage(h.events, 3, 5, 1, 2);
			if (!diff.isEmpty()) {
				return diff;
			}
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Completed);
			if (!finished.isEmpty()) {
				return finished;
			}
			const auto history = SameMessages(
				h.loop->history(),
				{ UserText(u"hi"_q), AssistantText(u"hello"_q) });
			if (!history.isEmpty()) {
				return u"history: "_q + history;
			}
			if (state.finishCount != 1
				|| !state.finishNetworkError.isEmpty()
				|| state.chunks != std::vector<QByteArray>{ QByteArray("chunk") }) {
				return u"parser bookkeeping differs"_q;
			}
			return QString();
		} },
		{ u"loop_tool_round_trip"_q, [] {
			auto h = Harness(LoopOptions{ u"m"_q, u"s"_q, 50 });
			h.loop->send(UserText(u"run it"_q));
			auto &first = h.state();
			first.response = Response{
				std::nullopt,
				Message{
					Role::Assistant,
					{ TextBlock{ u"calling"_q }, ToolUse(u"t1"_q) },
				},
				StopReason::ToolUse,
				Usage{ 2, 3, 0 },
			};
			h.transport->finished();
			if (h.transport->sent.size() != 1 || !h.loop->busy()) {
				return u"expected a busy wait for the tool"_q;
			}
			if (h.executor->pending.size() != 1
				|| h.executor->pending[0].id != u"t1"_q) {
				return u"expected one pending echo call"_q;
			}
			if (h.events.size() != 2
				|| ToolUseId(h.events[1]) != u"t1"_q
				|| !std::get_if<ToolCallRequested>(&h.events[1])) {
				return u"events: "_q + EventList(h.events);
			}
			h.executor->pending[0].done(
				ToolResultBlock{ u"t1"_q, u"result42"_q, false });
			if (h.transport->sent.size() != 2 || !h.loop->busy()) {
				return u"expected a second request"_q;
			}
			if (h.events.size() != 3
				|| !std::get_if<ToolCallFinished>(&h.events[2])) {
				return u"events: "_q + EventList(h.events);
			}
			if (h.adapter->prepared.size() != 2) {
				return u"expected two prepared requests"_q;
			}
			const auto &request = h.adapter->prepared[1];
			const auto historyDiff = SameMessages(
				request.messages,
				{
					UserText(u"run it"_q),
					Message{
						Role::Assistant,
						{ TextBlock{ u"calling"_q }, ToolUse(u"t1"_q) },
					},
					Message{
						Role::User,
						{ ToolResultBlock{ u"t1"_q, u"result42"_q, false } },
					},
				});
			if (!historyDiff.isEmpty()) {
				return u"second request history: "_q + historyDiff;
			}
			auto &second = h.state(1);
			second.response = Response{
				std::nullopt,
				AssistantText(u"done"_q),
				StopReason::EndTurn,
				Usage{ 10, 4, 0 },
			};
			h.transport->finished();
			if (h.loop->busy()) {
				return u"expected the turn to end"_q;
			}
			const auto usage = ExpectUsage(h.events, 10, 4, 0, 3);
			if (!usage.isEmpty()) {
				return usage;
			}
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Completed);
			if (!finished.isEmpty()) {
				return finished;
			}
			if (h.loop->history().size() != 4) {
				return u"expected four history messages"_q;
			}
			return QString();
		} },
		{ u"loop_two_tools_reverse_order"_q, [] {
			auto h = Harness();
			h.loop->send(UserText(u"tools"_q));
			h.state().response = Response{
				std::nullopt,
				Message{
					Role::Assistant,
					{ ToolUse(u"t1"_q), ToolUse(u"t2"_q) },
				},
				StopReason::ToolUse,
			};
			h.transport->finished();
			if (h.executor->pending.size() != 2) {
				return u"expected two pending tools"_q;
			}
			if (h.events.size() != 2
				|| ToolUseId(h.events[0]) != u"t1"_q
				|| ToolUseId(h.events[1]) != u"t2"_q) {
				return u"events: "_q + EventList(h.events);
			}
			h.executor->pending[1].done(
				ToolResultBlock{ u"t2"_q, u"second"_q, false });
			if (h.transport->sent.size() != 1) {
				return u"expected the wait for t1"_q;
			}
			h.executor->pending[0].done(
				ToolResultBlock{ u"t1"_q, u"first"_q, false });
			if (h.transport->sent.size() != 2 || !h.loop->busy()) {
				return u"expected a second request"_q;
			}
			if (h.events.size() != 4
				|| ToolUseId(h.events[2]) != u"t2"_q
				|| ToolUseId(h.events[3]) != u"t1"_q) {
				return u"events: "_q + EventList(h.events);
			}
			const auto &results = h.adapter->prepared[1].messages[2];
			if (results.blocks.size() != 2) {
				return u"expected two result blocks"_q;
			}
			const auto *one = std::get_if<ToolResultBlock>(&results.blocks[0]);
			const auto *two = std::get_if<ToolResultBlock>(&results.blocks[1]);
			if (!one || !two
				|| one->toolUseId != u"t1"_q
				|| one->content != u"first"_q
				|| two->toolUseId != u"t2"_q
				|| two->content != u"second"_q) {
				return u"results are not in tool use order"_q;
			}
			h.state(1).response = Response{
				std::nullopt,
				AssistantText(u"done"_q),
				StopReason::EndTurn,
			};
			h.transport->finished();
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Completed);
			if (!finished.isEmpty()) {
				return finished;
			}
			if (h.loop->history().size() != 4) {
				return u"expected four history messages"_q;
			}
			return QString();
		} },
		{ u"loop_synchronous_tool"_q, [] {
			auto h = Harness(LoopOptions{}, true);
			h.loop->send(UserText(u"now"_q));
			h.state().response = Response{
				std::nullopt,
				Message{ Role::Assistant, { ToolUse(u"t1"_q) } },
				StopReason::ToolUse,
			};
			h.transport->finished();
			if (h.transport->sent.size() != 2 || !h.loop->busy()) {
				return u"expected an immediate second request"_q;
			}
			if (h.events.size() != 2
				|| ToolUseId(h.events[0]) != u"t1"_q
				|| ToolUseId(h.events[1]) != u"t1"_q
				|| !std::get_if<ToolCallRequested>(&h.events[0])
				|| !std::get_if<ToolCallFinished>(&h.events[1])) {
				return u"events: "_q + EventList(h.events);
			}
			const auto &results = h.adapter->prepared[1].messages[2];
			const auto *result = std::get_if<ToolResultBlock>(
				&results.blocks[0]);
			if (!result
				|| result->toolUseId != u"t1"_q
				|| result->content != u"sync result"_q
				|| result->error) {
				return u"unexpected synchronous result"_q;
			}
			h.state(1).response = Response{
				std::nullopt,
				AssistantText(u"done"_q),
				StopReason::EndTurn,
			};
			h.transport->finished();
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Completed);
			if (!finished.isEmpty()) {
				return finished;
			}
			return QString();
		} },
		{ u"loop_invalid_tool_arguments"_q, [] {
			auto h = Harness();
			h.loop->send(UserText(u"break"_q));
			h.state().response = Response{
				std::nullopt,
				Message{ Role::Assistant, { BadToolUse(u"t1"_q) } },
				StopReason::ToolUse,
			};
			h.transport->finished();
			if (!h.executor->pending.empty()) {
				return u"expected no executor call"_q;
			}
			if (h.transport->sent.size() != 2 || !h.loop->busy()) {
				return u"expected a second request"_q;
			}
			if (h.events.size() != 2
				|| !std::get_if<ToolCallRequested>(&h.events[0])) {
				return u"events: "_q + EventList(h.events);
			}
			const auto *result = std::get_if<ToolCallFinished>(&h.events[1]);
			if (!result
				|| result->result.toolUseId != u"t1"_q
				|| result->result.content
					!= u"Invalid tool arguments: not json"_q
				|| !result->result.error) {
				return u"unexpected invalid arguments result"_q;
			}
			h.state(1).response = Response{
				std::nullopt,
				AssistantText(u"done"_q),
				StopReason::EndTurn,
			};
			h.transport->finished();
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Completed);
			if (!finished.isEmpty()) {
				return finished;
			}
			const auto &raw = h.adapter->prepared[1].messages[2].blocks;
			if (raw.size() != 1) {
				return u"expected one result block"_q;
			}
			const auto *block = std::get_if<ToolResultBlock>(&raw[0]);
			if (!block || block->content != u"Invalid tool arguments: not json"_q) {
				return u"history result differs"_q;
			}
			return QString();
		} },
		{ u"loop_step_limit"_q, [] {
			auto h = Harness(LoopOptions{ u"m"_q, u"s"_q, 0, 1 });
			h.loop->send(UserText(u"limit"_q));
			h.state().response = Response{
				std::nullopt,
				Message{ Role::Assistant, { ToolUse(u"t1"_q) } },
				StopReason::ToolUse,
				Usage{ 1, 1, 0 },
			};
			h.transport->finished();
			h.executor->pending[0].done(
				ToolResultBlock{ u"t1"_q, u"result"_q, false });
			if (h.transport->sent.size() != 1) {
				return u"expected no second request"_q;
			}
			if (h.loop->busy()) {
				return u"expected the turn to end"_q;
			}
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::StepLimit);
			if (!finished.isEmpty()) {
				return finished;
			}
			const auto history = SameMessages(
				h.loop->history(),
				{
					UserText(u"limit"_q),
					Message{ Role::Assistant, { ToolUse(u"t1"_q) } },
					Message{
						Role::User,
						{ ToolResultBlock{ u"t1"_q, u"result"_q, false } },
					},
				});
			if (!history.isEmpty()) {
				return u"history: "_q + history;
			}
			return QString();
		} },
		{ u"loop_transport_error"_q, [] {
			auto h = Harness(LoopOptions{ u"m"_q, u"s"_q, 0, 25 });
			h.loop->send(UserText(u"fail"_q));
			h.state().response = Response{
				Error{ ErrorKind::RateLimited, 429, u"rate limited"_q, 1000 },
				Message(),
				StopReason::EndTurn,
				Usage{ 1, 2, 0 },
			};
			h.transport->finished();
			if (h.loop->busy()) {
				return u"expected the turn to fail"_q;
			}
			const auto usage = ExpectUsage(h.events, 1, 2, 0, 0);
			if (!usage.isEmpty()) {
				return usage;
			}
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Failed,
				true);
			if (!finished.isEmpty()) {
				return finished;
			}
			if (!FindTurnFinished(h.events)->error
				|| FindTurnFinished(h.events)->error->text != u"rate limited"_q
				|| FindTurnFinished(h.events)->error->retryAfter != 1000) {
				return u"error details differ"_q;
			}
			if (h.loop->history().size() != 1) {
				return u"expected the response to be dropped"_q;
			}
			const auto before = int(h.events.size());
			h.loop->send(UserText(u"again"_q));
			auto &second = h.state(1);
			second.response = Response{
				Error{ ErrorKind::Network, 0, u"reset"_q },
				Message(),
				StopReason::EndTurn,
			};
			h.transport->finished(u"connection reset"_q);
			if (second.finishNetworkError != u"connection reset"_q) {
				return u"expected the network error in finish()"_q;
			}
			const auto again = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Failed,
				true,
				before);
			if (!again.isEmpty()) {
				return u"second failure: "_q + again;
			} else if (!FindTurnFinished(h.events)->error
				|| FindTurnFinished(h.events)->error->text != u"reset"_q) {
				return u"second error details differ"_q;
			}
			return QString();
		} },
		{ u"loop_interrupt_streaming"_q, [] {
			auto h = Harness();
			h.loop->send(UserText(u"hello"_q));
			h.state().deltas = { TextDelta{ u"Hel"_q } };
			h.transport->data("1");
			h.state().deltas = { TextDelta{ u"lo"_q } };
			h.transport->data("2");
			if (h.events.size() != 2
				|| h.loop->history().size() != 1) {
				return u"events: "_q + EventList(h.events);
			}
			h.loop->interrupt();
			if (h.loop->busy()) {
				return u"expected the turn to end"_q;
			}
			if (h.transport->cancelled.size() != 1
				|| h.transport->cancelled[0] != h.transport->sent[0].id) {
				return u"expected the request to be cancelled"_q;
			}
			if (!h.executor->cancelled.empty()) {
				return u"expected no tool cancels"_q;
			}
			const auto finished = ExpectOneTurnFinished(
				h.events,
				TurnEnd::Interrupted);
			if (!finished.isEmpty()) {
				return finished;
			}
			const auto history = SameMessages(
				h.loop->history(),
				{ UserText(u"hello"_q), AssistantText(u"Hello"_q) });
			if (!history.isEmpty()) {
				return u"history: "_q + history;
			}
			const auto seen = h.events.size();
			h.transport->data("late");
			h.transport->finished();
			if (h.events.size() != seen
				|| h.loop->busy()
				|| h.loop->history().size() != 2) {
				return u"expected the late handlers to be ignored"_q;
			}
			return QString();
		} },
		{ u"loop_interrupt_tools"_q, [] {
			auto h = Harness();
			h.loop->send(UserText(u"tools"_q));
			h.state().response = Response{
				std::nullopt,
				Message{
					Role::Assistant,
					{ TextBlock{ u"working"_q }, ToolUse(u"t1"_q), ToolUse(u"t2"_q) },
				},
				StopReason::ToolUse,
				Usage{ 1, 1, 0 },
			};
			h.transport->finished();
			h.executor->pending[0].done(
				ToolResultBlock{ u"t1"_q, u"ok"_q, false });
			h.loop->interrupt();
			if (h.loop->busy()) {
				return u"expected the turn to end"_q;
			}
			if (h.executor->cancelled != std::vector<QString>{ u"t2"_q }) {
				return u"expected only t2 to be cancelled"_q;
			}
			auto *finishedEvent = FindTurnFinished(h.events);
			if (!finishedEvent || finishedEvent->end != TurnEnd::Interrupted) {
				return u"events: "_q + EventList(h.events);
			}
			auto finishedCount = 0;
			for (const auto &event : h.events) {
				if (std::get_if<TurnFinished>(&event)) {
					++finishedCount;
				}
			}
			if (finishedCount != 1) {
				return u"expected one TurnFinished"_q;
			}
			const auto history = SameMessages(
				h.loop->history(),
				{
					UserText(u"tools"_q),
					Message{
						Role::Assistant,
						{
							TextBlock{ u"working"_q },
							ToolUse(u"t1"_q),
							ToolUse(u"t2"_q),
						},
					},
					Message{
						Role::User,
						{
							ToolResultBlock{ u"t1"_q, u"ok"_q, false },
							ToolResultBlock{
								u"t2"_q,
								u"Interrupted by the user."_q,
								true,
							},
						},
					},
				});
			if (!history.isEmpty()) {
				return u"history: "_q + history;
			}
			const auto seen = h.events.size();
			h.executor->pending.back().done(
				ToolResultBlock{ u"t2"_q, u"late"_q, false });
			if (h.events.size() != seen
				|| h.loop->history().size() != 3
				|| h.loop->busy()) {
				return u"expected the late done call to be ignored"_q;
			}
			return QString();
		} },
		{ u"loop_send_from_turn_finished"_q, [] {
			auto h = Harness(LoopOptions{ u"m"_q, u"s"_q, 0, 3 });
			auto resent = false;
			h.loop->events() | rpl::on_next([&](EngineEvent &&event) {
				const auto finished = std::get_if<TurnFinished>(&event);
				if (finished && !resent && finished->end == TurnEnd::Completed) {
					resent = true;
					h.loop->send(UserText(u"again"_q));
				}
			}, h.lifetime);
			h.loop->send(UserText(u"first"_q));
			h.state().response = Response{
				std::nullopt,
				AssistantText(u"first-reply"_q),
				StopReason::EndTurn,
			};
			h.transport->finished();
			if (!resent || h.transport->sent.size() != 2 || !h.loop->busy()) {
				return u"expected a second turn from the handler"_q;
			}
			h.state(1).response = Response{
				std::nullopt,
				AssistantText(u"second-reply"_q),
				StopReason::EndTurn,
			};
			h.transport->finished();
			if (h.loop->busy()) {
				return u"expected the second turn to end"_q;
			}
			auto finishedCount = 0;
			for (const auto &event : h.events) {
				if (std::get_if<TurnFinished>(&event)) {
					++finishedCount;
				}
			}
			if (finishedCount != 2) {
				return u"expected two TurnFinished events"_q;
			}
			const auto history = SameMessages(
				h.loop->history(),
				{
					UserText(u"first"_q),
					AssistantText(u"first-reply"_q),
					UserText(u"again"_q),
					AssistantText(u"second-reply"_q),
				});
			if (!history.isEmpty()) {
				return u"history: "_q + history;
			}
			return QString();
		} },
		{ u"loop_destroy_while_busy"_q, [] {
			{
				auto h = Harness();
				h.loop->send(UserText(u"dying"_q));
				if (!h.loop->busy()) {
					return u"expected busy after send"_q;
				}
				h.loop.reset();
				if (!h.events.empty()) {
					return u"expected no events on destruction"_q;
				} else if (h.transport->cancelled
					!= std::vector<uint64>{ h.transport->sent[0].id }) {
					return u"expected the request to be cancelled"_q;
				}
				h.transport->data("late");
				h.transport->finished(u"late"_q);
				if (!h.events.empty()) {
					return u"expected the late handlers to be ignored"_q;
				}
			}
			auto cancelledTools = std::vector<QString>();
			auto lateOk = true;
			{
				auto h = Harness();
				h.loop->send(UserText(u"dying"_q));
				h.state().response = Response{
					std::nullopt,
					Message{ Role::Assistant, { ToolUse(u"t1"_q), ToolUse(u"t2"_q) } },
					StopReason::ToolUse,
				};
				h.transport->finished();
				if (h.executor->pending.size() != 2) {
					return u"expected two pending tools"_q;
				}
				h.loop.reset();
				if (h.events.size() != 2) {
					return u"expected no events on destruction"_q;
				}
				cancelledTools = h.executor->cancelled;
				h.transport->data("late");
				h.transport->finished(u"late"_q);
				h.executor->pending[0].done(
					ToolResultBlock{ u"t1"_q, u"late"_q, false });
				h.executor->pending[1].done(
					ToolResultBlock{ u"t2"_q, u"late"_q, false });
				lateOk = (h.events.size() == 2);
			}
			if (cancelledTools != std::vector<QString>{ u"t1"_q, u"t2"_q }) {
				return u"expected both tools to be cancelled"_q;
			}
			if (!lateOk) {
				return u"expected the late handlers to be ignored"_q;
			}
			return QString();
		} },
	};
}

} // namespace Agent::Tests
