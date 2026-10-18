/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_openai.h"
#include "agent/core/agent_request.h"

#include <memory>
#include <utility>

namespace Agent::Tests {
namespace {

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
		sent.back().handlers.head(HttpHead{ status, QByteArray() });
	}
	void data(const QByteArray &chunk) {
		sent.back().handlers.data(chunk);
	}
	void finished(const QString &error = QString()) {
		sent.back().handlers.finished(error);
	}

	std::vector<Sent> sent;
	std::vector<uint64> cancelled;

private:
	uint64 nextId = 1;

};

void ReplayFixture(
		FakeTransport &transport,
		const QByteArray &bytes,
		int parts) {
	transport.head(200);
	const auto step = bytes.size() / parts;
	for (auto i = 0; i != parts; ++i) {
		transport.data(bytes.mid(i * step, (i == parts - 1)
			? bytes.size() - i * step
			: step));
	}
	transport.finished();
}

[[nodiscard]] ProviderSettings TestSettings() {
	return ProviderSettings{
		u"test"_q,
		ProviderKind::OpenAiCompatible,
		u"Test"_q,
		u"https://provider.test/v1"_q,
		u"key"_q,
		u"gpt-test"_q,
		128000,
		false,
	};
}

[[nodiscard]] Request TestRequest() {
	return Request{
		u"gpt-test"_q,
		u"system"_q,
		{ Message{ Role::User, { TextBlock{ u"hi"_q } } } },
		{},
		0,
	};
}

} // namespace

std::vector<Check> RequestChecks() {
	return {
		{ u"request_success"_q, [] {
			auto transport = std::make_unique<FakeTransport>();
			auto done = 0;
			std::optional<Response> response;
			auto request = SingleRequest(
				std::make_unique<OpenAi::Adapter>(TestSettings()),
				transport.get(),
				TestRequest(),
				[&](Response value) {
					++done;
					response = std::move(value);
				});
			const auto bytes = ReadFixture(u"openai-text.sse"_q);
			if (bytes.isEmpty()) {
				return u"fixture read failed"_q;
			}
			ReplayFixture(*transport, bytes, 4);
			if (done != 1) {
				return u"done called %1 times"_q.arg(done);
			} else if (response->error) {
				return u"unexpected error: %1"_q.arg(response->error->text);
			}
			auto text = QString();
			for (const auto &block : response->message.blocks) {
				if (const auto blockText = std::get_if<TextBlock>(&block)) {
					text = blockText->text;
				}
			}
			if (text != u"Привет, мир!"_q) {
				return u"answer: %1"_q.arg(text);
			}
			return QString();
		} },
		{ u"request_network_error"_q, [] {
			auto transport = std::make_unique<FakeTransport>();
			auto done = 0;
			std::optional<Response> response;
			auto request = SingleRequest(
				std::make_unique<OpenAi::Adapter>(TestSettings()),
				transport.get(),
				TestRequest(),
				[&](Response value) {
					++done;
					response = std::move(value);
				});
			transport->finished(u"connection lost"_q);
			if (done != 1) {
				return u"done called %1 times"_q.arg(done);
			} else if (!response->error) {
				return u"expected a network error"_q;
			} else if (response->error->kind != ErrorKind::Network) {
				return u"error kind differs"_q;
			} else if (response->error->text != u"connection lost"_q) {
				return u"error text differs"_q;
			}
			return QString();
		} },
		{ u"request_destroy_cancels"_q, [] {
			auto transport = std::make_unique<FakeTransport>();
			auto done = 0;
			{
				auto request = SingleRequest(
					std::make_unique<OpenAi::Adapter>(TestSettings()),
					transport.get(),
					TestRequest(),
					[&](Response) { ++done; });
				transport->head(200);
				transport->data(u"partial"_q.toUtf8());
			}
			if (done != 0) {
				return u"done called on destruction"_q;
			} else if (transport->cancelled.size() != 1) {
				return u"expected one cancel call"_q;
			}
			transport->finished(u"late"_q);
			if (done != 0) {
				return u"done called after destruction"_q;
			}
			return QString();
		} },
		{ u"request_done_destroys_self"_q, [] {
			auto transport = std::make_unique<FakeTransport>();
			auto done = 0;
			auto held = std::unique_ptr<SingleRequest>();
			held = std::make_unique<SingleRequest>(
				std::make_unique<OpenAi::Adapter>(TestSettings()),
				transport.get(),
				TestRequest(),
				[&](Response) {
					++done;
					held.reset();
				});
			transport->finished();
			if (done != 1) {
				return u"done called %1 times"_q.arg(done);
			} else if (!transport->cancelled.empty()) {
				return u"self-destruction cancelled the finished request"_q;
			}
			return QString();
		} },
	};

}

} // namespace Agent::Tests
