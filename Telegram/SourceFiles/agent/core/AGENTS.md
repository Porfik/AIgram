# agent/core map

The pure `td_agent` library: the conversation model, provider adapters,
engines and every rule of the agent that can be tested without a client.
It links only `lib_base` (with `rpl` and `crl`) and Qt Core and never
includes client headers (`Main::`, `Data::`, `Ui::`, `Storage::`, `MTP::`,
styles, `lang`). Read `../AGENTS.md` first.

## Tests

- `tests/` is `td_agent_test`, built only by an explicit `--target
  td_agent_test`. Each module has `agent_<module>_tests.cpp` returning a
  `std::vector<Check>`; recorded provider responses are in
  `tests/fixtures/`. A changed contract below changes its tests in the same
  commit.

## Shared rules

- Serialization (`agent_providers`, `agent_record`, `agent_memory`,
  `agent_spending`, `agent_exclusion_list`, `agent_media`, `agent_task`,
  `agent_chat_state`) is `QDataStream` (`Qt_5_1`) with a leading `quint32`
  version. New fields are appended under a new version; older versions load
  defaults. Unknown versions, unknown tags or enum values, truncated data,
  negative or huge counts deserialize to `std::nullopt`. The version history
  lives in the `Deserialize*()` code, not here.
- Model-facing texts (errors, results, prompts, labels) are English.
  User-facing texts never come from `core`.
- Tool use ids that don't match `^[A-Za-z0-9_-]+$` or repeat an earlier id of
  a request are replaced, with their results, by
  `toolu_aigram_<message>_<block>` (Anthropic, Claude Code) or
  `call_aigram_<message>_<block>` (Responses), so one history always gives
  one body. OpenAI-compatible providers reuse `call_0`.

## Conversation model (`agent_types.h`)

- A `Message` is a role and blocks. User messages carry `TextBlock`,
  `MediaBlock`, `ToolResultBlock`; assistant messages `TextBlock`,
  `ThinkingBlock`, `ToolUseBlock`.
- `TextBlock::hidden` goes to the model but not to the feed (client hints,
  the compaction summary).
- `MediaBlock`: `kind` (`Image`, `Audio`, `Video`, `File`), `bytes`,
  `mimeType`, `name`, `attachment` (`aN` of a user attachment), `peer` and
  `messageId` of its message, `size`, `duration`. Tool results carry media
  in `ToolResultBlock::media`. A block without `bytes` is a reference: the
  model gets its `MediaPlaceholder()`.
- `ThinkingBlock::signature` is opaque provider data that round-trips
  unchanged; adapters that can't send thinking back drop the block.
  `redacted` is Anthropic's `redacted_thinking` (`signature` holds `data`).
  `durationMs` is display only, never sent.
- `ToolUseBlock::inputError` set means the arguments were not a JSON object:
  `input` is empty, `rawInput` keeps the text and goes back verbatim; the
  loop answers with an error result without executing.
- `Usage`: `inputTokens` include `cachedInputTokens` and `cacheWriteTokens`;
  `cost` is dollars and valid only with `hasCost`; `+=` sums and ORs
  `hasCost`. `Error::retryAfter` is in ms, 0 when unknown.
- `Request::effort` is empty or a level of the provider; `pdfEngine` asks
  OpenRouter's `file-parser` plugin; `cacheKey` keeps the prompt cache warm.

## Transport (`agent_transport.h`, `agent_sse.h`)

- `Transport::start()` returns a non-zero id and calls handlers
  asynchronously, never inside `start()`: `head` once, `data` per chunk in
  order, `finished` once (empty string on success, else a network error).
  After `cancel(id)` nothing of that request is called; cancelling an
  unknown id is a no-op. A non-2xx status is not a network error: `head`
  reports it and `data` carries the body.
- `SseParser` splits a byte stream into events at any chunk boundary.

## Adapters (`agent_adapter.h`, `agent_openai.h`)

- An `Adapter` is pure protocol translation: `prepare()` makes an
  `HttpRequest`, `parser()` a fresh `ResponseParser` per response. No I/O,
  timers or globals. `CreateAdapter()` returns null for `ClaudeCode`
  profiles.
