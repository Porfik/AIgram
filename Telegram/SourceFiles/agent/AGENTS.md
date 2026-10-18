# agent — AI agent

The product plan is `docs/ai-agent-plan.md`. This map covers placement,
contracts and invariants of the code under `SourceFiles/agent/`.

## Placement

- `agent/core/` is the pure `td_agent` library from `cmake/td_agent.cmake`.
  It links only `lib_base` (with `rpl` and `crl`) and Qt Core. It must not
  include anything from the client: no `Main::Session`, `Data::`, `Ui::`,
  `Storage::`, `MTP::`, styles or `lang`. Everything there is testable
  without a running client.
- `agent/core/tests/` is the `td_agent_test` executable, built only by an
  explicit `--target td_agent_test`. Each module has its own
  `agent_<module>_tests.cpp` returning a `std::vector<Check>`; recorded
  provider responses live in `agent/core/tests/fixtures/`.
- `agent/` (outside `core`) is the client side: the Qt network transport,
  sessions, tools, policy and agent UI. It may include `agent/core`, never
  the other way round.
- The settings page is `settings/sections/settings_agent*`. Encrypted storage
  of provider settings is `Storage::Domain` in `storage/storage_domain*`,
  next to the wallet keyring.

## Conversation model (`agent_types.h`)

- A `Message` is a role and a list of blocks. User messages carry
  `TextBlock`, `ImageBlock` and `ToolResultBlock`; assistant messages carry
  `TextBlock`, `ThinkingBlock` and `ToolUseBlock`.
- `ThinkingBlock::signature` is opaque provider data that must round-trip
  unchanged. Adapters that can't send thinking back drop the block from
  requests.
- `ToolUseBlock` with a non-empty `inputError` means the model produced
  arguments that are not a JSON object: `input` is empty and `rawInput`
  keeps the exact text. Requests send `rawInput` back verbatim; the loop
  answers such a call with an error `ToolResultBlock` without executing it.
- `Error::retryAfter` is in milliseconds, zero when the provider gave none.

## Transport (`agent_transport.h`)

- `Transport::start()` returns a non-zero id and calls the handlers
  asynchronously, never from inside `start()`: `head` once when the status
  line and headers arrive, `data` for every body chunk in order, then
  `finished` exactly once with an empty string on success or a network error
  text. After `cancel(id)` no handler of that request is called again, and
  cancelling an unknown or finished id is a no-op.
- A non-2xx status is not a network error: `head` reports it, `data` carries
  the error body and `finished` gets an empty string.
- `HttpTransport` in `agent/agent_http_transport.*` is the client
  implementation over `QNetworkAccessManager`. It inherits the application
  proxy that `core/sandbox.cpp` installs, and never logs request bodies or
  the `Authorization` header.

## Adapters (`agent_adapter.h`)

- An `Adapter` is pure protocol translation for one provider: `prepare()`
  turns a `Request` into an `HttpRequest`, `parser()` makes a fresh
  `ResponseParser` for one response. No I/O, timers or globals.
- A `ResponseParser` gets `head()` once, then `feed()` with raw body chunks
  split at arbitrary byte positions, including inside UTF-8 sequences and
  inside SSE lines. `takeDeltas()` returns what became visible since the
  previous call. `finish()` is called once after the transport finished and
  returns the complete `Response`: either `error` set, or the assistant
  `message` with `stopReason` and `usage`.
- A response that has tool uses ends with `StopReason::ToolUse` even when the
  provider reported a plain stop.
- `OpenAi::Adapter` speaks Chat Completions with `stream: true` and
  `stream_options.include_usage`. It posts to `<baseUrl>/chat/completions`
  with `Authorization: Bearer <apiKey>` (omitted when the key is empty, for
  local servers). It maps tool results to `tool` role messages, images to
  `image_url` data URLs, reads `reasoning` and `reasoning_content` deltas as
  thinking, assembles `tool_calls` fragments by `index`, generates
  `call_<index>` ids when the provider sends none, and reports in-stream
  `error` objects and non-2xx bodies through `ParseError()`.

## Loop and engines (`agent_engine.h`, `agent_loop.h`)

- `Engine` is what sessions and UI talk to; `Loop` is the native engine and
  the Claude Code engine of stage 4 implements the same interface.
- `send()` appends a user message and runs a turn: request, stream, then
  execute every tool use through `ToolExecutor`, append one user message with
  all results in the order of the tool uses, and request again, until the
  model stops without tool uses, `maxSteps` requests were made, or an error.
- `send()` while `busy()` is a programming error.
- Events arrive in order: deltas while streaming, `ToolCallRequested` and
  `ToolCallFinished` around each execution, `UsageReported` after each
  response, and exactly one `TurnFinished` per `send()`.
- `interrupt()` cancels the request in flight and every running tool. History
  stays valid for the next request: partial assistant text is kept as a
  finished message, and every tool use without a result gets an error result
  saying the user interrupted it. The turn ends with `TurnEnd::Interrupted`.
- Destroying a `Loop` while busy cancels everything and emits nothing.
- The loop does not retry failed requests; it ends the turn with
  `TurnEnd::Failed` and the error.

## Provider settings (`agent_providers.h`)

- `SerializeProviders()` and `DeserializeProviders()` use `QDataStream` with
  a leading format version, so later fields can be appended. Unknown future
  versions and truncated data deserialize to `std::nullopt`.
- The serialized bytes include API keys. They are stored only through the
  encrypted `Storage::Domain` blob and never written to logs.
