/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/agent_anthropic_stream.h"

#include "base/algorithm.h"
#include "base/flat_map.h"

#include <QtCore/QJsonDocument>

namespace Agent::Anthropic {
namespace {

[[nodiscard]] ErrorKind KindForStatus(int status) {
	switch (status) {
	case 401:
	case 403: return ErrorKind::Auth;
	case 429: return ErrorKind::RateLimited;
	case 408:
	case 500:
	case 502:
	case 503:
	case 504:
	case 529: return ErrorKind::Overloaded;
	}
	if (status / 100 == 4) {
		return ErrorKind::BadRequest;
	}
	return ErrorKind::BadResponse;
}

} // namespace

struct StreamAssembler::State {
	enum class Kind : uchar {
		Text,
		Thinking,
		RedactedThinking,
		ToolUse,
	};
	struct BlockData {
		Kind kind = Kind::Text;
		QString text;
		QByteArray signature;
		QString id;
		QString name;
		QByteArray json;
	};

	base::flat_map<int, BlockData> blocks;
	std::vector<StreamDelta> deltas;
	std::optional<Error> error;
	int rawInput = 0;
	int rawCacheWrite = 0;
	int rawCacheRead = 0;
	int rawOutput = 0;
	bool started = false;
	bool stopped = false;
	bool sawStopReason = false;
	StopReason stopReason = StopReason::EndTurn;