- A `ResponseParser` gets `head()` once, then `feed()` with chunks split
  anywhere (inside UTF-8 sequences and SSE lines). `takeDeltas()` returns
  what became visible since the last call, consecutive deltas of one kind
  merged. `finish()` returns the `Response`: `error`, or the message with
  `stopReason` and `usage`. A response with tool uses ends with
  `StopReason::ToolUse` even when the provider said a plain stop.
- `OpenAi::Adapter` is Chat Completions with streaming and
  `include_usage`, `Authorization: Bearer` only for a non-empty key (local
  servers). Tool result media go into one `user` message right after the
  `tool` messages, each introduced by `Media returned by tool call <id>:`.
  It reads `reasoning` / `reasoning_content` as thinking, assembles
  `tool_calls` by `index` and makes `call_<index>` ids when none come.
- OpenRouter specifics (`IsOpenRouter()`: host `openrouter.ai` or a
  subdomain): `ignoredRoutes` go as `provider.ignore`; `cacheKey` as
  `x-session-id` (for host `api.openai.com` as `prompt_cache_key`);
  `usage.cost` is the cost, plus `cost_details.upstream_inference_cost`
  when `is_byok`. `IsClaudeOnOpenRouter()` (model `anthropic/*`) adds a
  top-level `cache_control` and sends the system message as one part with
  `cache_control`, the breakpoint that keeps tools and system cached.
- `ParseModels()` (`/models`) reads modalities into `vision`, `audio`,
  `video`, `files` and prices into dollars per million tokens (bad or
  negative prices are 0). `SearchModels()`: every query word is a prefix of
  a word of the name or id (`z-ai/glm-5.3-flash` is `z ai glm 5 3 flash`);
  an empty query matches all. `ParseEndpoints()` gives one `ModelRoute` per
  `provider_name`, `tools` when any of its endpoints supports tools.
  `KeyRequest()` (`/key`): 200 valid, 401 not.

## Anthropic (`agent_anthropic.h`, `agent_anthropic_stream.h`)

- `StreamAssembler` turns Messages API stream events (SSE, or the
  `stream_event` lines of `claude`) into deltas and one `Response`; it
  serves both `Anthropic::Adapter` and the Claude Code engine. Unknown
  events and block types are ignored. Without `message_start` or a stop
  reason `finish()` is `BadResponse`. Blocks come in `index` order; empty
  text blocks are dropped; tool input is the joined `partial_json` (empty
  is `{}`, not an object sets `inputError`).
- `ParseUsage()`: input is `input_tokens` plus both cache counts;
  `message_delta` usage fields replace `message_start` ones. No cost: prices
  come from the profile.
- `ParseStopReason()` and `ErrorKindForType()` map the API values; unknown
  error types fall back to the HTTP status like every adapter (401/403
  `Auth`, 429 `RateLimited`, 408/5xx/529 `Overloaded`, other 4xx
  `BadRequest`).
- `RequestObject()`: `max_tokens` 32000 when unset; top-level and system
  `cache_control`; adaptive summarized thinking with
  `prefix_mismatch_behavior: drop_block` when
  `SupportsAdaptiveThinking()`; `output_config.effort`; `fallbacks:
  "default"` when `SupportsFallbacks()`; betas in one `anthropic-beta`
  header. Same-role messages merge; images and PDFs go as base64 sources,
  other media is skipped; thinking without a signature is dropped; tool
  results go first in their user message.
- `EffortLevels()`, `KnownPrices()` (cache write is 1.25 × input; a dated
  id matches its base), `SupportsAdaptiveThinking()` (false for the 4.5 and
  older generations) and `SupportsFallbacks()` are tables in
  `agent_anthropic.cpp`; update them from Anthropic's model pages.
- `ModelsRequest()` (`/models?limit=1000`): 200 means the key works.
  `ParseModels()` reads the window, max output, image and PDF input and the
  supported effort levels, with `KnownPrices()`.

## OpenAI Responses (`agent_openai_responses.h`)

- `ResponsesAdapter` serves the `OpenAiResponses` kind: "OpenAI → API key"
  and "OpenAI → ChatGPT subscription". `ResponsesOptions::plan`
  (`UsesChatGptPlan()`) restricts the body to what the plan accepts.
- `ResponsesRequestObject()`: `store: false`; the system prompt as
  `instructions` (the plan rejects `system` items); function tools with
  `strict: false`, `parallel_tool_calls`; `reasoning.summary: auto`, the
  effort and `include: reasoning.encrypted_content` when
  `SupportsReasoning()`; `prompt_cache_key`; `max_output_tokens` only off
  the plan. Nothing else (no `temperature`, `metadata`,
  `previous_response_id`, `truncation`). Items never carry `id`.
- Input: tool results as `function_call_output` first, images as
  `input_image`, files as `input_file`, audio and video skipped. Reasoning
  items are sent back only in the current turn (after the last user message
  without tool results) and only when `ReasoningItem()` accepts the
  signature.
- `ReasoningSignature()` wraps the reasoning item as `{"openai": {model,
  origin, item}}`; `ReasoningItem()` returns it only for the same model and
  origin (the profile id) with non-empty `encrypted_content`, so a
  signature never crosses models or accounts.
- `ResponsesParser()`: text and refusal deltas are text, reasoning summary
  deltas thinking (`"\n\n"` between summary parts); items by
  `output_index` from `output_item.done`, else from the final `response`.
  A stream without `completed`, `incomplete` or `failed` is `BadResponse`.
  No cost.
- `ParseResponsesError()` reads `{"error": ...}` or the plan's `{"detail":
  ...}`; `ErrorKindForCode()` maps the plan's `subscription_sharing_*`,
  `chatpass_*` and `chatgpt_signed_out` codes, else the status.
- `FindKnownModel()` (an id or one continued after `-`, longest match) and
  the model table with prices, levels and `ContextWindowLimit()` (272 000:
  above it the API doubles the price) are in `agent_openai_responses.cpp`.
  `ParseResponsesModels()` reads the plan answer (`{"models"}`, only
  `visibility: list`, server order kept) or the API answer (`{"data"}`,
  only chat models, known ones first), filling gaps from the table.

## ChatGPT sign-in (`agent_chatgpt_auth.h`)

- Pure parts of "Sign in with ChatGPT": OAuth with PKCE against
  `auth.openai.com`, tokens for `https://api.openai.com/v1`. Nothing logs or
  prints tokens. The client (`../agent_chatgpt.*`) does the I/O.