	void startMessage(const QJsonObject &message);
	void startBlock(int index, const QJsonObject &block);
	void applyDelta(int index, const QJsonObject &delta);
	void applyMessageDelta(const QJsonObject &event);
	void appendBlocks(std::vector<Agent::Block> &to) const;
	[[nodiscard]] bool hasToolUse() const;
	[[nodiscard]] Usage usage() const;

private:
	[[nodiscard]] BlockData *blockAt(int index);
	void applyUsage(const QJsonObject &usage);
	void pushText(TextDelta &&delta);
	void pushThinking(ThinkingDelta &&delta);
};

StreamAssembler::State::BlockData *StreamAssembler::State::blockAt(int index) {
	const auto found = blocks.find(index);
	return (found == blocks.end()) ? nullptr : &found->second;
}

void StreamAssembler::State::applyUsage(const QJsonObject &usage) {
	if (usage.contains(u"input_tokens"_q)) {
		rawInput = usage.value(u"input_tokens"_q).toInt();
	}
	if (usage.contains(u"cache_creation_input_tokens"_q)) {
		rawCacheWrite = usage
			.value(u"cache_creation_input_tokens"_q).toInt();
	}
	if (usage.contains(u"cache_read_input_tokens"_q)) {
		rawCacheRead = usage
			.value(u"cache_read_input_tokens"_q).toInt();
	}
	if (usage.contains(u"output_tokens"_q)) {
		rawOutput = usage.value(u"output_tokens"_q).toInt();
	}
}

void StreamAssembler::State::pushText(TextDelta &&delta) {
	if (!deltas.empty()) {
		if (const auto last = std::get_if<TextDelta>(&deltas.back())) {
			last->text += delta.text;
			return;
		}
	}
	deltas.push_back(std::move(delta));
}

void StreamAssembler::State::pushThinking(ThinkingDelta &&delta) {
	if (!deltas.empty()) {
		if (const auto last = std::get_if<ThinkingDelta>(&deltas.back())) {
			last->text += delta.text;
			return;
		}
	}
	deltas.push_back(std::move(delta));
}

void StreamAssembler::State::startMessage(const QJsonObject &message) {
	started = true;
	rawInput = 0;
	rawCacheWrite = 0;
	rawCacheRead = 0;
	rawOutput = 0;
	applyUsage(message.value(u"usage"_q).toObject());
}

void StreamAssembler::State::startBlock(int index, const QJsonObject &block) {
	if (index < 0) {
		return;
	}
	const auto type = block.value(u"type"_q).toString();
	if (type == u"text"_q) {
		auto &entry = blocks[index];
		entry.kind = Kind::Text;
		entry.text = block.value(u"text"_q).toString();
	} else if (type == u"thinking"_q) {
		auto &entry = blocks[index];
		entry.kind = Kind::Thinking;
		entry.text = block.value(u"thinking"_q).toString();
		entry.signature = block.value(u"signature"_q).toString().toUtf8();
	} else if (type == u"redacted_thinking"_q) {
		auto &entry = blocks[index];
		entry.kind = Kind::RedactedThinking;
		entry.signature = block.value(u"data"_q).toString().toUtf8();
	} else if (type == u"tool_use"_q) {
		auto &entry = blocks[index];
		entry.kind = Kind::ToolUse;
		entry.id = block.value(u"id"_q).toString();
		entry.name = block.value(u"name"_q).toString();
		deltas.push_back(ToolUseStarted{ entry.id, entry.name });
	} else {
		blocks.remove(index);
	}
}

void StreamAssembler::State::applyDelta(int index, const QJsonObject &delta) {
	if (index < 0) {
		return;
	}
	const auto block = blockAt(index);
	if (!block) {
		return;
	}
	const auto type = delta.value(u"type"_q).toString();
	if (type == u"text_delta"_q) {
		const auto text = delta.value(u"text"_q).toString();
		if (!text.isEmpty()) {
			block->text += text;
			pushText(TextDelta{ text });
		}
	} else if (type == u"thinking_delta"_q) {
		const auto text = delta.value(u"thinking"_q).toString();
		if (!text.isEmpty()) {
			block->text += text;
			pushThinking(ThinkingDelta{ text });
		}
	} else if (type == u"signature_delta"_q) {
		block->signature += delta.value(u"signature"_q).toString().toUtf8();
	} else if (type == u"input_json_delta"_q) {
		block->json += delta.value(u"partial_json"_q).toString().toUtf8();
	}
}

void StreamAssembler::State::applyMessageDelta(const QJsonObject &event) {
	const auto delta = event.value(u"delta"_q).toObject();
	const auto reason = delta.value(u"stop_reason"_q);
	if (reason.isString()) {
		sawStopReason = true;
		stopReason = ParseStopReason(reason.toString());
	}
	applyUsage(event.value(u"usage"_q).toObject());
}

bool StreamAssembler::State::hasToolUse() const {
	for (const auto &entry : blocks) {
		if (entry.second.kind == Kind::ToolUse) {
			return true;
		}
	}
	return false;
}

Usage StreamAssembler::State::usage() const {
	auto result = Usage();
	result.inputTokens = rawInput + rawCacheWrite + rawCacheRead;
	result.cachedInputTokens = rawCacheRead;
	result.cacheWriteTokens = rawCacheWrite;
	result.outputTokens = rawOutput;
	return result;
}

void StreamAssembler::State::appendBlocks(std::vector<Agent::Block> &to) const {
	for (const auto &entry : blocks) {
		const auto &block = entry.second;
		switch (block.kind) {
		case Kind::Text: {
			if (!block.text.isEmpty()) {
				to.push_back(TextBlock{ block.text });
			}
			break;
		}
		case Kind::Thinking: {
			to.push_back(ThinkingBlock{
				block.text,
				block.signature,
				false });
			break;
		}
		case Kind::RedactedThinking: {
			to.push_back(ThinkingBlock{
				QString(),
				block.signature,
				true });
			break;
		}
		case Kind::ToolUse: {
			auto use = ToolUseBlock();
			use.id = block.id;
			use.name = block.name;
			if (block.json.isEmpty()) {
				use.input = QJsonObject();
			} else {
				const auto document = QJsonDocument::fromJson(block.json);
				if (document.isObject()) {
					use.input = document.object();
				} else {
					use.inputError = u"Arguments are not a JSON object"_q;
					use.rawInput = block.json;
				}
			}
			to.push_back(std::move(use));
			break;
		}
		}
	}
}

StreamAssembler::StreamAssembler()
: _state(std::make_unique<State>()) {
}

StreamAssembler::~StreamAssembler() = default;

void StreamAssembler::event(const QJsonObject &event) {
	const auto type = event.value(u"type"_q).toString();
	if (type == u"message_start"_q) {
		_state->startMessage(event.value(u"message"_q).toObject());
	} else if (type == u"content_block_start"_q) {
		_state->startBlock(
			event.value(u"index"_q).toInt(-1),
			event.value(u"content_block"_q).toObject());
	} else if (type == u"content_block_delta"_q) {
		_state->applyDelta(
			event.value(u"index"_q).toInt(-1),
			event.value(u"delta"_q).toObject());
	} else if (type == u"message_delta"_q) {
		_state->applyMessageDelta(event);
	} else if (type == u"message_stop"_q) {
		_state->stopped = true;
	} else if (type == u"error"_q) {
		if (!_state->error) {
			_state->error = ParseErrorObject(
				event.value(u"error"_q).toObject(),
				0);
		}
	}
}

std::vector<StreamDelta> StreamAssembler::takeDeltas() {
	return base::take(_state->deltas);
}

bool StreamAssembler::started() const {
	return _state->started;
}

bool StreamAssembler::stopped() const {
	return _state->stopped;
}

const std::optional<Error> &StreamAssembler::error() const {
	return _state->error;
}

Response StreamAssembler::finish() const {
	auto response = Response();
	if (_state->error) {
		response.error = *_state->error;
		return response;
	} else if (!_state->started || !_state->sawStopReason) {
		auto error = Error();
		error.kind = ErrorKind::BadResponse;
		error.text = u"Stream ended without a stop reason"_q;
		response.error = std::move(error);
		return response;
	}
	auto blocks = std::vector<Block>();
	_state->appendBlocks(blocks);
	response.message = Message{ Role::Assistant, std::move(blocks) };
	response.stopReason = _state->hasToolUse()
		? StopReason::ToolUse
		: _state->stopReason;
	response.usage = _state->usage();
	return response;
}

Usage ParseUsage(const QJsonObject &usage) {
	auto result = Usage();
	result.inputTokens = usage.value(u"input_tokens"_q).toInt()
		+ usage.value(u"cache_creation_input_tokens"_q).toInt()
		+ usage.value(u"cache_read_input_tokens"_q).toInt();
	result.cachedInputTokens = usage
		.value(u"cache_read_input_tokens"_q).toInt();
	result.cacheWriteTokens = usage
		.value(u"cache_creation_input_tokens"_q).toInt();
	result.outputTokens = usage.value(u"output_tokens"_q).toInt();
	return result;
}

StopReason ParseStopReason(const QString &reason) {
	if (reason == u"tool_use"_q) {
		return StopReason::ToolUse;
	} else if (reason == u"max_tokens"_q
		|| reason == u"model_context_window_exceeded"_q) {
		return StopReason::MaxTokens;
	} else if (reason == u"refusal"_q) {
		return StopReason::Refusal;
	}
	return StopReason::EndTurn;
}

ErrorKind ErrorKindForType(const QString &type, int status) {
	if (type == u"authentication_error"_q
		|| type == u"permission_error"_q
		|| type == u"billing_error"_q) {
		return ErrorKind::Auth;
	} else if (type == u"rate_limit_error"_q) {
		return ErrorKind::RateLimited;
	} else if (type == u"overloaded_error"_q
		|| type == u"api_error"_q
		|| type == u"timeout_error"_q) {
		return ErrorKind::Overloaded;
	} else if (type == u"invalid_request_error"_q
		|| type == u"not_found_error"_q
		|| type == u"request_too_large"_q) {
		return ErrorKind::BadRequest;
	}
	return KindForStatus(status);
}

Error ParseErrorObject(const QJsonObject &error, int status) {
	auto result = Error();
	result.kind = ErrorKindForType(
		error.value(u"type"_q).toString(),
		status);
	result.httpStatus = status;
	result.text = error.value(u"message"_q).toString();
	return result;
}

} // namespace Agent::Anthropic