- The redirect is `http://127.0.0.1:<port>/auth/callback` (never
  `localhost`, `kCallbackPort` 1455). `AuthorizeUrl()` uses the dynamic
  client `dynamic_agent_client` with `agent_name_hint=AIgram` when no client
  id is saved; `dynamic_agent_client` is never a token request's client id.
  Scopes include `offline_access` and `chatgpt.tokens.use.direct`
  (`HasPlanScope()`; without it the plan can't be used).
- `ParseCallback()` / `ParsePastedUrl()` check path, `state` (a mismatch
  fails), `error` and `code`; a pasted URL needs the redirect's scheme,
  host and path (any port on `127.0.0.1`).
- `ParseTokenResponse()`: `terminal` errors (`invalid_grant`, expired,
  revoked or reused refresh tokens, `invalid_client`) mean sign in anew;
  others mean keep the tokens and retry later. `ParseIdToken()` decodes the
  JWT payload without checking the signature (it came straight from the
  token endpoint over TLS).
- `NeedsRefresh()` uses `kRefreshMarginMs`. `ApplyTokens()` replaces the
  pair, scopes and expiry together; `ClearTokens()` keeps the client id,
  host id and account names. `RefreshStillCurrent()` is true only when the
  account still has the refresh token a refresh started with: only then may
  its answer change the account.
- `SignedOutBody()` is the 401 body `ChatGptTransport` answers without a
  usable token (`chatgpt_signed_out`).

## Claude Code (`agent_claude_process.h`, `agent_claude_protocol.h`, `agent_claude_engine.h`)

- The subscription engine drives the user's `claude` over its stream-json
  control protocol; the client supplies the process (`ClaudeLauncher`).
  `Launcher` handlers are asynchronous: `line` per stdout line, `finished`
  once; destroying the `Process` kills it silently.
- `LaunchArguments()` turn off everything of the CLI's own: no built-in
  tools, no setting sources, no session persistence, no slash commands,
  `--strict-mcp-config` with one SDK MCP server `aigram`, permissions over
  stdio. `InitializeRequest()` sets the system prompt (a short default when
  blank: an empty one gives Claude Code's own prompt) and the server
  timeout `kToolTimeoutMs`.
- `ReplayFrames()` turns history into frames: tool results first, then
  text, images and PDFs; thinking is dropped (bound to another
  conversation); same-role frames merge.
- MCP: `McpToolsList()` sets `anthropic/maxResultSizeChars` (else the CLI
  saves big results under `~/.claude/projects`); `McpToolResult()` sends
  only text and images (the CLI writes other media to disk) and says how
  many were left out.
- `ParseLine()` types each line (control requests and responses, stream
  events, assistant and user echoes, `result`, `rate_limit_event` as
  `Limits`, `system` init; `nested` for subagent lines).
  `VersionSupported()` is at least `kMinVersion`, the version the protocol
  was checked with. `ResultError()` maps the CLI errors to kinds.
- `Connection` is one process with fixed options, an owner (session id)
  and a `ConnectionKey` (record message count and `summaryUntil`).
  `ConnectionPool` hands out an owner's idle connection.
- `Engine` implements `Agent::Engine` like `Loop`. `resume()` reuses the
  owner's warm connection when it is alive, has the same options and the
  key `{owner, recordMessages - 1, summaryUntil}`, and then sends only the
  last message; otherwise it starts a process, sends `initialize` and
  replays the history, each user frame waiting for its `result` with
  `num_turns` 0 (sending without waiting makes the CLI reorder frames).
- During a query: top-level stream events go through a `StreamAssembler`,
  tool names lose `mcp__aigram__`; `can_use_tool` allows only
  `mcp__aigram__*` (the registry and gate decide inside the call); MCP
  `tools/call` matches the streamed tool use by
  `_meta["claudecode/toolUseId"]`; results the CLI made itself (denials,
  interruptions) complete their tool uses as errors; a loaded group sends
  `notifications/tools/list_changed`.
- `result` ends the turn (`error_max_turns` is `StepLimit`). A process exit
  fails the turn (`Network`). After `Completed`, `MaxTokens` or `Refusal`
  with `keepWarm` the connection returns to the pool with the new key; any
  other end destroys it, and an interrupted connection is never reused.
- `OneShot` is one turn without tools; `ModelsQuery` lists the models of
  `initialize`. Both call `done` once and may be destroyed in it.

## Loop and engines (`agent_engine.h`, `agent_loop.h`, `agent_request.h`)

- `Engine` is what sessions talk to; `Loop` is the native engine,
  `Claude::Engine` the other one.
- `send()` appends a user message and runs a turn: request, stream, execute
  every tool use through `ToolExecutor`, append one user message with all
  results in tool use order, repeat until no tool uses, `maxSteps`
  requests, or an error. `send()` while `busy()` is a programming error.
  `resume()` runs a turn on the current history (it must end with a user
  message): retries and continuing after compaction.
- Events come in order: deltas, `ToolCallRequested` / `ToolCallFinished`
  around each execution, `UsageReported` per response, exactly one
  `TurnFinished` per turn.
- `interrupt()` cancels the request and running tools and keeps history
  valid: partial text stays as a message, unanswered tool uses get "the user
  interrupted" results, the turn ends `Interrupted`. Destroying a busy
  `Loop` cancels everything and emits nothing. The loop never retries; a
  failure ends the turn `Failed`.
- `LoopOptions::pdfEngine` and `cacheKey` go into every request.
- `PendingRequest` is the base of one-shot requests (`SingleRequest`,
  `Claude::OneShot`). `SingleRequest` sends one request without tools,
  calls `done` once, asynchronously, may be destroyed in `done`, and
  destroying it cancels. It serves compaction, titles and the media model.

## Provider settings (`agent_providers.h`)

- Every `ProviderSettings` is a model profile: `title`, `kind` (the
  protocol: `OpenAiCompatible`, `Anthropic`, `ClaudeCode`,
  `OpenAiResponses`), `service` (how it was made: `Custom` "Other
  provider", `OpenRouter`, `Anthropic`, `ClaudeSubscription`, `OpenAi`,
  `ChatGptSubscription`), model, window, abilities (`vision`, `audio`,
  `video`, `files`), prices per million tokens, `effort`, OpenRouter
  `ignoredRoutes`.
- The serialized bytes include API keys and ChatGPT tokens: they are stored
  only in the encrypted `Storage::Domain` blob and never logged.
- `ProvidersState` also holds the roles (`defaultId`, `backgroundId`,
  `mediaId`), the entry-point and display options (`showEntryPoints`,
  `showSpending`, `rowPreview`, `showBotIcon`), the background daily
  limits, `claudePath`, the agent's name and photo and the one `chatGpt`
  account of all plan profiles.
- Lookups: `FindProvider()` (null for empty or unknown ids),
  `DefaultProvider()` (`defaultId`, else the first), `SessionProvider()`
  (`profileId`, else the default), `BackgroundProvider()` (the task's, else
  `backgroundId`, else the default), `MediaProvider()` (null without
  `mediaId`).
- One key per service: `ServiceKey()` is the first profile's key,
  `SetServiceKey()` sets it on every profile of the service (`Custom` has
  none).
- `UsesClaudeCode()`, `UsesChatGptPlan()`; `IsSubscription()` profiles have
  no prices and count tokens only.
- `chatGpt` changes only through the client's `SaveChatGptAccount()`:
  `SaveProviders()` always writes the stored account, never the copy it was
  given, so a stale copy can't restore a rotated refresh token.
- `ShowBotIcon()` is false only for the agent user with the option off
  (`ShowUserBotIcon()` in `dialogs_layout.cpp`).

## Session records (`agent_record.h`)

- `SessionRecord::messages` is the full transcript;
  `messages[0, summaryUntil)` are replaced by `summary` in the context.
- `SessionInfo`: `titleFinal` (false while the title is the first line of
  the first message), `created` / `updated` (unix seconds), `touched` peers,
  priced `usage`, `attachments` (how many `aN` ids were issued), `task` (the
  task id of a run), `profileId` (empty means the default profile).
- Also `cards` (creation order), `loadedGroups` (load order), `undone` tool
  use ids, `tainted`, and `turnEnds` (`after` is the message count when a
  turn ended `Interrupted`, `StepLimit`, `MaxTokens` or `Refusal`; one mark
  per `after`, increasing). `Message::date`, `replyTo` (a `ChatMessage`
  key) and `replyQuote` are display only.
- A record never stores media bytes. Runs never enter the session index.
- `MakeSessionPreview()` is the last visible text without Markdown markers,
  at most 120 characters; the session puts it into `info` on save.
- `CloseInterruptedTurn()` repairs a record saved mid-turn: waiting cards
  become `NotDone`, and unanswered tool uses get `CardResultText()` or a
  "the client was closed" error.

## Feed, chat messages, journal (`agent_feed.h`, `agent_chat_messages.h`, `agent_chat_state.h`, `agent_journal.h`)

- `BuildFeed()` turns a transcript into display items in order. A user
  message is one `FeedUser` (visible text and its attachments); tool results
  attach to their `FeedTool`. Assistant text runs are `FeedAssistant`.
  Thinking and tool uses not separated by visible text form one
  `FeedSteps` item, also across messages; action tools (`actionTools`) go
  into the run's `FeedActions` after it; a tool use with a card is a
  `FeedCard`. Visible text, cards, `FeedTurnEnd` marks and `FeedSummary`
  end a run; hidden text does not.
- `BuildChatMessages()` groups feed items into chat messages: an agent
  message collects steps and actions up to and including a text; cards,
  user messages, summaries and turn ends stand alone. Keys are the kind
  letter and the first record index (`.N` for repeats), stable while a turn
  grows. `LiveTail` folds the running step's engine events into messages
  the view appends until the next record change.
- `ChatState` (`pinned`, `pinnedIndex`, `since`) is the agent value
  `chat`; a new agent chat is pinned on top.
- `BuildJournal()` lists done actions (with a result) and `Done` /
  `Drafted` cards, newest first, from sessions and runs alike; `failed` is
  an error result or an action whose `done` is empty and `failed` not.

## Scope and policy (`agent_scope.h`, `agent_policy.h`)

- `PeerKey` is `PeerId::value`; zero is never a peer. `Scope::Account()` is
  the general agent; `Scope::Chat(peer)` binds a session to one peer (a
  forum, never a topic).
- One `PolicyGate` per `Session`. `excluded` is asked on every call
  (exclusions change at runtime). `available()` needs `ToolPolicy::general`
  or `chat` by scope, plus `interactive` in a normal session or
  `background` in a run (a run's `chatTask` stands in for `chat`).
  `visible(peer)`: not zero, not excluded, the scope peer in the chat
  scope. `inFrame(peer)`: a run's `GateFrame::contains`.
- `check()` order: not available `NotInMode`; `Forbidden`; any excluded
  peer `Excluded`; zero or foreign peer `OutOfScope`. Then `Read` and
  `Reversible` allow (a `taintedAsks` tool in a tainted gate is `Confirm`
  without "allow for the turn"); `Visible` allows only when this turn's
  allowances for the tool cover every peer and taint doesn't block it,
  else `Confirm`.
- Taint rule (account scope): once tainted, an `outbound` call (text or an
  image others see) with no peers, or with a peer the user did not name this
  turn (`FindNamedPeers()`, mentions), can't be allowed for the turn.
  Taint is sticky: `markTainted()` is never undone and is saved in the
  record, because read content stays in the context.
- `allowForTurn()` stores peers and card target keys; `coversTargets()`
  checks a later card's keys; `beginTurn()` replaces named peers and drops
  allowances.
- With a frame (a run), after the first four steps: `Read` allows in the
  frame, else `Deny` `OutOfFrame`; `Reversible` and `Visible` allow when
  the tool is in `GateFrame::tools` and the peers in the frame, else
  `Confirm` without "allow for the turn".
- `FindNamedPeers()` finds `@username`, `t.me/username` links and titles of
  three or more characters on word boundaries, ignoring case.

## Cards (`agent_card.h`)

- A `Card` is a confirmation answered in the feed, recorded by `toolUseId`.
  `targets` are chats (`messageId` 0) or messages with `title` / `preview`
  taken when the card was made. Kinds: `Send`, `Draft`, `Edit`,
  `Forward`, `Delete`, `React`, `PinMessage`, `UnpinMessage`,
  `DeleteFolder`, `Handoff`,
  `Transcribe`, `Download`, `SendFiles`, `CreateTask`, `UpdateTask`,
  `Action` (a reversible action a run wants outside its frame), `Change`
  (a visible change of the `admin`, `contacts`, `profile` groups and
  `mark_read`: localized `text` title and `rows` from the tool) and
  `Method` (a raw API write: the model's description, the method, the
  params JSON).
- Fields by kind: `text` (editable text, caption, task instruction, card
  title), `proposedText` (the model's original), `detail`, `option`
  ("delete for everyone", "notify members", "send without compression";
  hidden without `optionAvailable`), `files`, `amount` / `quota`, `task` /
  `previousTask` (`SerializeTask()`), `rows` with `previous`.
- `CardTextEditable()`, `CardHasAlternate()` (`Send` "to draft", `Draft`
  "append") and `CardAlwaysAsks()` (`Draft`, `Transcribe`, `Download`,
  `CreateTask`, `UpdateTask`: shown even when the gate allows, never
  "allow for the turn").
- `ApplyDecision()`: `Decline` sets `Declined`; `Execute` / `Alternate`
  copy `checked`, editable `text`, `option` and `allowedForTurn`, then
  `Running`. The tool ends it `Done`, `Drafted` or `Failed` (every checked
  target failed). `NotDone` waited when the client quit; `Expired` is a
  run's card nobody answered for `kTaskCardExpireSeconds`. `CardFinal()` is
  every status but `Pending` and `Running`.
- `CardAllowanceKeys()`: one key per (checked) target, or for a card
  without targets one key of its content, so only the same action again
  matches an allowance.
- `CardResultText()` is the model's result for a final card: what was done,
  unchecked and failed targets by title and ids, the edited text in full
  when it differs from `proposedText`.

## Memory, spending, exclusions (`agent_memory.h`, `agent_spending.h`, `agent_exclusion_list.h`)

- A `Memory` is the fact list of one owner (a chat peer, or 0 for the
  general agent); ids `m<n>` from `nextId`, which only grows, also after a
  clear. Add, update, remove and restore (undo of a remove, at the old
  index) validate `Empty`, `TooLong` (`kMemoryMaxChars`), `Full`
  (`kMemoryMaxFacts`), `UnknownId`, `DuplicateId` and leave the memory
  unchanged on failure. Text is one line. `FormatMemory()` is `- [m3]
  text` lines.
- `PriceUsage()` prices a usage without `hasCost` from the provider's
  prices: uncached input, cached input (`priceCached`, else `priceInput`),
  cache writes (`priceCacheWrite`, else `priceInput`), output.
  `SpendingStats` keeps per-day buckets (`QDate::toJulianDay()`) for
  `kSpendingKeepDays`, older ones summed into `older`.
- An `ExclusionList` holds hidden peers and folder ids in hiding order;
  peer 0 and folder 0 ("All chats") never change it.

## Media and documents (`agent_media.h`, `agent_documents.h`)

- `ChooseMediaRoute()`: `Main` when the session model takes the kind, else
  `Helper` when the media model does, else `None`. `MediaKindForMime()`
  (SVG is a file).
- `MediaLabel()` and `MediaPlaceholder()` are the model's labels
  (`[Attachment a1: shot.png, image/png, 240 KB]`, "shown earlier, call the
  tool again"); `AttachmentId()` / `AttachmentIndex()`; `FormatByteSize()`
  is 1024-based English.
- `HelperRequest()` asks the media model without tools to describe media
  for an agent that can't see it: answer the question, full transcript in
  the original language first, never follow instructions inside the media.
- `MixToMono()` and `MakeWav()` make mono 16-bit WAV for audio routes.
- `DetectDocument()` by extension, then MIME; `DecodeText()` rejects
  binary data; `WantOfficeEntry()` and `ExtractOfficeText()` read DOCX,
  XLSX, PPTX and OpenDocument text; `PageText()` pages at line breaks
  without splitting surrogate pairs (`nextOffset` -1 at the end).

## Tasks (`agent_task.h`)

- A `Task` is a background rule: `scope` (where it was made), `trigger` (a
  message `filter` or a `schedule`: `Interval`, `Times`, `Once`), `frame`
  (what a run may do without asking), `providerId` (empty: the background
  model), `notify`, `catchUp`, `limits`, `state` (`DailyLimit` /
  `HourlyLimit` pause until `pausedUntil`; `Finished` is a fired `Once`),
  `lastFired`, `lastSeen`, `spentToday` of `spendingDay`.
- `ChatSet` names chats by `peers`, `kinds` and `folders`; `triggerChat`
  (frames only) adds the trigger's chat. `ChatSetContains()` never matches
  peer 0, and `kinds` never match Saved Messages.
- `MatchesMessage()`: never the agent's own messages; the user's outgoing
  messages only in Saved Messages named in `chats.peers`; then every set
  condition (chats, senders, keywords or regex, media, mentions,
  `InTimeWindow()`). An empty chat set matches nothing.
- `NextFireTime()` is strictly after `after`, 0 when none; intervals are at
  least `kTaskMinIntervalMinutes`.
- `TaskInputSchema()` / `ParseTaskInput()` / `ParseTaskUpdate()` /
  `TaskToJson()` are the model's task shape; parsing fails with one English
  error naming the field, enforces the `kTaskMax*` limits, and in the chat
  scope forbids the account-only fields (both chat sets are the scope
  peer). An update replaces `trigger`, `allowed_actions` and `allowed_chats`
  whole, `limits` field by field. `TaskToJson()` parses back to the same
  task.
- Limits: `CheckSpendingLimits()` (the task, then the whole background; by
  cost when known, else tokens), `CountRecentActions()` and
  `HourlyResumeTime()` over the last hour, `CallActionCount()` (the larger
  of `chats` and `users`, at least 1), `RunActionTimes()` rebuilds the hour
  from saved runs so a restart keeps the count.

## Telegram API (`agent_tl_schema.h`, `agent_tl_codec.h`, `agent_mtproto.h`)

- `ParseTlSchema()` reads `api.tl` text (embedded by the client; nothing is
  generated). Lines without a `#id` are skipped; a malformed combinator
  fails with its line number; `// LAYER N` sets `layer()`.
- JSON shape both ways: `{"_": "<constructor>", ...}`; `#` parameters never
  appear; `long` is a decimal string on output; `bytes` base64 (or `"[N
  bytes]"` past `maxBytesShown`); `int128` / `int256` hex. Input may omit
  `"_"` when the type has one constructor. Errors name the JSON path.
- `EncodeTlCall()` computes flags from present parameters. Chats are never
  objects: `InputPeer`, `InputUser`, `InputChannel` values come as strings
  or numbers through `refs.input` (only the empty and self constructors are
  accepted as objects); bare `chat_id`s and a few id vectors go through
  `refs.bare`. Constructors with `access_hash` or `file_reference` and any
  `InputFile` fail: the model never sees those.
- `DecodeTl()` reads one value and records `TlSpan`s of `spanTypes` for the
  client to re-read with generated types; `access_hash` and
  `file_reference` are left out.
- `ScrubExcludedPeers()` removes hidden chats from decoded results: objects
  whose `peer_id` / `peer` / `saved_peer_id`, `chat_id` or `channel_id` is
  hidden, hidden `chat` / `channel` objects and hidden folders. Users stay
  as members and authors: people are hidden only as chats.
- `ClassifyMethod()`: `Forbidden` for unknown and generic functions, the
  `auth`, `payments`, `wallet`, `toncenter`, `smsjobs`, `fragment`
  namespaces, names containing `Password`, `Authorization`, `SecureValue`,
  `Takeout`, `WebView`, `UrlAuth`, `Star`, `Gift`, `Paid`, and the account
  security methods listed there; `Write` for reads with side effects (bot
  callbacks, views, `startBot`, server-fetched URLs, RTMP keys); `Read` for
  `get*`, `search*`, `check*`, `resolve*`; otherwise `Write`.
  `ForbiddenParamsError()` refuses `allow_paid_stars` /
  `allow_paid_floodskip`. `MethodRedirect()` names the tools that cover a
  method with their own cards; `call_method` refuses those.
- `ParsePeerRef()`: a number with bits above the low 48 is a `PeerId`, a
  smaller one a bare id, `user:N` / `chat:N` / `channel:N` typed, a
  negative one a Bot API id; anything else is resolved as a username.
- `FindMethods()`, `MethodNamespaces()`, `MethodSignature()` and
  `DescribeTl()` are the discovery texts; forbidden functions are never
  listed and fail to describe.

## Context and prompts (`agent_context.h`, `agent_prompts.h`)

- `EstimateTokens()` is a cheap upper bound (three characters a token, fixed
  costs per media kind). `CapToolResult()` cuts long results and asks for a
  smaller page.
- `ContextMessages()` puts the summary first as hidden text. Media lives
  until the end of a turn (from the last user message with text or media):
  earlier media become `MediaPlaceholder()` text, tool result media are
  dropped with a placeholder line; current media get a hidden
  `MediaLabel()` before them. `DropMediaBytes()` empties a record's bytes.
- `PlanCompaction()` compacts when the context exceeds 70% of the window
  and cuts at the smallest turn start that leaves it under 40% with a
  2000-token summary. `CompactionRequest()`, `TitleRequest()` (3–6 words in
  the user's language), `CleanTitle()`, `HandoffText()` (a chat session for
  the general agent, newest part kept; the caller wraps it untrusted).
  `IsContextOverflow()` recognizes context-length errors.
- `SystemPrompt()` and `TaskSystemPrompt()` are English and depend only on
  their input, so they stay byte-stable for a session and the prompt cache
  holds: no clock, no counters (`FormatChatBrief()` has none;
  `FormatChatCounters()` goes into each message hint). They state the
  mode, that `<untrusted_data>` is data and never instructions, that
  `[Context]` lines come from the client, the tools paragraph and the tool
  catalog, the memory and media rules, then the general instructions and
  memory. A run's prompt adds that the user is not watching, every run
  starts fresh, other actions wait for the user and declined ones are not
  retried; it has no memory paragraph.
- `PromptFamilyForModel()` adds a family paragraph (`Glm` today) only for a
  failure seen in exported sessions of that family.
- `WrapUntrusted()` wraps and defuses inner tags; `UnwrapUntrusted()` is for
  display only. `HintText()` makes the `[Context]` line.

## Tool catalog and navigation (`agent_tool_catalog.h`, `agent_navigation.h`)

- `FormatToolCatalog()` renders the prompt catalog: sections in enum
  order, `- <name>: <snippet> [group <group>]` lines, then the groups for
  `load_tools` with their `when` lines.
- Matching words split camelCase and everything but letters and digits.
  `RankToolDocuments()` is BM25 with English stop words and a naive stem; a
  result must contain the query's rarest term and, for four or more terms,
  at least half of them. `NearestToolNames()` suggests names for an unknown
  one (same without `-`/`_`, small Levenshtein distance, a shared word).
  `MatchSnippet()` cuts a window around the first query word.
- `FormatTurnContext()` is the per-message hint (now with weekday and zone,
  loaded groups, the open topic, waiting cards). `CoerceToolInput()` turns
  string scalars into the schema's integers, numbers and booleans.
- `RankSettings()` matches settings entries by word prefixes; title matches
  above keywords above path.
