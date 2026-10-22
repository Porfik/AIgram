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
  `Manager`, `Session`, `SessionStore`, tools in `agent/tools/` and widgets
  in `agent/ui/` (namespace `Agent::Ui`, which pulls in `::Ui` like
  `Calls::Group::Ui`; styles in `agent/ui/agent.style`,
  `agent/ui/agent_panel.style`, `agent/ui/agent_general.style`,
  `agent/ui/agent_media.style`, `agent/ui/agent_queue.style` and
  `agent/ui/agent_tasks.style`). It may include `agent/core`, never the
  other way round.
- Tools: `agent_tool_table.h` holds `ToolEntry` (with `generalSchema`
  replacing `inputSchema` in the account scope when it is set),
  `PendingCall` and the schema helpers; each `agent_tools_<group>.cpp` returns its entries from
  `<Group>Tools::Entries()`: `chat` (reading one chat), `general` (chat
  list, settings navigation, handoff), `actions` (card actions),
  `chat_state` (mute, archive, pin, read), `folders`, `memory`, `media`
  (looking at, listening to and reading message media and attachments),
  `files` (saving and sending files), `admin_members` and `admin_chat`
  (the `admin` group), `contacts`, `profile`, `telegram_ai` and
  `mtproto` (the raw Telegram API; `api.tl` is embedded as the resource
  `:/agent/api.tl` in `telegram.qrc`).
  `agent_tools.cpp` is the `ToolRegistry` and `load_tools`;
  `agent_tool_rights.*` maps admin rights and member restrictions to the
  names tools use.
- `agent_claude.*` is the client side of the Claude Code engine:
  `ClaudeLauncher` (a `QProcess` per process), `ClaudePool`,
  `ClaudeCommand` (one-off `claude` commands such as `auth status`),
  finding the binary and the subscription limits.
- Account data next to the sessions: `Memories` (`agent_memories.*`),
  `Exclusions` (`agent_exclusions.*`) and `SpendingTracker`
  (`agent_spending_tracker.*`), owned by `Manager`; their pure parts are
  `agent_memory`, `agent_exclusion_list` and `agent_spending` in `core`.
  `Attachments` (`agent_attachments.*`) keeps the user's attachments.
- Media: `agent_media` and `agent_documents` in `core` are the pure parts
  (labels, routing, WAV, text extraction); `agent_media_loader.*` finds
  and downloads message media, `agent_media_convert.*` scales images,
  decodes audio and unzips office files, `agent_files.*` is the agent
  folder and saving to it.
- Background tasks: `agent_task` in `core` is the pure part (the task
  model, filters, schedules, parsing the model's input, limits);
  `agent_tasks.*` stores tasks and their runs, `agent_task_runner.*`
  watches triggers and runs tasks, `agent_notifications.*` shows their
  system notifications. Tools are `agent_tools_tasks.cpp` (`TaskTools`).
  UI: `agent_tasks_view.*` the "Tasks" tab, `agent_task_format.*` the
  task texts, `agent_queue.*` the "waiting for your decision" strip and
  box (styles `agent_tasks.style` and `agent_queue.style`).
- The agent chat (stage 5, plan 11.9): `agent_chat.*` owns the local
  agent peer (`AgentChat`: a bot `UserData` with the fake id
  `AgentPeerId()`, its `History` in the real chats list, the chat list
  preview item, the local pin, typing and the waiting-cards badge);
  `core/agent_chat_state` stores the pin, `core/agent_chat_messages`
  turns the feed into chat messages (`BuildChatMessages()`) and keeps the
  live tail of a running turn (`LiveTail`); `core/agent_journal` lists
  the agent's done actions for the journal in the profile.
- UI: `agent_card_box.*` is the native "Edit..." box of a card,
  `agent_section.*` the general agent screen (sessions list and session
  chat), `agent_session_rows.*` the sessions list rows,
  `agent_chat_view.*` the native session view (`ChatView`),
  `agent_chat_items.*` its local history items (`ChatItems`),
  `agent_feed_format.*` the step, action and card texts,
  `agent_profile.*` the agent profile, `agent_history_view.*` the history
  pane with its tabs, `agent_memory_list.*` the memory tab,
  `agent_menus.*` the agent items of the chat and folder menus and
  `agent_spending_format.*` the spending texts.
- Hooks into existing code stay one call or one enum value wide:
  `Main::Session::agent()`, the agent values in `Storage::Account`,
  `Info::Section::Type::Agent`, `Agent::Ui::SetupPanelTopBar()` in
  `Info::WrapWidget::setupTopBarMenuToggle()`, the ✦ button in the history
  top bar, the third column follow in
  `MainWidget::updateThirdColumnToCurrentChat()`,
  `Shortcuts::Command::AgentPanel`, `Shortcuts::Command::AgentPalette`
  (handled in `Dialogs::InnerWidget::setupShortcuts()`),
  `Agent::Ui::AddPassToAgentAction()` in both message context menus, the
  "To agent" button of `HistoryView::TopBarWidget` in selection mode
  (`agentSelectionRequest()`, handled by `HistoryWidget` and
  `HistoryView::ChatWidget`), `Agent::Ui::FillPeerMenu()` in the `Window::PeerMenu`
  filler, `Agent::Ui::FillFolderMenu()` in both folder menus,
  `Agent::Ui::PanelAvailabilityChanges()` in the history top bar and
  `NativeManager::showCustomNotification()` with its activation check at
  the start of `Window::Notifications::Manager::notificationActivated()`.
- The agent chat keeps the agent peer off the server with one-line
  checks of `Agent::IsAgentPeer()` / `IsAgentHistory()`:
  `MainWidget::showHistory()` (opens the general agent screen instead),
  `Window::FillDialogsEntryMenu()` (`Agent::Ui::FillAgentChatMenu()`),
  `ApiWrap::savePinnedOrder()` (left out of the order), the pinned limit
  in `Data::Session::pinnedCanPin()` (`CountPinnedForLimit()`) and
  `maxPinnedChatsLimitValue()` (one extra slot in the main list),
  `ApiWrap::requestFullPeer()`, `Data::Histories::requestDialogEntry()`,
  `Data::ScheduledMessages::request()`,
  cloud drafts (`ShouldSkipPlainDraftCloudSave()`,
  `ApiWrap::savePreparedDraftToCloud()`), `Data::ChatFilter::contains()`
  (never in folders), `Info::Memento` (the agent peer's Info section is
  `Section::Type::Agent`, the agent profile; `thirdSectionIsAgentPanel()`
  skips it, so leaving the agent chat shows the next chat's profile) and
  `Api::ActivateBotButton()` (`ChatItems::Activate()` handles the inline
  buttons of agent items) and `HistoryView::Message::textState()` (agent
  items are no history entries, so their inline keyboard is hit-tested
  like an admin log entry's). `Exclusions::excluded()` is true for the agent
  peer, so tools never see it.
- `HistoryView::ComposeControlsDescriptor` has `customMenuText` /
  `customMenuClicked` (a bot-menu-style button left of the field, the
  session model) and `customStopShown` / `customStopClicked` (the send
  button turns into "Stop" while the field is empty);
  `ChatHelpers::ComposeFeatures::aiEditor` false hides Telegram's AI
  editor button ("✨Ai") in agent composers;
  `ComposeControls::inputField()` gives the field to the agent drafts.
- The settings page is `settings/sections/settings_agent*`. Encrypted storage
  of provider settings is `Storage::Domain` in `storage/storage_domain*`,
  next to the wallet keyring.

## Conversation model (`agent_types.h`)

- A `Message` is a role and a list of blocks. User messages carry
  `TextBlock`, `MediaBlock` and `ToolResultBlock`; assistant messages carry
  `TextBlock`, `ThinkingBlock` and `ToolUseBlock`.
- A `MediaBlock` is media for the model: `kind` (`Image`, `Audio`,
  `Video`, `File`), `bytes`, `mimeType`, `name` (the file name),
  `attachment` (the id `aN` of a user attachment, else empty), `peer` and
  `messageId` of the message it came from (zero for attachments), the
  original `size` in bytes and `duration` in seconds (zero when unknown).
  User messages carry one per attachment; tool results carry them in
  `ToolResultBlock::media`. A media block without `bytes` is only a
  reference: the model gets its `MediaPlaceholder()` text (see
  "Context").
- `Request::pdfEngine`, when not empty, asks the provider to parse PDF
  files with that engine (the OpenRouter `file-parser` plugin).
- `ThinkingBlock::signature` is opaque provider data that must round-trip
  unchanged. Adapters that can't send thinking back drop the block from
  requests. A `redacted` block is an Anthropic `redacted_thinking` block:
  `signature` holds its `data` and `text` is empty.
- `ToolUseBlock` with a non-empty `inputError` means the model produced
  arguments that are not a JSON object: `input` is empty and `rawInput`
  keeps the exact text. Requests send `rawInput` back verbatim; the loop
  answers such a call with an error `ToolResultBlock` without executing it.
- `Error::retryAfter` is in milliseconds, zero when the provider gave none.
- `Usage::cost` is in dollars and meaningful only with `hasCost`. `+=` sums
  tokens and costs and ORs `hasCost`. `inputTokens` include
  `cachedInputTokens` (read from the provider's prompt cache) and
  `cacheWriteTokens` (written to it).
- `Request::effort` is empty (the model's default) or one of
  `Anthropic::EffortLevels()`. Adapters that have no such setting ignore
  it.
- `TextBlock::hidden` text is sent to the model but not shown in the feed:
  client hints and the compaction summary.

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
  local servers). It maps tool results to `tool` role messages and media
  blocks with bytes to content parts (media without bytes is skipped):
  `Image` to `image_url` with a data URL, `Audio` to `input_audio`
  (`data` in base64, `format` `wav` for `audio/wav`, `mp3` for
  `audio/mpeg`, otherwise the MIME subtype), `Video` to `video_url` with
  a data URL and `File` to `file` (`filename`, `file_data` data URL).
  Tool result media go into one `user` message right after that message's
  `tool` messages: for each result with media, in order, a text part
  `Media returned by tool call <id>:` and its parts, then the message's
  other blocks. A request with `pdfEngine` adds `"plugins": [{"id":
  "file-parser", "pdf": {"engine": <pdfEngine>}}]`. It reads `reasoning` and `reasoning_content` deltas as
  thinking, assembles `tool_calls` fragments by `index`, generates
  `call_<index>` ids when the provider sends none, and reports in-stream
  `error` objects and non-2xx bodies through `ParseError()`.
- `ParseModels()` reads `architecture.input_modalities` when present:
  `image` into `vision`, `audio`, `video` and `file` into `audio`,
  `video` and `files` (each set true or false). It reads `name` and
  `pricing.prompt`, `pricing.completion` and `pricing.input_cache_read`
  (dollars per token as strings or numbers) into `priceInput`,
  `priceOutput` and `priceCached` in dollars per million tokens; missing,
  unparsable or negative prices are zero.
- `SearchModels()` returns the indices of the models matching `query`, in
  list order. The query splits into words of letters and digits (case
  ignored); a model matches when every query word is a prefix of a word
  of its `name` or `id` split the same way (`z-ai/glm-5.3-flash` has the
  words `z`, `ai`, `glm`, `5`, `3`, `flash`). An empty or blank query
  matches every model.
- `EndpointsRequest()` is a GET of `<baseUrl>/models/<model>/endpoints`
  (the model id keeps its `/`), `KeyRequest()` a GET of `<baseUrl>/key`;
  both send `Authorization: Bearer <apiKey>` when the key is not empty.
  OpenRouter answers `/key` with 200 for a valid key and 401 otherwise.
- `ParseEndpoints()` reads `data.endpoints` and returns one `ModelRoute`
  per distinct `provider_name` in first-seen order; `tools` is true when
  any endpoint of that provider lists `tools` in
  `supported_parameters`. Malformed JSON gives an empty list.
- `prepare()` copies `ignoredRoutes` of an OpenRouter provider
  (`IsOpenRouter()`) into `Request::ignoredRoutes`; other providers send
  none. `RequestObject()` writes a non-empty `ignoredRoutes` as
  `"provider": {"ignore": [...]}`.
- `Request::cacheKey`, when not empty, keeps the provider's prompt cache
  warm: to OpenRouter (`IsOpenRouter()`) it goes as the `x-session-id`
  header, to a provider whose base URL host is `api.openai.com` as the
  `prompt_cache_key` body field; other providers get nothing.
- For a provider where `IsClaudeOnOpenRouter()` the request body gets a
  top-level `"cache_control": {"type": "ephemeral"}` (automatic caching
  of the conversation tail) and the system message is sent as one text
  content part with `"cache_control": {"type": "ephemeral"}` (the
  breakpoint that keeps tools and system cached). Other providers get
  neither. `usage.prompt_tokens_details.cache_write_tokens`, when
  present, goes to `Usage::cacheWriteTokens`.
- It reads `usage.cost` (OpenRouter) into `Usage::cost` with `hasCost`;
  when `usage.is_byok` is true it adds
  `usage.cost_details.upstream_inference_cost`, which the user's own key
  pays. Without `usage.cost` the usage has no cost.

## Anthropic (`agent_anthropic.h`, `agent_anthropic_stream.h`, stage 4)

- `StreamAssembler` turns Messages API stream events (the JSON object of
  each event, the same for SSE and for the `stream_event` lines of
  `claude`) into deltas and one `Response`. It is shared by
  `Anthropic::Adapter` and the Claude Code engine. Events:
  `message_start` (`message.usage`), `content_block_start` (`index`,
  `content_block` of type `text`, `thinking`, `redacted_thinking` with
  `data`, or `tool_use` with `id`, `name`, `input`), `content_block_delta`
  (`text_delta.text`, `thinking_delta.thinking`,
  `signature_delta.signature`, `input_json_delta.partial_json`),
  `content_block_stop`, `message_delta` (`delta.stop_reason`, `usage`),
  `message_stop`, `ping` and `error` (`error` object). Unknown event
  and block types are ignored.
- Deltas: `TextDelta` per text delta, `ThinkingDelta` per thinking delta,
  `ToolUseStarted` at the start of a `tool_use` block. Consecutive deltas
  of the same kind are merged in `takeDeltas()` like the OpenAI parser.
- `finish()`: an `error` event gives `Response::error` through
  `ParseErrorObject()` (status 0). Without `message_start` or without a
  stop reason it is an error of kind `BadResponse` ("Stream ended without
  a stop reason"). Otherwise the assistant message has the blocks in
  `index` order: `ThinkingBlock` (text and signature; `redacted` with the
  data in `signature`), `TextBlock` (blocks with empty text are dropped),
  `ToolUseBlock` (the joined `partial_json` parsed as an object; empty
  means `{}`; anything else sets `inputError` and keeps `rawInput`, as in
  the OpenAI parser). `stopReason` is `ParseStopReason()`, `ToolUse` when
  the message has tool uses. `usage` is `ParseUsage()` of the
  `message_start` usage with every field the `message_delta` usage has
  replacing it.
- `ParseUsage()`: `inputTokens` is `input_tokens +
  cache_creation_input_tokens + cache_read_input_tokens`,
  `cachedInputTokens` is `cache_read_input_tokens`, `cacheWriteTokens`
  is `cache_creation_input_tokens`, `outputTokens` is `output_tokens`;
  missing fields are zero. No cost.
- `ParseStopReason()`: `tool_use` → `ToolUse`, `max_tokens` and
  `model_context_window_exceeded` → `MaxTokens`, `refusal` → `Refusal`,
  everything else (`end_turn`, `stop_sequence`, `pause_turn`) →
  `EndTurn`.
- `ErrorKindForType()`: `authentication_error`, `permission_error`,
  `billing_error` → `Auth`; `rate_limit_error` → `RateLimited`;
  `overloaded_error`, `api_error`, `timeout_error` → `Overloaded`;
  `invalid_request_error`, `not_found_error`, `request_too_large` →
  `BadRequest`; an unknown type falls back to the HTTP status like the
  OpenAI adapter (401/403 `Auth`, 429 `RateLimited`, 408/5xx/529
  `Overloaded`, other 4xx `BadRequest`, else `BadResponse`).
  `ParseErrorObject()` reads `type` and `message` of an error object.
  `ParseError()` reads a non-2xx body `{"type": "error", "error": {...}}`
  (the message, else the first 500 bytes of the body), with
  `retryAfter` from the head as in the OpenAI adapter.
- `Adapter::prepare()` posts `RequestObject()` (with the profile's model
  when the request has none, and the profile's `effort` when the request
  has none) to `<baseUrl>/messages` with `x-api-key: <apiKey>`,
  `anthropic-version: 2023-06-01`, `content-type: application/json`,
  `accept: text/event-stream` and, when the body uses any, one
  `anthropic-beta` header with the betas joined by commas. The parser
  reads SSE (`SseParser`), feeds each event's JSON to a `StreamAssembler`,
  collects a non-2xx body for `ParseError()` and reports network errors
  like the OpenAI parser.
- `RequestObject()`:
  - `model`, `stream: true`, `max_tokens` (`maxOutputTokens`, 32000 when
    it is not positive), top-level `"cache_control": {"type":
    "ephemeral"}`;
  - `system` (when not empty) as one text block with `"cache_control":
    {"type": "ephemeral"}`;
  - `tools` as `{name, description, input_schema}`;
  - `thinking` `{"type": "adaptive", "display": "summarized",
    "block_binding": {"prefix_mismatch_behavior": "drop_block"}}` with
    the beta `thinking-binding-controls-2026-08-01` when
    `SupportsAdaptiveThinking()` of the model, else no `thinking`;
  - `output_config: {"effort": <effort>}` when `effort` is not empty;
  - `fallbacks: "default"` with the beta
    `server-side-fallback-2026-07-01` when `SupportsFallbacks()`.
  - `messages`: roles `user` and `assistant`; consecutive messages of
    the same role are merged, messages left without content are skipped.
    Text blocks (hidden ones too) become `text` blocks. Media with bytes:
    `Image` → `image` with a base64 source; `File` with `application/pdf`
    → `document` with a base64 source; other media and media without
    bytes are skipped. Thinking blocks with a non-empty `signature`
    become `thinking` (`thinking`, `signature`) or `redacted_thinking`
    (`data`); others are dropped. Tool uses become `tool_use` (`id`,
    `name`, `input`; `{}` when `inputError` is set). Tool results become
    `tool_result` (`tool_use_id`, `is_error` when set, `content` a text
    block followed by the result's media mapped as above) and go first
    in their user message.
  - Tool use ids that don't match `^[A-Za-z0-9_-]+$` or repeat an
    earlier id of the request (OpenAI-compatible providers reuse
    `call_0`) are replaced, with their results, by
    `toolu_aigram_<message index>_<block index>` of the tool use, so the
    same history always gives the same body.
- `EffortLevels()` is `low`, `medium`, `high`, `xhigh`, `max`.
  `KnownPrices()` matches a model id or a dated id that starts with it
  (`claude-haiku-4-5-20251001`): dollars per million tokens input /
  output / cache read, cache write is 1.25 × input. Fable 5.1 10 / 50 /
  0.25; Fable 5 10 / 50 / 1; Opus 5.5 4 / 20 / 0.2; Opus 5, 4.8, 4.7,
  4.6, 4.5 5 / 25 / 0.5; Sonnet 5.5 2 / 10 / 0.2; Sonnet 5 2 / 10 / 0.2;
  Sonnet 4.6, 4.5 3 / 15 / 0.3; Haiku 5.5 0.1 / 0.5 / 0.01; Haiku 4.5
  1 / 5 / 0.1. Unknown models have no prices.
  `SupportsAdaptiveThinking()` is false for `claude-haiku-4-5`,
  `claude-sonnet-4-5`, `claude-opus-4-5`, `claude-opus-4-1`,
  `claude-opus-4-0`, `claude-sonnet-4-0`, `claude-opus-4-2025*`,
  `claude-sonnet-4-2025*` and `claude-3*` ids, true otherwise (newer and
  unknown models). `SupportsFallbacks()` is true for `claude-fable-5-1`,
  `claude-opus-5-5`, `claude-opus-5` and `claude-sonnet-5-5` (and their
  dated ids).
- `ModelsRequest()` is a GET of `<baseUrl>/models?limit=1000` with the
  `x-api-key` and `anthropic-version` headers; 200 means the key works,
  401 or 403 that it doesn't. `ParseModels()` reads `data[]`: `id`,
  `display_name` into `name`, `max_input_tokens` into `contextWindow`,
  `max_tokens` into `maxOutput`, `capabilities.image_input.supported`
  into `vision`, `capabilities.pdf_input.supported` into `pdf`, the
  levels of `EffortLevels()` whose `capabilities.effort.<level>.supported`
  is true into `effortLevels` (in that order), and `KnownPrices()` of the
  id. Malformed JSON gives an empty list.

## OpenAI Responses (`agent_openai_responses.h`, stage 6)

`OpenAi::ResponsesAdapter` speaks the Responses API (`POST
<baseUrl>/responses`) for profiles of kind `OpenAiResponses`: "OpenAI →
API key" and "OpenAI → ChatGPT subscription" (plan 11.11). The ChatGPT
plan accepts only part of the API, so `ResponsesOptions::plan` switches
the body to what it accepts.

- `prepare()` posts `ResponsesRequestObject()` of the request (with the
  profile's `model` when the request has none and the profile's `effort`
  when the request has none) with `options` `{origin: <profile id>, plan:
  UsesChatGptPlan(settings)}` to `<baseUrl>/responses` with
  `content-type: application/json`, `accept: text/event-stream` and
  `Authorization: Bearer <apiKey>` when the key is not empty (ChatGPT
  plan profiles have no key: `ChatGptTransport` adds the token).
  `parser()` is `ResponsesParser(<model>, <profile id>)`.
- `ResponsesRequestObject()`:
  - `model`, `stream: true`, `store: false`;
  - `instructions`: the system prompt, when not empty (the plan rejects
    `system` role items, so the prompt never goes into `input`);
  - `tools` as `{"type": "function", "name", "description",
    "parameters": <inputSchema>, "strict": false}`, then `tool_choice:
    "auto"` and `parallel_tool_calls: true`; all three are left out
    without tools;
  - when `SupportsReasoning(model)`: `reasoning: {"summary": "auto"}`
    plus `"effort": <effort>` when the effort is not empty, and
    `include: ["reasoning.encrypted_content"]`;
  - `prompt_cache_key`: `cacheKey`, when not empty;
  - `max_output_tokens`: when `maxOutputTokens` is positive and not
    `plan` (the plan rejects it). Nothing else: no `temperature`,
    `metadata`, `previous_response_id`, `prompt_cache_retention`,
    `user`, `truncation`.
  - `input`, in message order. A user message gives first one
    `function_call_output` item per `ToolResultBlock` (`call_id`,
    `output`), then, when it has other blocks, one `{"role": "user",
    "content": [...]}` item: text blocks (hidden ones too) as
    `input_text`, media with bytes as `input_image` (`Image`: `image_url`
    a `data:<mime>;base64,` URL, `detail: "auto"`) or `input_file`
    (`File`: `filename`, `file_data` a data URL); `Audio`, `Video` and
    media without bytes are skipped. A result's `output` is its
    `content` string; a result with `Image` or `File` media with bytes
    makes `output` an array: `input_text` with the content (when not
    empty), then the media parts as above. An `error` result is sent like
    any other (its content says what went wrong).
  - An assistant message gives its blocks in order: a `ThinkingBlock`
    becomes the `reasoning` item from `ReasoningItem(signature, model,
    origin)` when that gives one and the message is in the current turn
    (after the last user message that has no `ToolResultBlock`), else
    nothing; a `TextBlock` with text becomes `{"type": "message", "role":
    "assistant", "content": [{"type": "output_text", "text": ...}]}`;
    a `ToolUseBlock` becomes `{"type": "function_call", "call_id",
    "name", "arguments"}` with `arguments` the compact JSON of `input`,
    or `rawInput` verbatim when `inputError` is set. Items never carry
    `id` (with `store: false` ids refer to nothing).
  - Tool use ids that don't match `^[A-Za-z0-9_-]+$` or repeat an
    earlier id of the request become, with their results,
    `call_aigram_<message index>_<block index>`, like the Anthropic
    adapter, so the same history always gives the same body.
- `ReasoningSignature()` is the compact JSON `{"openai": {"model":
  <model>, "origin": <origin>, "item": <item>}}`; `item` is the
  reasoning item as the stream gave it without `id` and `status`
  (`type`, `summary`, `encrypted_content`). `ReasoningItem()` returns
  that item when the signature is such an envelope with the same
  `model` and `origin` and a non-empty `encrypted_content`, else
  nothing. `IsReasoningSignature()` is true when the signature starts
  with `{`.
- `ResponsesParser()` reads SSE (`SseParser`), each event's data a JSON
  object with `type`:
  - `response.output_item.added` with an item of type `function_call`:
    `ToolUseStarted` (`call_id`, `name`);
  - `response.output_text.delta` and `response.refusal.delta`:
    `TextDelta` (`delta`);
  - `response.reasoning_summary_text.delta`: `ThinkingDelta`;
    `response.reasoning_summary_part.added` with `summary_index` above 0
    first gives a `ThinkingDelta` of `"\n\n"`;
  - `response.output_item.done`: keeps the finished `item` by
    `output_index`;
  - `response.completed` and `response.incomplete`: the final
    `response` (its `usage`, `incomplete_details.reason`, and its
    `output` array, used for items no `output_item.done` gave);
  - `response.failed`: the error `response.error` (`code`, `message`);
  - `error`: the error (`code`, `message`, or a nested `error` object);
  - anything else is ignored. Consecutive deltas of the same kind merge
    in `takeDeltas()` like the other parsers.
- `finish()`: a network error gives a `Network` error as in the OpenAI
  parser; a non-2xx status gives `ParseResponsesError()` of the body; a
  `response.failed` or `error` event gives an error with `code`, the
  message as `text` and the kind `ErrorKindForCode(code, <fallback>)`
  (fallback `RateLimited` for `rate_limit_exceeded`, `Overloaded` for
  `server_error`, else `BadResponse`); a stream without `completed`,
  `incomplete` or `failed` is `BadResponse` ("Stream ended without a
  final response event"). Otherwise the assistant message has the items
  in `output_index` order: `reasoning` → `ThinkingBlock` (the summary
  texts joined with `"\n\n"`, `signature` `ReasoningSignature()` when
  `encrypted_content` is not empty, else empty; no block when both are
  empty), `message` →
  `TextBlock` (its `output_text` and `refusal` parts joined; empty text
  gives no block), `function_call` → `ToolUseBlock` (`call_id` as the
  id, `name`, `arguments` parsed like the OpenAI parser: empty is `{}`,
  not an object sets `inputError` and keeps `rawInput`); other item types
  are skipped. `stopReason`: `ToolUse` when there are tool uses, else
  `MaxTokens` for the reason `max_output_tokens`, `Refusal` for
  `content_filter` or a `refusal` part, else `EndTurn`. `usage`:
  `input_tokens` → `inputTokens`, `input_tokens_details.cached_tokens`
  → `cachedInputTokens`, `output_tokens` → `outputTokens`; no cost.
- `ParseResponsesError()` reads `{"error": {"message", "type", "code",
  "param"}}`, or `{"detail": "..."}` (the plan's admission errors), else
  the first 500 bytes of the body as the text; `code` is the error's
  `code` (or `type` when there is no code). The kind is
  `ErrorKindForCode(code, <by status>)`, by status like the OpenAI
  adapter (401/403 `Auth`, 429 `RateLimited`, 408/5xx `Overloaded`,
  other 4xx `BadRequest`, else `BadResponse`); `retryAfter` from the head.
  `ErrorKindForCode()`: `subscription_sharing_usage_limit_exceeded` →
  `RateLimited`; `subscription_sharing_usage_unavailable`,
  `subscription_sharing_user_unavailable` → `Overloaded`;
  `subscription_sharing_user_not_eligible`,
  `subscription_sharing_invalid_user`, `chatpass_v2_scope_not_authorized`,
  `chatpass_v2_invalid_authorization_context`, `invalid_api_key`,
  `chatgpt_signed_out` → `Auth`;
  `subscription_sharing_unsupported_capability`,
  `subscription_sharing_route_not_supported` → `BadRequest`; else the
  fallback.
- `ResponsesEffortLevels()` is `none`, `low`, `medium`, `high`, `xhigh`,
  `max`. `FindKnownModel()` matches an id or an id that continues it after
  a `-` (`gpt-6.1-sol-2026-09-29`, not `gpt-5.4o`), the longest match
  wins, and gives the table entry (`KnownModel::id` is the table id);
  dollars per
  million tokens input / output / cached input, effort levels:
  `gpt-6-astra` "GPT-6 Astra" 10 / 50 / 1, `gpt-6.1-sol` "GPT-6.1 Sol"
  2 / 10 / 0.1, `gpt-6-sol` "GPT-6 Sol" 2 / 10 / 0.2 (these three
  `low` … `max`), `gpt-6-luna` "GPT-6 Luna" 0.1 / 0.5 / 0.01 (`none` …
  `max`), `gpt-5.6-sol` 4 / 20 / 0.4, `gpt-5.6-terra` 2 / 12 / 0.2,
  `gpt-5.6-luna` 0.2 / 1.2 / 0.02, `gpt-5.5` 5 / 30 / 0.5, `gpt-5.5-pro`
  30 / 180 / 0, `gpt-5.4` 2.5 / 15 / 0.25, `gpt-5.4-pro` 30 / 180 / 0,
  `gpt-5.4-mini` 0.75 / 4.5 / 0.075, `gpt-5.4-nano` 0.2 / 1.25 / 0.02,
  `gpt-5.2` 1.75 / 14 / 0.175, `gpt-5.1` 1.25 / 10 / 0.125, `gpt-5`
  1.25 / 10 / 0.125, `gpt-5-mini` 0.25 / 2 / 0.025, `gpt-5-nano`
  0.05 / 0.4 / 0.005 (the `gpt-5*` ones `low`, `medium`, `high`; names
  "GPT-5.6 Sol" and so on). Every known model has the context window
  `ContextWindowLimit()` (272 000: above it the API doubles the price),
  `vision`, `files` and `reasoning`. `SupportsReasoning()` is false for
  ids starting with `gpt-4`, `gpt-3`, `chatgpt-4o`, true otherwise.
- `ResponsesModelsRequest()` is a GET of `<baseUrl>/models` with
  `Authorization: Bearer <apiKey>` when the key is not empty.
  `ParseResponsesModels()` reads either shape:
  - the ChatGPT plan answer `{"models": [...]}`: entries with
    `visibility` other than `list` are skipped (no `visibility` keeps the
    entry), `slug` is the id, `display_name` the name (else the id); when
    present, `context_window` (a number) is the window (at most
    `ContextWindowLimit()`), `input_modalities` (strings) sets `vision`
    (`image`) and `files` (`file`), false when missing from the list,
    `supported_reasoning_levels` (strings,
    or objects with `effort`) the effort levels (only values of
    `ResponsesEffortLevels()`, in that order). The server's order is
    kept.
  - the API answer `{"data": [{"id", ...}]}`: only ids starting with
    `gpt-`, or `o` and a digit, that contain none of `audio`, `realtime`,
    `transcribe`, `tts`, `image`, `embedding`, `moderation`, `search`,
    `instruct`, `codex`; known models (dated ids too) first in the
    table's order, then the rest by id.
  Each model gets what `FindKnownModel()` knows for what the answer
  didn't say (name, window, abilities, prices, levels); unknown models
  get the id as the name, the window `ContextWindowLimit()`, `vision` and
  `files` true and no prices. `tools` is true for every listed model. Malformed JSON gives an
  empty list.

## ChatGPT sign-in (`agent_chatgpt_auth.h`, stage 6)

Pure parts of "Sign in with ChatGPT" for open-source apps (plan 11.11):
OAuth with PKCE against `https://auth.openai.com`, tokens for
`https://api.openai.com/v1`. The client (`agent_chatgpt.*`) does the
I/O. Nothing here logs or prints tokens.

- Constants: `AgentName()` `AIgram` (the `agent_name_hint` users see in
  ChatGPT settings), `CallbackPath()` `/auth/callback`,
  `RedirectUri(port)` `http://127.0.0.1:<port>/auth/callback`
  (`127.0.0.1`, never `localhost`; `kCallbackPort` 1455),
  `UsageUrl()` `https://chatgpt.com/settings/usage`.
- `GeneratePkce()`: `verifier` 32 random bytes (`QRandomGenerator::
  system()`) in base64url without padding, `challenge` the base64url
  (no padding) SHA-256 of the verifier. `GenerateState()` is 32 random
  bytes in base64url (also used as the nonce). `GenerateHostId()` is
  `urn:uuid:` and a random version 4 UUID in lower case.
- `AuthorizeUrl()` is `https://auth.openai.com/api/accounts/authorize`
  with the query (every value percent-encoded): `response_type=code`,
  `client_id` (`dynamic_agent_client` when `params.clientId` is empty,
  else it), `agent_name_hint=AIgram` only with `dynamic_agent_client`,
  `ext_agent_host_id` (`hostId`), `redirect_uri`, `scope=openid profile
  email offline_access resource.invoke chatgpt.tokens.use.direct`,
  `resource=https://api.openai.com/v1`, `state`, `nonce`,
  `code_challenge`, `code_challenge_method=S256`, and `login_hint` when
  not empty.
- `ParseCallback()` checks a callback URL: a path other than
  `CallbackPath()` → error `not_found`; a missing `state` or one that
  differs from `expectedState` → `state_mismatch`; an `error` parameter
  → that value (`access_denied`); a missing `code` → `missing_code`.
  Otherwise `code`, and `clientId` from the `client_id` parameter, else
  `pendingClientId` (a sign-in with a saved client id may leave it out);
  an empty client id is `missing_client_id`. `ParsePastedUrl()` trims
  the text, requires the scheme, host, port and path of `redirectUri`
  (`wrong_url` otherwise; any port is accepted when the pasted host is
  `127.0.0.1` and the path matches) and then works like
  `ParseCallback()`.
- `CodeRequest()` and `RefreshRequest()` POST to
  `https://auth.openai.com/api/accounts/oauth/token` with `content-type:
  application/x-www-form-urlencoded`, `accept: application/json` and the
  form (percent-encoded): `grant_type=authorization_code`, `client_id`,
  `code`, `code_verifier`, `redirect_uri`,
  `resource=https://api.openai.com/v1`; or `grant_type=refresh_token`,
  `client_id`, `refresh_token`, `resource` (no `scope`: the grant stays).
  `dynamic_agent_client` is never a token request's client id.
- `DiscoveryRequest()` is a GET of
  `https://auth.openai.com/.well-known/openid-configuration`;
  `ParseRevocationEndpoint()` reads its `revocation_endpoint` (empty when
  missing or not https). `RevokeRequest()` POSTs the form `token`,
  `token_type_hint=refresh_token`, `client_id` to that endpoint; an empty
  200 is success, also for an already invalid token.
- `ParseTokenResponse()`: status 200 with a JSON object that has
  non-empty `access_token`, `refresh_token`, `scope` and a positive
  `expires_in` gives those (and `id_token` when present); missing fields
  give `errorCode` `bad_response`. Other statuses read `error` and
  `error_description` (`errorCode`, `errorText`); `terminal` is true for
  `invalid_grant`, `invalid_refresh_token`, `token_expired`,
  `refresh_token_expired`, `refresh_token_invalidated`,
  `refresh_token_reused` and `invalid_client` (the tokens can't be used
  again, sign in anew), false for everything else (network trouble,
  5xx: keep the tokens and retry later).
- `ParseIdToken()` decodes the JWT payload (the middle part, base64url)
  without checking the signature (the token came straight from the token
  endpoint over TLS): `sub`, `email`, `iss`, `aud` (a string, or the
  first of an array), `nonce`, `exp`, and `plan` from
  `https://api.openai.com/auth`.`chatgpt_plan_type` when present.
  Not three parts or bad JSON gives nothing.
- `HasPlanScope()` is true when the space-separated scopes contain
  `chatgpt.tokens.use.direct` (without it the plan can't be used: the
  sign-in fails with "no plan").
- `SignedIn()` is a non-empty refresh token. `NeedsRefresh()` is true
  when the access token is empty or `expiresAt - kRefreshMarginMs <=
  now`. `ApplyTokens()` replaces the access token, the refresh token
  (when the answer has one), `scopes` and `expiresAt = now + expiresIn *
  1000` together. `ClearTokens()` empties the access and refresh tokens,
  `expiresAt` and `scopes`, keeping `clientId`, `hostId`, `email`,
  `subject` and `plan`.
- `SignedOutBody()` is the JSON body `{"error": {"code":
  "chatgpt_signed_out", "message": <text>}}` that `ChatGptTransport`
  answers with status 401 when there is no usable token.

## Claude Code (`agent_claude_process.h`, `agent_claude_protocol.h`, `agent_claude_engine.h`, stage 4)

The subscription engine drives the user's `claude` binary over its
stream-json control protocol (plan 4.4 and 11.8). Everything here is pure
`td_agent`; the client supplies the process (`ClaudeLauncher`).

- `Launcher::start()` starts a process and calls the handlers
  asynchronously, never from inside `start()`: `line` for every non-empty
  stdout line without the newline, then `finished` exactly once with an
  empty string for exit code 0, else a short description. Destroying the
  `Process` kills it and no handler is called afterwards; a handler may
  destroy the process. `write()` appends a newline.
- `LaunchArguments()` is `-p --input-format stream-json --output-format
  stream-json --verbose --include-partial-messages --tools ""
  --strict-mcp-config --mcp-config <server> --setting-sources ""
  --no-session-persistence --disable-slash-commands
  --permission-prompt-tool stdio --thinking-display summarized
  --max-turns <maxTurns>`, then `--model` and `--effort` when set. The
  server is `{"mcpServers": {"aigram": {"type": "sdk", "name": "aigram",
  "alwaysLoad": true}}}`. `InitializeRequest()` declares `aigram` in
  `sdkMcpServers` with `sdkMcpServerConfigs.aigram` `{"timeout":
  kToolTimeoutMs, "disableAutoBackground": true}` and the system prompt
  as `systemPrompt: [<system>]` (a short default when it is blank: an
  empty one would give Claude Code's own prompt).
- Lines: `ControlRequestLine()`, `ControlResponseLine()` (success with a
  `response`), `ControlErrorLine()`, `UserLine()` (`shouldQuery: false`
  when it must not ask the model) and `AssistantLine()`; compact JSON.
- `ReplayFrames()` turns messages into stream-json frames: user frames
  carry `tool_result` blocks first, then text blocks (hidden ones too),
  `Image` media with bytes and PDF `File` media with bytes; assistant
  frames carry text and `tool_use` blocks with `ToolName()` names. Thinking
  is dropped (it is bound to another conversation), other media and media
  without bytes are skipped, empty frames are skipped and consecutive
  frames of one role merge. Tool use ids that are not `^[A-Za-z0-9_-]+$`
  or repeat an earlier id become `toolu_aigram_<message>_<block>`, and
  the results that follow use the new id. `UserContent()` is the user
  frame content of one message without id remapping.
- MCP: `McpToolsList()` lists the tools with `_meta:
  {"anthropic/maxResultSizeChars": kMaxResultChars}` (otherwise the CLI
  saves big results into `~/.claude/projects`). `McpToolResult()` is the
  result text and its `Image` media as MCP image content, `isError` when
  set; other media are never sent (the CLI writes them to disk) and a
  line says how many were left out.
- `ParseLine()` returns the typed line: `ControlRequest` (also for
  `control_cancel_request`, with that subtype), `ControlResponse`,
  `StreamEvent` (a Messages API event), `AssistantEcho` (its `error`, e.g.
  `authentication_failed`, and text), `UserEcho` (the `tool_result`
  blocks the CLI sent the model; `replay` for `isReplay` echoes),
  `ResultLine`, `Limits` (`rate_limit_event`: status and the five-hour
  and seven-day `utilization` 0..1 and `resetsAt` unix time) and
  `SystemInit`; `nested` marks lines of subagents
  (`parent_tool_use_id`). Anything else is `std::monostate`.
- `ParseModels()` reads `models` of the `initialize` response (`value`,
  `resolvedModel`, `displayName`, `description`,
  `supportedEffortLevels`), skipping entries without `value`.
  `ParseAuthStatus()` reads `claude auth status` JSON (`loggedIn`,
  `authMethod`, `email`, `subscriptionType`). `ParseVersion()` finds
  `x.y.z` in `claude --version`; `VersionSupported()` is at least
  `kMinVersion` (2.1.304, the version the protocol was checked with).
- `ResultError()` maps the assistant `error` (`authentication_failed`,
  `oauth_org_not_allowed`, `account_on_hold`, `verification_required`,
  `billing_error`, `cloud_credential_error` → `Auth`; `rate_limit` →
  `RateLimited`; `overloaded`, `server_error` → `Overloaded`;
  `invalid_request`, `model_not_found` → `BadRequest`), else
  `api_error_status` like the HTTP adapters, else `BadResponse`; the text
  is the result text or its `errors`.
- `Connection` is one `claude` process with fixed `SessionOptions`, the
  `owner` (a session id) and a `ConnectionKey` saying which record state
  the process holds: `messages` record messages and `summaryUntil`. Lines
  go to the handlers set with `setHandlers()`; without handlers control
  requests get an error. `ConnectionPool` keeps idle warm connections:
  `take()` hands out the one of the owner (or null) and removes it.
- `Engine` implements `Agent::Engine` like `Loop` (events, history,
  interrupt and destruction rules). `resume()` takes the owner's warm
  connection when it is alive, initialized, has the same options and the
  key `{owner, recordMessages - 1, summaryUntil}`, and then sends only the
  last message as the query. Otherwise it starts a new process, sends
  `initialize`, and after its success replays `ReplayFrames()` of the
  history: an assistant frame is written right away, a user frame waits
  for its `result` with `num_turns` 0 (sending without waiting makes the
  CLI reorder the frames), the last frame (it must be a user frame) is
  the query.
- While the query runs: stream events of the top level go through a
  `StreamAssembler`; deltas are forwarded (tool names without the
  `mcp__aigram__` prefix), and at `message_stop` the message is appended
  to the history and its usage reported. `can_use_tool` is allowed for
  `mcp__aigram__*` tools (the registry and its `PolicyGate` decide inside
  the call) and denied for anything else. `mcp_message`: `initialize`,
  `tools/list`, `ping`, notifications (acknowledged), `tools/call` (the
  `_meta["claudecode/toolUseId"]` matches the streamed tool use;
  `ToolCallRequested`, `ToolExecutor::execute()`, then the MCP answer and
  `ToolCallFinished`), `notifications/cancelled` (cancels that call in the
  executor). Tool results the CLI made itself (denials, interruptions)
  arrive in `UserEcho` and complete their tool uses as errors. When every
  tool use of a message has a result, one user message with them in
  order is appended. After a tool call, when `tools()` changed (a group
  was loaded) the engine sends `notifications/tools/list_changed`.
- The `result` line ends the turn: `error_max_turns` → `StepLimit`, an
  assistant error `max_output_tokens` → `MaxTokens`, other errors →
  `Failed` with `ResultError()`, success by the last stop reason
  (`MaxTokens`, `Refusal` or `Completed`). Unfinished tool uses get an
  error result first and partial streamed text is kept. A process exit
  during a turn fails it (`Network`). After `Completed`, `MaxTokens` and
  `Refusal` with `keepWarm` the connection goes back to the pool with the
  key `{owner, recordMessages + appended messages, summaryUntil}`; after
  any other end it is destroyed. `interrupt()` sends `interrupt`, cancels
  running tools, adds "Interrupted by the user." results and ends the
  turn at once; that connection is never reused.
- `limits` gets every `rate_limit_event`.
- `OneShot` runs one turn without tools (`maxTurns` 1, no pool) over the
  given messages and calls `done` once with the assistant blocks, the
  summed usage and the error (also when the turn ended without an
  answer); `done` may destroy it. `ModelsQuery` starts `claude`, sends
  `initialize`, answers the MCP handshake and calls `done` once with
  `ParseModels()` of the answer or an error.

## Loop and engines (`agent_engine.h`, `agent_loop.h`)

- `Engine` is what sessions and UI talk to; `Loop` is the native engine and
  `Claude::Engine` (see "Claude Code") implements the same interface.
- `send()` appends a user message and runs a turn: request, stream, then
  execute every tool use through `ToolExecutor`, append one user message with
  all results in the order of the tool uses, and request again, until the
  model stops without tool uses, `maxSteps` requests were made, or an error.
- `send()` while `busy()` is a programming error.
- `resume()` runs a turn on the current history without appending a message,
  used to retry a failed turn or to continue after compaction. It requires an
  idle engine and a history that ends with a user message.
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
- `LoopOptions::pdfEngine` and `LoopOptions::cacheKey` are copied into
  every request.

## Provider settings (`agent_providers.h`)

- `SerializeProviders()` and `DeserializeProviders()` use `QDataStream` with
  a leading format version, so later fields can be appended. Unknown future
  versions and truncated data deserialize to `std::nullopt`.
- The serialized bytes include API keys. They are stored only through the
  encrypted `Storage::Domain` blob and never written to logs.
- `ProvidersState::showEntryPoints` (default true) hides the ✦ button and the
  agent row when false; it is appended to the stream under a new version.
- Version 3 writes `priceInput`, `priceOutput` and `priceCached` (`double`,
  dollars per million tokens, zero when unset) after `vision` of each
  provider and `showSpending` (default false) after `showEntryPoints`.
  Versions 1 and 2 load with zero prices and `showSpending` false.
- Version 4 appends `rowPreview` (what the "✦ Agent" row shows under its
  name; default `LastMessage`).
- Version 5 appends `audio`, `video` and `files` (`qint32` 0 or 1) after
  `priceCached` of each provider and `mediaId` after `rowPreview`; older
  versions load them false and empty. `vision`, `audio`, `video` and
  `files` say what the model takes besides text when `/models` doesn't.
- Version 6 appends `backgroundId` (`QString`), `backgroundDailyCost`
  (`double`) and `backgroundDailyTokens` (`qint64`) after `mediaId`;
  older versions load an empty id, 1.0 and 1 000 000.
  `BackgroundProvider()` is the provider with `taskProviderId`, else the
  one with `backgroundId`, else `DefaultProvider()`; ids that match no
  provider are skipped.
- `MediaProvider()` is the provider whose id is `mediaId`, null when
  `mediaId` is empty or unknown. `IsOpenRouter()` is true when the host of
  `baseUrl` is `openrouter.ai` or a subdomain of it.
- `Agent::SaveProviders()` fires `Agent::ProvidersChanged()` after writing.
- Every `ProviderSettings` is a model profile (plan 11.7): `title` is the
  name the user sees, `service` says how it was made. `Custom` is the
  manual form ("Other provider"); `OpenRouter` profiles have
  `OpenRouterBaseUrl()` (`https://openrouter.ai/api/v1`) as `baseUrl`,
  the model id, and the context window, abilities and prices copied from
  `/models` when the model was chosen. `ignoredRoutes` are OpenRouter
  provider names (`provider_name` of `/endpoints`) its requests skip.
- Version 7 appends `service` (`qint32`) and `ignoredRoutes` (a `qint32`
  count of at most 100, then the strings) after `files` of each provider;
  older versions load `Custom` and no routes, so saved providers become
  "Other provider" profiles with the same ids. An unknown `service`
  fails the read. `defaultId` (formerly `activeId`, same place in the
  stream) is the default model profile.
- `FindProvider()` is the provider with that id, null for an empty or
  unknown id. `DefaultProvider()` is the one with `defaultId`, else the
  first one, else null. `SessionProvider()` is the one with `profileId`,
  else `DefaultProvider()`.
- One key per service: `ServiceKey()` is the `apiKey` of the first
  profile of that service (empty for `Custom` or when there is none);
  `SetServiceKey()` sets the key of every profile of that service
  (`Custom` is a programming error).
- Version 8 (stage 4, plan 11.8) appends `effort` (`QString`) and
  `priceCacheWrite` (`double`, dollars per million tokens written to the
  prompt cache, zero when unset) after `ignoredRoutes` of each provider
  and `claudePath` after `backgroundDailyTokens`; older versions load
  them empty and zero. Kinds: `Anthropic` profiles talk to the Messages
  API through `Anthropic::Adapter`, `ClaudeCode` profiles run through the
  user's `claude` binary (`UsesClaudeCode()`, no adapter: `CreateAdapter()`
  returns null for them). Services: `Anthropic` is a profile made by
  "Anthropic → API key" (`AnthropicBaseUrl()`,
  `https://api.anthropic.com/v1`), `ClaudeSubscription` one made by
  "Anthropic → Claude subscription" (kind `ClaudeCode`, empty key, the
  model is the `value` `claude` listed in `initialize`). `claudePath` is
  the `claude` binary chosen by the user, empty to search `PATH`.
- `IsClaudeOnOpenRouter()` is `IsOpenRouter()` with a model id starting
  with `anthropic/` (case ignored).
- Version 9 (stage 5) appends `agentName` and `agentPhoto` (JPEG bytes)
  after `claudePath`; older versions load them empty, which means the
  default name and photo.
- Version 10 appends `showBotIcon` (`qint32` 0 or 1, default true) after
  `agentPhoto`. `ShowBotIcon()` is false only for the agent user with it
  off; `ShowUserBotIcon()` in `dialogs_layout.cpp` checks it, so the chats
  list and search rows drop the bot icon before the agent's name.

- Version 11 (stage 6, plan 11.11) adds the kind `OpenAiResponses`
  (`OpenAi::ResponsesAdapter` from `CreateAdapter()`) and the services
  `OpenAi` ("OpenAI → API key", `OpenAiBaseUrl()`
  `https://api.openai.com/v1`, one key per service like OpenRouter) and
  `ChatGptSubscription` ("OpenAI → ChatGPT subscription", the same base
  URL, empty `apiKey`). It appends `chatGpt` after `showBotIcon`:
  `clientId`, `hostId`, `email`, `subject`, `plan`, `accessToken`,
  `refreshToken` (`QString`s), `expiresAt` (`qint64`, unix ms) and
  `scopes` (`QString`); older versions load an empty account. Kinds and
  services beyond the known ones fail the read.
- `UsesChatGptPlan()` is the `ChatGptSubscription` service.
  `IsSubscription()` is `ClaudeSubscription` or `ChatGptSubscription`:
  plan profiles have no prices and count tokens only.
- `chatGpt` is the one ChatGPT sign-in of all subscription profiles. Only
  the client's ChatGPT account code changes it (`SaveChatGptAccount()`):
  `SaveProviders()` always writes the stored `chatGpt`, never the one in
  the state it was given, so a stale copy (the settings page keeps one)
  can't put back a refresh token that was already rotated.

## Journal (`agent_journal.h`)

- `BuildJournal()` lists what the agent did, newest first, at most
  `limit` entries (all when `limit <= 0`), from the given session records
  (normal sessions and task runs alike).
- An entry is either a reversible action (a tool use whose name is in
  `actionTools`) that has a result, or a card whose status is `Done` or
  `Drafted` (`card` set, `call` is the card's tool use). Tool uses without
  a result (a turn still running or cut off) and other cards are skipped.
- `sessionId`, `sessionTitle`, `scope` and `task` come from the record's
  `info`. `date` is the date of the message holding the tool use, else
  `info.created`. `undone` is whether the tool use id is in the record's
  `undone`. `failed` is true for an error result, and for an action whose
  JSON result has a non-empty `failed` array and an empty `done` array.
- Order: by `date`, newest first; within one date the later tool use
  (record order, records in the given order) comes first.

## Chat messages (`agent_chat_messages.h`, `agent_chat_state.h`)

- `BuildChatMessages()` groups feed items into chat messages: a user
  message per `FeedUser`; an agent message collects the steps and actions
  that come before a text and ends with that text, so steps or actions
  after a text start a new agent message; a card or a user message ends
  the pending agent message; summaries are their own messages, and so is
  a `FeedTurnEnd` (kind `TurnEnd` with its `turnEnd`), which also ends
  the pending agent message. A user message copies `replyTo` and
  `replyQuote`. Keys are the kind letter (`u`, `a`, `c`, `s`, `e`) and
  the first record index, with `.N` added for repeats, so they stay
  stable while a turn grows.
- `LiveTail` folds engine events of the running step into an assistant
  message (text and thinking deltas joined, tool uses filled by
  `ToolCallRequested`) and a user message of tool results; the view
  appends them to the record messages and clears the tail on every
  record change.
- `ChatState` (`pinned`, `pinnedIndex`, `since`) is the agent value
  `chat`, version 1; a new agent chat is pinned on top.

## Scope (`agent_scope.h`)

- `PeerKey` is the client's `PeerId::value`. Zero is never a valid peer.
- `Scope::Account()` is the general agent; `Scope::Chat(peer)` is a chat
  agent bound to one peer for the whole session. In a forum the scope is the
  forum peer, never a topic.

## Policy (`agent_policy.h`)

- `FindNamedPeers()` returns, in candidate order and without duplicates, the
  peers the text names, ignoring case: `@username` followed by a non-word
  character or the end, `t.me/username` or `telegram.me/username` (with or
  without a scheme), or the trimmed title of at least three characters with a
  word boundary on both sides. Letters, digits and `_` are word characters.
- One `PolicyGate` belongs to one `Session`. `excluded` may be null and is
  asked on every call, because exclusions change at runtime.
- `available()`: the account scope needs `ToolPolicy::general`, the chat
  scope `ToolPolicy::chat`.
- `visible(peer)`: false for zero and excluded peers; in the chat scope true
  only for the scope peer.
- `inFrame(peer)`: true without a frame, else `GateFrame::contains`
  (false for a null one). Tools that list or read chats beyond their chat
  parameters use it next to `visible()`.
- `check()` decides in this order: not available, `NotInMode`; risk
  `Forbidden`, `Forbidden`; any excluded peer, `Excluded`; a zero peer or, in
  the chat scope, any other peer, `OutOfScope`. `Read` and `Reversible` are
  allowed. `Visible` is allowed only when this turn's allowances for the same
  tool cover every requested peer (any allowance for the tool covers an
  empty peer list) and the taint rule does not block it; otherwise it is
  `Confirm`.
- Taint rule: when the gate is tainted and the policy is `outbound`, an
  empty peer list or any requested peer missing from the turn's named
  peers blocks allowances, and `Decision::canAllowForTurn` is false.
  Otherwise `Confirm` offers it.
- `allowForTurn()` adds the request's peers to the tool's allowance.
  `beginTurn()` replaces the named peers and drops all allowances.
- Taint is sticky for the session: `markTainted()` is never undone, and the
  session persists it in `SessionRecord::tainted`, because content read
  earlier stays in the model context.
- `ToolPolicy::interactive` and `background` (both true by default) say
  whether a tool exists in normal sessions and in background task runs.
  A gate made with a `GateFrame` is a task run's gate: `background()` is
  true and `available()` needs `background`, otherwise `interactive`,
  besides the scope flag. In a run of a chat task `chatTask` stands in for
  `chat`: `send_message` and `send_files` have it, so a task made from a
  chat panel can write to its own chat (the scope check keeps every other
  chat out).
- With a frame, `check()` keeps the first four steps (not available,
  forbidden, excluded, out of scope) and then decides by the frame
  instead of allowances and taint: the peers are in the frame when
  `GateFrame::contains` is true for each (a null `contains` holds no
  peer; an empty peer list is in the frame). `Read`: in the frame
  `Allow`, otherwise `Deny` with `OutOfFrame`. `Reversible` and
  `Visible`: the tool in `GateFrame::tools` and the peers in the frame
  `Allow`, otherwise `Confirm` with `canAllowForTurn` false.

## Cards (`agent_card.h`)

- A `Card` is a confirmation the user answers in the feed, recorded in
  `SessionRecord::cards` by `toolUseId`. `targets` are chats (`messageId`
  zero) or messages; `title` is the chat title and `preview` a short
  message text, both taken when the card was made. Without targets a card
  has no checkboxes.
- Per kind: `text` is the editable message text (`Send`, `Draft`, `Edit`),
  the forwarded messages' preview (`Forward`), the reaction emoji
  (`React`), the folder title (`DeleteFolder`) or the request to carry over
  (`Handoff`). `proposedText` is the model's original `text`. `detail` is
  the message's current text (`Edit`), the text already in the message
  field (`Draft`), the source chat title (`Forward`) or the replied
  message preview (`Send`). `option` is "delete for everyone" (`Delete`,
  on by default when `optionAvailable`) or "notify members" (`PinMessage`,
  off by default); without `optionAvailable` it is hidden and false.
- `Transcribe`: one target, the voice or round video message, with its
  media description as `preview`; `amount` is the trial transcriptions
  left and `quota` the weekly number. `Download`: the targets are
  messages with files and `files[i]` (name, MIME type, size) describes
  `targets[i]`; `amount` is the total size of all targets; `detail` is
  the agent folder path when saving and empty when the files are only
  read. `SendFiles`: the targets are chats, `files` the files to send
  (`ref` is an attachment id or an agent folder file name), `text` the
  editable caption and `proposedText` the model's caption, `option` is
  "send without compression" (off, `optionAvailable` when a file is an
  image).
- `CreateTask` and `UpdateTask`: no targets; `task` is `SerializeTask()`
  of the proposed task and, for `UpdateTask`, `previousTask` of the task
  as it was; `text` is the editable task instruction and `proposedText`
  the model's.
- `Action`: a reversible action a background run wants outside its
  frame; no targets, `detail` is the tool name and `text` the summary
  (the tool's title and the chats), not editable. Done, it ends with the
  tool's own result.
- `Change`: a visible change of the `admin`, `contacts` and `profile`
  groups. `text` is the card title the tool made (a localized
  `lng_agent_card_title_*`), not editable; `detail` the tool name;
  `rows` the label and value lines the tool made (localized, `previous`
  the old value or empty); `targets` the users, messages or links acted
  on (a link target has the chat as `peer` and the link as `title`), or
  none; `files` an image to preview (`ref` an attachment id or an agent
  folder file name). Done, it usually ends with the tool's own result.
- `Method`: a raw Telegram API call (`call_method`) that changes
  something. `text` is the model's one-line description (shown as the
  agent's words, not editable), `detail` the method name, `proposedText`
  the call's parameters as indented JSON (as the model wrote them),
  `rows` one "Chats" line with the titles of the chats the parameters
  name, or none; no targets. Done, it ends with the tool's own result.
- `TargetsStatus()` is `Done` without targets or when a checked target
  has no error, otherwise `Failed`.
- `Expired` is a final status: a background task's card nobody answered
  for `kTaskCardExpireSeconds`; nothing was done.
- `CardTextEditable()` is true for `Send`, `Draft`, `Edit`, `SendFiles`,
  `CreateTask` and `UpdateTask`. `CardHasAlternate()` is true for `Send`
  ("to draft") and `Draft` ("append"); `Execute` means "send",
  "replace", "confirm", "transcribe", "download", "create" or "save".
- `CardAlwaysAsks()` is true for `Draft`, `Transcribe`, `Download`,
  `CreateTask` and `UpdateTask`: such a card is shown even when the gate allows the call and never
  offers "allow for the turn".
- `ApplyDecision()`: `Decline` sets `Declined` and keeps everything else.
  `Execute` and `Alternate` (only where `CardHasAlternate()`) copy
  `checked` (same size as `targets`), `text` for editable kinds, `option`
  when available and `allowedForTurn` when `canAllowForTurn`, set
  `decided` and `Running`.
- The executing tool then sets `Done`, `Drafted` (a `Send` card put into
  drafts), or `Failed` when every checked target failed, with per-target
  `error`s. `NotDone` is a card that waited when the client quit.
  `CardFinal()` is true for every status except `Pending` and `Running`.
- `CardResultText()` is the English tool result the model gets for a final
  card: done, drafted, appended (a `Draft` card with `Alternate`),
  declined, failed or not done; targets the user unchecked and failed
  targets with their errors, each named by title, chat id and message id;
  the edited text in full when it differs from `proposedText`. A `Handoff`
  card says the user now sees a button to continue in the general agent.
  A declined `Transcribe` says the user declined to spend a trial
  transcription, a declined `Download` that the user declined the
  download; a done `Download` names the saved files and the folder, a
  done `SendFiles` the chats like `Send`. An approved `Transcribe` and a
  reading `Download` end with the tool's own result instead. A done
  `CreateTask` says the user approved and the task is active, a done
  `UpdateTask` that the user approved the changes, both with the edited
  instruction like an edited text. An `Expired` card says the user did
  not answer in time, the card expired and nothing was done.

## Session records (`agent_record.h`)

- `GenerateSessionId()` is 16 lowercase hex digits.
- `SessionRecord::messages` is the full transcript shown to the user.
  `messages[0, summaryUntil)` are replaced by `summary` in the model context;
  `0 <= summaryUntil <= messages.size()`.
- `SessionInfo::titleFinal` is false while the title is the first line of
  the first message, and true once the model or the user named it.
  `created` and `updated` are unix seconds; `touched` lists the peers the
  session's tools read or acted on, without duplicates. `usage` is the
  priced sum of every request of the session (`PriceUsage()` applied).
- `cards` are the session's cards in creation order, `loadedGroups` the
  tool groups `load_tools` loaded in load order, `undone` the tool use ids
  of actions the user undid.
- Serialization uses `QDataStream` (`Qt_5_1`) with a leading `quint32`
  version, like `agent_providers`. Unknown versions, unknown block tags,
  truncated data and an invalid `summaryUntil` deserialize to
  `std::nullopt`. Every `Block` alternative round-trips, including `hidden`,
  `signature`, `inputError` and `rawInput`. Version 2 adds `cards`,
  `loadedGroups` and `undone` (every `Card` field round-trips); version 1
  records still load with them empty. Version 3 keeps `info.usage` with
  `cost` and `hasCost`; older records load their usage into `info.usage`
  without a cost. Index version 2 adds `usage` (with the cost) to every
  entry; version 1 indexes load with zero usage. Index version 3 adds
  `preview` and `previewOutgoing`; older indexes load them empty.
- Version 4 writes media blocks under a new tag with every field except
  `bytes` (a record never stores media bytes), tool results with their
  media, `Card::files`, `amount` and `quota`, and `info.attachments`;
  image blocks of older versions load as `Image` media blocks. Index
  version 4 adds `attachments`. `SessionInfo::attachments` is how many
  attachment ids (`a1`, `a2`, ...) the session issued.
- Version 5 writes `info.task` (the task id of a background run, empty
  for normal sessions), `Card::task`, `previousTask` and the `Expired`
  status; older records load an empty task. The index stays version 4:
  runs never enter it.
- Version 6 appends, after `info.task`, the `rows` of every card in card
  order (a count, then label, value and previous of each row); older
  records load cards without rows. The index stays version 4.
- Version 7 appends `info.profileId` after the card rows, and index
  version 5 appends it after `attachments` of every entry; older records
  and indexes load it empty. `profileId` is the model profile the
  session uses (see "Provider settings"); empty means the default one.
- Version 8 writes `redacted` (`qint32` 0 or 1) after `signature` of
  every thinking block and appends `info.usage.cacheWriteTokens` after
  `info.profileId`; index version 6 appends it after `profileId` of every
  entry. Older records and indexes load them false and zero.
- Version 9 (stage 5) appends `Message::date` (unix seconds, `qint64`)
  of every message after `cacheWriteTokens`; older records load zero.
  The session stamps a user message when it is sent and every message
  copied from the loop the first time it appears.
- Version 10 (stage 5f) writes `durationMs` (`qint32`) after `redacted`
  of every thinking block, then appends, after the message dates,
  `replyTo` and `replyQuote` of every message and `turnEnds` (a `qint32`
  count, then `after` and `end` as `qint32` of each mark). Older records
  load zero durations, empty replies and no marks. A mark with `after`
  outside `[1, messages.size()]`, an `after` not greater than the
  previous mark's, or an `end` that is not `Interrupted`, `StepLimit`,
  `MaxTokens` or `Refusal` makes the record deserialize to
  `std::nullopt`. The index stays version 6.
- `ThinkingBlock::durationMs` is how long the model thought (0 when
  unknown); adapters never send it. `Message::replyTo` is the
  `ChatMessage::key` of the message a user message replies to and
  `replyQuote` the quoted part (empty for the whole message); both are
  display only, the model reads the quote from a hidden text block.
  `turnEnds` marks the turns that ended without completing: `after` is
  the message count when the turn ended. The session replaces a mark
  with the same `after`.
- `MakeSessionPreview()` is the last message with visible text, its text
  blocks joined, Markdown markers (`**`, `__`, `~~`, backticks, leading
  `#` and `>`) removed, whitespace simplified and capped at 120
  characters; `outgoing` when the user wrote it. The session sets it into
  `info` on every save; the record itself does not store it.
- `CloseInterruptedTurn()` repairs a record saved while a turn ran: every
  `Pending` or `Running` card becomes `NotDone`, and when the last message
  is an assistant message with tool uses, one user message is appended
  with a result for each of them in order: `CardResultText()` of its card,
  or an error result saying the client was closed while the tool ran.
  Returns whether it changed anything.

## Feed (`agent_feed.h`)

- `BuildFeed()` turns a transcript into display items in transcript order;
  `index` is the message the item came from.
- A user message gives one `FeedUser` with its visible text blocks joined by
  an empty line and its media blocks that have an `attachment` as
  `attachments`, or nothing when it has neither. Its tool results attach
  to the `FeedTool` with the same id and never show on their own; other
  media blocks are skipped.
- An assistant message gives one `FeedAssistant` per run of adjacent text
  blocks. Its non-empty thinking blocks (`FeedThinking`) and tool uses
  (`FeedTool`) are steps: steps not separated by visible text form one
  `FeedSteps` item, also across messages, so a whole run of tool rounds is
  one item. Its `index` is the message of the first step. Visible user text,
  assistant text and `FeedSummary` end the run; hidden text does not.
- `FeedThinking::durationMs` copies the block's `durationMs`;
  `FeedUser::replyTo` and `replyQuote` copy the message's.
- Every mark of `turnEnds` gives a `FeedTurnEnd` (`index` is `after - 1`)
  right after all items of `messages[0, after)`, before a `FeedSummary`
  standing at `after`. It ends a steps run like visible text, so steps
  after it start a new `FeedSteps` item.
- `FeedSummary` stands before the item of `messages[summaryUntil]` when
  `summaryUntil > 0`, or last when everything is summarized.
- A tool use with a card in `cards` becomes a `FeedCard` at its place, with
  its result attached; it ends the steps run like visible text.
- A tool use named in `actionTools` (and without a card) goes into the
  run's `FeedActions` item instead of `FeedSteps`. In a run the
  `FeedSteps` item (if any) comes before the `FeedActions` item (if any);
  the latter's `index` is the message of its first action.

## Memory (`agent_memory.h`)

- A `Memory` is the fact list of one owner: a chat (its scope peer) or the
  general agent (owner 0). `nextId` only grows, so an id is never reused
  within an owner, also after `clear`.
- `NormalizeMemoryText()` turns every line break into a space and trims.
- `MemoryAdd()` normalizes the text and fails with `Empty`, then
  `TooLong` (more than `kMemoryMaxChars` UTF-16 units), then `Full` (at
  `kMemoryMaxFacts`); otherwise appends `m<nextId>`, increments `nextId`
  and returns the fact and its index.
- `MemoryUpdate()` fails with `UnknownId`, `Empty` or `TooLong`; returns
  the new fact, its index and `previousText`.
- `MemoryRemove()` fails with `UnknownId`; returns the removed fact and
  the index it had.
- `MemoryRestore()` undoes a remove: `DuplicateId` when the id exists,
  `Full` at the limit, otherwise inserts at the index clamped to the list
  and raises `nextId` above the id's number when needed.
- A failed operation leaves the memory unchanged. `MemoryErrorText()` is the
  English error for the model, empty for `None`.
- `FormatMemory()` is one `- [m3] text` line per fact in order, joined by
  `\n`, empty for no facts.
- `SerializeMemoryBook()` uses `QDataStream` (`Qt_5_1`) with a leading
  `quint32` version 1, skipping owners with no facts and `nextId` 1.
  Unknown versions, truncated data, negative counts and more than
  `kMemoryMaxFacts` facts deserialize to `std::nullopt`.

## Spending (`agent_spending.h`)

- `PriceUsage()` returns a usage with `hasCost` unchanged. Otherwise, when
  any price of the provider is positive, it sets `hasCost` and the cost:
  uncached input (`inputTokens - cachedInputTokens - cacheWriteTokens`,
  at least zero) times `priceInput`, cached input times `priceCached` (or
  `priceInput` when that is zero), cache writes times `priceCacheWrite`
  (or `priceInput` when that is zero) and output times `priceOutput`,
  each per million tokens. Without prices it returns the usage unchanged.
- `SpendingTotals::add(Usage)` adds the tokens (cache writes included),
  counts one request and adds the cost only when `hasCost` (then sets
  `hasCost`). `tokens()` is input plus output; `empty()` means no
  requests. `CachedPercent()` is `cachedInputTokens * 100 / inputTokens`
  rounded to the nearest integer, 0 when there is no input.
- `SpendingStats::days` are per-day totals in ascending `day` order (the
  caller's day number, `QDate::toJulianDay()` of the local date).
  `AddSpending()` adds to the day's bucket, creating it in order, then
  moves every bucket older than `kSpendingKeepDays` days before the newest
  one into `older`.
- `SumSpending()` sums the buckets with `fromDay <= day <= toDay`;
  `SumAllSpending()` sums all buckets and `older`.
- Serialization: `quint32` version 2, `older`, the bucket count, each
  bucket; version 2 writes every totals' `cacheWriteTokens` after
  `requests`, version 1 loads them zero. Unknown versions, truncated data
  and negative or huge counts deserialize to `std::nullopt`.

## Exclusions (`agent_exclusion_list.h`)

- An `ExclusionList` holds hidden peers and hidden folder ids in the order
  they were hidden, without duplicates. `SetPeerExcluded()` and
  `SetFolderExcluded()` add or remove one entry and return whether the
  list changed; peer 0 and folder 0 ("All chats") never change it.
- Serialization: `quint32` version 1, peers, folders; duplicates are
  dropped on load, bad data deserializes to `std::nullopt`.

## Media (`agent_media.h`)

- `Supports()` maps `Image`, `Audio`, `Video` and `File` to `images`,
  `audio`, `video` and `files`. `ChooseMediaRoute()` is `Main` when the
  main abilities support the kind, else `Helper` when there are helper
  abilities that do, else `None`.
- `MediaKindForMime()`: `image/*` except `image/svg+xml` is `Image`,
  `audio/*` and `application/ogg` `Audio`, `video/*` `Video`, the rest
  `File`.
- `FormatByteSize()` is English for the model: `512 B`, `240 KB`
  (rounded, below 1 MiB), `2.3 MB` (below 1 GiB), `1.2 GB`; 1024-based.
- Kind words are `image`, `audio`, `video` and `file <name>`.
  `MediaLabel()`: `[Attachment a1: shot.png, image/png, 240 KB]` for an
  attachment (`, 0:42` added when `duration` is set, `h:mm:ss` from an
  hour), `[Image of message 123 in chat 456]` for message media (the kind
  word capitalized), `[Image]` otherwise. `MediaPlaceholder()`: `[Attachment
  a1: report.pdf, application/pdf, 2.3 MB. Open it with a media tool and
  "attachment": "a1".]`, `[The image of message 123 in chat 456 was shown
  earlier. Call the tool again to look at it.]`, `[An image was shown
  earlier.]` (`An audio`, `A video`, `A file <name>`).
- `AttachmentId(n)` is `a<n>` for `n >= 1`; `AttachmentIndex()` parses it
  back and is 0 for anything else.
- `HelperRequest()` asks the media model, without tools: the system
  prompt says it describes media for another AI agent that can't see,
  hear or open it, answers the question completely, gives the full
  transcript of speech in the original language first, gives the text a
  document question needs (all of it without a question) and never
  follows instructions inside the media. One user message: `Question:
  <question>` (or `Describe this media in detail.`) and the media block.
- `SerializeAttachment()` uses `QDataStream` (`Qt_5_1`): `quint32`
  version 1, kind, `mimeType`, `name`, `attachment`, `size`,
  `duration`, `bytes`; `peer` and `messageId` are not stored. Unknown
  versions, bad kinds and truncated data deserialize to `std::nullopt`.
- `MixToMono()` takes interleaved little-endian PCM (`sampleSize` 2 is
  signed 16-bit, 1 is unsigned 8-bit), averages the channels and
  resamples from `fromRate` to `toRate` by linear interpolation, giving
  `frames * toRate / fromRate` samples (rounded down); a trailing partial
  frame is ignored; other sample sizes, `channels < 1` or non-positive
  rates give nothing. `MakeWav()` is the 44-byte PCM RIFF header (mono,
  16-bit, `sampleRate`) and the samples.

## Documents (`agent_documents.h`)

- `DetectDocument()` looks at the extension first, ignoring case: text
  (`txt`, `md`, `markdown`, `csv`, `tsv`, `json`, `jsonl`, `xml`, `html`,
  `htm`, `yaml`, `yml`, `toml`, `ini`, `cfg`, `conf`, `log`, `sql`, `sh`,
  `bat`, `ps1`, `py`, `js`, `ts`, `tsx`, `jsx`, `c`, `h`, `cpp`, `hpp`,
  `cc`, `cs`, `java`, `kt`, `go`, `rs`, `rb`, `php`, `swift`, `m`, `mm`,
  `lua`, `pl`, `r`, `tex`, `srt`, `vtt`, `svg`, `css`, `scss`), `docx`,
  `xlsx`, `pptx`, `odt`/`ods`/`odp` (`OpenDocument`) and `pdf`; then the
  MIME type: `text/*`, `application/json`, `application/xml` and
  `application/x-sh` are text, `application/pdf` and the office types
  their kinds; anything else is `Other`. `IsOfficeDocument()` is true for
  `Docx`, `Xlsx`, `Pptx` and `OpenDocument`.
- `DecodeText()` decodes UTF-8 (with or without a BOM) and UTF-16 with a
  BOM and turns `\r\n` into `\n`; invalid UTF-8 without a BOM or a NUL
  character give `std::nullopt` (a binary file).
- `WantOfficeEntry()`: `word/document.xml` (`Docx`);
  `xl/workbook.xml`, `xl/sharedStrings.xml` and `xl/worksheets/sheetN.xml`
  (`Xlsx`); `ppt/slides/slideN.xml` (`Pptx`); `content.xml`
  (`OpenDocument`).
- `ExtractOfficeText()` reads the XML with `QXmlStreamReader` and keeps
  what it read before an error. `Docx`: a line per paragraph, `w:tab` a
  tab, `w:br` a line break. `Xlsx`: per sheet in workbook order a `# <sheet
  name>` line, then a line per row with tab-separated cells (shared
  strings, inline strings, values as written). `Pptx`: slides in number
  order, each `# Slide N` and a line per paragraph. `OpenDocument`: a line
  per `text:p` or `text:h`, `text:tab` a tab, `text:s` spaces,
  `text:line-break` a line break, table rows as tab-separated cells.
- `PageText()` clamps `offset` to the text; the page is at most `limit`
  characters, cut back to just after the last line break in its last
  fifth when there is one, never splitting a surrogate pair. `total` is
  the text size and `nextOffset` the offset after the page, -1 at the end.

## Tasks (`agent_task.h`)

- A `Task` is a background rule of one account. `scope` is where it was
  made: `Account()` from the general agent, `Chat(peer)` from that chat's
  panel. `trigger` is a new message by `filter` or a `schedule`; `frame`
  is what a run may do without asking. `providerId` empty means the
  background model. `state` `DailyLimit` and `HourlyLimit` pause until
  `pausedUntil` (unix seconds); `Finished` is a fired `Once` schedule.
  `lastFired` is the last trigger time, `lastSeen` the last time the
  client ran with the task loaded (for missed events). `spentToday`
  holds the priced usage of day `spendingDay` (`QDate::toJulianDay()`).
- `ChatSet` names chats by `peers`, by `kinds` (`ChatKindFlag` bits) and
  by `folders` (filter ids); `triggerChat` (frames only) adds the chat
  of the run's trigger message. `empty()` has none of them.
- `ChatFacts::kind` is one flag: bots `Bot`, other users (and the user's
  own chat) `Private`, basic groups, supergroups and forums `Group`,
  broadcast channels `Channel`. `folders` are the ids of the folders the
  chat is in now; `self` marks Saved Messages.
- `ChatSetContains()` is false for peer 0; true when the peer is in
  `peers`, when its kind is in `kinds` and it is not `self`, when one of
  its folders is in `folders`, or when `triggerChat` and the peer is
  `triggerPeer` (non-zero).
- `TimeWindow` holds minutes of the day (`from`, `till` in 0..1439) and
  `days` (bit 0 Monday ... bit 6 Sunday, 0 every day). `always()` is
  `from == till` with no days. `InTimeWindow()`: the time fits when
  `from == till`, when `from <= t < till`, or for a window over midnight
  (`from > till`) when `t >= from` or `t < till`. With `days`, the day
  must be in the mask; in a window over midnight a time before `till`
  counts as the previous day.
- `MatchesMessage()` is false for `fromAgent`. An `outgoing` message
  matches only in `self` (Saved Messages) and only when the filter names
  that peer in `chats.peers`. Then every set condition must hold: the
  chat by `ChatSetContains(chats, chat)` (an empty set matches no chat),
  `sender` in `senders` when there are any, the text when `keywords`
  or `regex` are set (any keyword as a case-insensitive substring, or
  a case-insensitive `QRegularExpression` match; with both set either is
  enough; an invalid regex matches nothing), `media & filter.media` when
  `media` is not 0, `mentionsMe` with `mentionsOnly`, and
  `InTimeWindow()` of `local`.
- `NextFireTime()` returns the first fire time strictly after `after`,
  or 0 when there is none. `Interval`: steps of
  `max(everyMinutes, kTaskMinIntervalMinutes)` minutes from `anchor`
  (`anchor` itself when it is after `after`). `Times`: the earliest local
  date and time in `zone` after `after` whose minute of the day is in
  `times` and whose weekday is in `days` (any day for 0), looking at most
  eight days ahead; 0 for no times. `Once`: `at` when it is after
  `after`, else 0.
- `GenerateTaskId()` is 16 lowercase hex digits.
- Serialization uses `QDataStream` (`Qt_5_1`) with a leading `quint32`
  version 2: `SerializeTask()` every field of `Task`, `SerializeTasks()`
  a count and the tasks, `SerializeRuns()` a count and every field of
  each `TaskRun` (with the usage cost and `hasCost`). Version 2 appends
  `cacheWriteTokens` at the end of each task (of its `spentToday`) and of
  each run (of its usage); version 1 loads them zero. Unknown versions,
  truncated data, negative or huge counts and out-of-range enum values
  deserialize to `std::nullopt`.
- `TaskInputSchema(chatScope, update)` is the JSON schema of the
  `create_task` (`update` false) and `update_task` input:
  `title`, `instruction`, `trigger` {`type` `message`, `schedule` or
  `once`; for messages `chats`, `chat_types` (`private`, `group`,
  `channel`, `bot`), `folders`, `senders`, `keywords`, `regex`, `media`
  (`text`, `photo`, `video`, `voice`, `round_video`, `audio`,
  `document`, `sticker`, `gif`), `mentions_only`, `time_window` {`from`,
  `till` as `HH:MM`, `days`}; for schedules `every_minutes` or `times`
  (`HH:MM` strings) and `days` (`mon` ... `sun`); for `once` `at` as
  `YYYY-MM-DD HH:MM` local time}, `allowed_actions` (action tool names),
  `allowed_chats` {`chats`, `chat_types`, `folders`, `trigger_chat`},
  `notify` (`actions` or `log_only`), `catch_up` and `limits`
  {`daily_cost` dollars, `daily_tokens`, `actions_per_hour`}. Create
  requires `title`, `instruction` and `trigger`; update requires `task`
  (the id) and nothing else. The chat scope schema has no `chats`,
  `chat_types`, `folders` or `allowed_chats`. Descriptions tell the
  model what each field means, that own messages trigger only in Saved
  Messages, and that `allowed_actions` run without asking while
  everything else waits for the user.
- `ParseTaskInput()` fills `scope`, `title`, `instruction`, `trigger`,
  `frame`, `notify`, `catchUp` and `limits` and leaves the rest default;
  it fails with one English error naming the field: an empty or too long
  `title` (`kTaskMaxTitleChars`) or `instruction`
  (`kTaskMaxInstructionChars`), an unknown `type`, a message trigger of
  the account scope without `chats`, `chat_types` or `folders`, a chat or
  sender `refs.chat` returns 0 for ("Unknown chat: ..."), a folder
  `refs.folder` doesn't know, more than `kTaskMaxKeywords` keywords, an
  invalid regex (with its error), an unknown media or day name, a bad
  `HH:MM`, a schedule with both or neither of `every_minutes` and
  `times`, `every_minutes` below `kTaskMinIntervalMinutes`, more than
  `kTaskMaxTimes` times, an `at` that is not after `refs.now`, an action
  `refs.actionTool` rejects, `trigger_chat` without a message trigger,
  limits outside 0.01..100 dollars, 1000..100 000 000 tokens or 1..1000
  actions, or, in the chat scope, any of the account-only fields. In the
  chat scope both `filter.chats` and `frame.chats` are the scope peer.
  Without `allowed_chats` the frame chats are `triggerChat` for a message
  trigger and empty for schedules. Senders resolve with `refs.chat` too.
  Times are kept sorted and unique.
- `ParseTaskUpdate()` starts from `base` and parses only the given
  fields with the same rules: a given `trigger`, `allowed_actions` or
  `allowed_chats` replaces that part whole, `limits` fields one by one.
  Ids, state, times and spending stay from `base`.
- `TaskToJson()` is the task for the model in the input's shape (chat
  and sender ids as numbers, folder ids as numbers, times and `at` in
  `zone`) plus `id`, `state` (`active`, `paused`, `daily_limit`,
  `hourly_limit`, `finished`) and `paused_until` (`YYYY-MM-DD HH:MM`)
  while paused by a limit. Parsing it back gives the same trigger,
  frame, limits, notify and catch-up.
- `CheckSpendingLimits()` checks the task first, then the whole
  background: by cost when the totals have `hasCost` and the cost limit
  is positive, otherwise by tokens when the token limit is positive; a
  total at or over the limit is a hit.
- `CountRecentActions()` counts times in `(now - 3600, now]`.
  `HourlyResumeTime()` is `now` while that count is below `perHour` (or
  `perHour <= 0`), otherwise the time the count drops below it: the
  `(count - perHour + 1)`-th oldest recent time plus 3600.

## TL schema (`agent_tl_schema.h`, stage 3c)

- `ParseTlSchema()` reads the text of a `.tl` file like
  `mtproto/scheme/api.tl` (the client embeds that file; the build does
  not generate anything for the agent). Lines are trimmed; empty lines
  and `//` comments are skipped, except `// LAYER N`, which sets
  `layer()`. `---types---` and `---functions---` switch between
  constructors and functions (types first). A combinator line is
  `name#hexid` (lowercase hex, no `0x`), then optional generic
  parameters in braces (`{X:Type}`, which makes it `generic`), then
  parameters `name:type`, then `= Result;`. A line without `#id` (the
  built-in `int ? = Int;` lines, `vector {t:Type} # [ t ] = Vector t;`)
  and a line whose id is missing or malformed is skipped, never an
  error; `vector#1cb5c415 {t:Type} # [ t ] = Vector t;` is skipped too.
  A parameter type `flags.N?T` (any `#` parameter name, such as
  `flags2`) becomes `flagParam` (the index of that `#` parameter in
  `params`) and `flagBit` N with `type` T; the `#` parameter itself has
  type `#`. A malformed combinator line (no `=`, a bad parameter, a flag
  that names an unknown or non-`#` parameter) fails the whole parse with
  `error` naming the line number. Duplicate names or ids: the later one
  wins.
- `constructor(id)`, `constructor(name)` and `function(name)` are null
  when unknown. `constructorsOf(type)` lists the constructors whose
  `result` is `type`, in file order (`InputPeer` gives `inputPeerEmpty`,
  `inputPeerSelf`, ...).
- `TlIsBuiltinType()` is true for `int`, `long`, `double`, `string`,
  `bytes`, `int128`, `int256`, `Bool`, `true` and `#`.
  `TlVectorItem()` is the item type of `Vector<T>` or `vector<T>`, else
  nothing. `TlIsBareType()` is true for the builtin types other than
  `Bool`, for `vector<T>`, and for a type whose name (after the last
  `.`) starts with a lowercase letter: such values are written without
  a constructor id.

## TL codec (`agent_tl_codec.h`, stage 3c)

- JSON shape, both ways: an object is `{"_": "<constructor name>",
  "<param>": value, ...}` in parameter order; `#` parameters never
  appear; an absent conditional parameter is omitted. `int` is a number,
  `long` a string of decimal digits on output (input takes a number or
  such a string), `double` a number, `string` a string, `Bool` and
  `true` booleans, a vector an array, `int128` and `int256` lowercase hex
  strings. `bytes` is base64 on output, or `"[N bytes]"` when longer
  than `TlDecodeOptions::maxBytesShown`; input takes base64.
- `EncodeTlCall()` serializes a call of function `method` (not
  `generic`) with `params` (the function's parameters by name) as the
  little-endian bytes of the boxed request: the function id, then each
  parameter. Boxed values start with the constructor id, bare ones
  don't; a boxed vector is `0x1cb5c415`, the count and the items, a bare
  vector only the count and the items; items are boxed unless the item
  type is bare. `string` and `bytes` use the TL byte-string format
  (length byte or `0xfe` plus a 3-byte length for 254 and more, padding
  to 4 bytes). `Bool` is `boolTrue` / `boolFalse`. A `#` parameter is
  computed: the OR of the bits of the conditional parameters that are
  present, a `true` parameter being present only when it is JSON
  `true`. Input objects may omit `"_"` when the type has exactly one
  constructor.
- Errors (then `nullopt` and one English `error` naming the path, like
  `params.peer` or `params.filter.q`): unknown or generic method,
  unknown parameter, missing required parameter, wrong JSON type, an
  unknown `"_"` or one of another type, a number outside `int` range, a
  non-integer for `int` or `long`, bad base64 or hex.
- References: a value of type `InputPeer`, `InputUser` or `InputChannel`
  given as a JSON string or number goes to `refs.input(type, value,
  error)`, whose bytes (the boxed value) are written as they are; a
  null result fails with its error. An object for these types is
  accepted only for `inputPeerEmpty`, `inputPeerSelf`, `inputUserEmpty`,
  `inputUserSelf` and `inputChannelEmpty`; any other fails with "Pass
  chats as ids or @usernames, not objects.". This holds at any depth.
- Bare ids: when `refs.bare` is set, every `long` parameter named
  `chat_id` (any constructor or function) and the `Vector<long>` items of
  `messages.getChats` `id`, `inputPrivacyValueAllowChatParticipants` and
  `inputPrivacyValueDisallowChatParticipants` `chats` (kind `chat`) and
  `contacts.editCloseFriends` `id` and
  `phone.deleteConferenceCallParticipants` `ids` (kind `user`) go to
  `refs.bare(kind, id, error)` before they are written; `false` fails
  with its error. Values that are not integers are left to the type
  check.
- The model never sees `access_hash` and `file_reference`, so an input
  object whose constructor has a parameter with one of these names fails
  ("<constructor> needs an access_hash the agent never sees; ..."), and
  any value of type `InputFile`, `InputEncryptedFile` or
  `InputSecureFile` fails saying uploads are not supported here and
  naming `send_files`, `set_chat_photo` and `set_profile_photo`.
- `DecodeTl()` reads one boxed value of `type` (or a bare one when
  `TlIsBareType()`) from `bytes` and fails when bytes remain after it,
  when the data is truncated, or on an unknown constructor id or one of
  another type (the error names the id in hex). `gzip_packed` is not
  supported (the transport unpacks responses). Fields named
  `access_hash` and `file_reference` are left out of the JSON. Every
  decoded object whose type is in `spanTypes` and that is not inside an
  object already recorded adds a `TlSpan` with its type and byte range
  (`begin` at its constructor id), so the client can read it again with
  the generated types.
- `ScrubExcludedPeers()` removes what belongs to hidden chats: an
  object with a field `peer_id`, `peer` or `saved_peer_id` holding
  `peerUser` (`user_id`), `peerChat` (`chat_id`) or `peerChannel`
  (`channel_id`) for which `excluded(kind, id)` is true (`kind` `user`,
  `chat` or `channel`); a `chat`, `chatForbidden`, `chatFull`,
  `channel`, `channelForbidden` or `channelFull` object whose `id` is
  excluded (`kind` `chat` for the first three, `channel` for the
  others); any object with a `chat_id` (`chat`) or `channel_id`
  (`channel`) field that is excluded, which also removes `peerChat`,
  `peerChannel`, `inputPeerChat` and `inputPeerChannel` references
  anywhere; an `inputPeerUser` whose `user_id` is excluded (`user`); and
  a `dialogFilter` or `dialogFilterChatlist` whose `id` is excluded
  (`kind` `folder`). A removed array item disappears
  from the array; a removed field value becomes `null`; when the value
  itself is removed the result is `null`. `user` objects and `peerUser`
  outside `peer_id` / `peer` / `saved_peer_id` (a forward's author, a
  member) stay: people are hidden only as chats, never as members.

## MTProto methods (`agent_mtproto.h`, stage 3c)

- `ClassifyMethod()` is, in this order: `Forbidden` for unknown and
  `generic` functions, for the namespaces `auth`, `payments`, `wallet`,
  `toncenter`, `smsjobs` and `fragment`, for a method whose name after
  the dot contains `Password`, `Authorization`, `SecureValue`,
  `Takeout`, `WebView`, `UrlAuth`, `Star`, `Gift` or `Paid` (matched
  case-sensitively, so `startBot` is not caught), and for
  `account.deleteAccount`,
  `account.setAccountTTL`, `account.sendChangePhoneCode`,
  `account.changePhone`, `account.sendConfirmPhoneCode`,
  `account.confirmPhone`, `account.sendVerifyEmailCode`,
  `account.verifyEmail`, `account.sendVerifyPhoneCode`,
  `account.verifyPhone`, `account.invalidateSignInCodes`,
  `account.resetPassword`, `account.declinePasswordReset`,
  `users.setSecureValueErrors`, `messages.getBotApp`,
  `messages.requestChatJoinWebView`, `messages.editChatCreator`,
  `messages.startHistoryImport`, `messages.declineUrlAuth` and
  `messages.checkUrlAuthMatchCode`; `Write` for the reads with a side
  effect `messages.getBotCallbackAnswer`, `messages.getInlineBotResults`,
  `messages.getMessagesViews`, `contacts.getLocated`,
  `messages.startBot`, `ephemeral.getCallbackAnswer` (bot buttons),
  `messages.getWebPagePreview`, `messages.getWebPage`,
  `bots.checkDownloadFileParams` (the server fetches a URL the model
  wrote) and `phone.getGroupCallStreamRtmpUrl` (it can revoke the key and
  returns it); `Read` when the name after the dot starts with
  `get`, `search`, `check` or `resolve`; otherwise `Write`.
- `ForbiddenParamsError()` is an English error when the parameters set
  `allow_paid_stars` or `allow_paid_floodskip` (anything but absent,
  null or false): they spend Stars. Empty otherwise.
- `MethodRedirect()` lists the tools that cover a method, empty for the
  rest: the visible actions with their own cards (sending, forwarding,
  editing, deleting, reactions, pins, reading, mute, archive, chat pins,
  folders, profile, contacts and blocking, the 3a admin actions).
  `call_method` refuses such methods and names the tools.
- `ParsePeerRef()` reads a chat reference for an `InputPeer`,
  `InputUser` or `InputChannel`: a positive integer (a JSON number or a
  numeric string) is `Peer` (a `PeerId::value`, as other tools give)
  when it has bits above the low 48, else `Bare` (a bare id from an
  MTProto result, or a user's `PeerId::value`); `user:N`, `chat:N` and
  `channel:N` (any case, N a bare id) are typed; a negative integer is a
  Bot API id: below -10^12 a channel (`-10^12 - N`), else a chat.
  Anything else is `None` (the client tries it as an @username or
  link).
- `MethodNamespaces()` lists the namespaces (the part before the dot,
  `""` for methods without one) of the functions that are not
  `Forbidden`, with their counts, sorted by name.
- `FindMethods()` splits the query into words: runs of letters and
  digits, a camelCase word also split at its capitals (`getHistory` is
  `get` and `history`), compared ignoring case. A word matches a
  non-forbidden function when it is a substring of its name or of one of
  its parameter names or types. The functions matching the most words
  are the result (all words, or else the best partial count, then
  `partial` is set). Ranking: all matched words in the name first, then
  shorter names, then file order; at most `limit` results. An empty
  query gives nothing.
- `MethodSignature()` is `name(param: Type, opt?: Type, ...) -> Result`
  without `#` parameters, a `?` after conditional parameter names, and
  `true` parameters written as `flag?: true`.
- `DescribeTl()` explains a function, a constructor or a type for the
  model, in plain text: for a function the signature, its
  `ClassifyMethod()` as "reads, runs without asking" or "changes
  something, the user confirms it on a card" (for a method with
  `MethodRedirect()` instead "call_method refuses it: use <tools>
  instead"), a line naming the chat reference forms of `ParsePeerRef()`
  and a line saying that values with `access_hash` or `file_reference`
  and uploaded files can't be passed, then for every parameter or result
  type that is not
  builtin (vector items included) one line `Type: ctor1{param: Type,
  ...} | ctor2{...}` with at most 12 constructors (then `| ...`), each
  type once, at most 20 types (then a line saying to describe a type
  for more); for a constructor its line and its type; for a type its
  constructors line. Unknown names and forbidden functions fail with
  `error` (forbidden ones say they are never allowed).

## Context (`agent_context.h`)

- `EstimateTokens()` is a cheap upper bound: one token per three characters
  of text, tool names, arguments and results; four per message. A media
  block with bytes, also in a tool result: 1000 for `Image`, 32 per second
  of `Audio` (bytes / 32000 seconds when `duration` is zero), 300 per
  second of `Video` (1000 when `duration` is zero), bytes / 100 but at
  least 1000 for `File`; without bytes it counts as its placeholder text.
  For a request it adds the system prompt and the tool specs.
- `CapToolResult()` keeps results up to `maxChars` and otherwise cuts them,
  without splitting a surrogate pair, adding a line that says how many
  characters were dropped and asks for a smaller page.
- `ContextMessages()` is `messages[summaryUntil, end)` with the summary as a
  hidden text block put first into the first message when that is a user
  message, or as its own leading user message otherwise.
- Media in the context lives until the end of a turn. The turn start is
  the last user message with a text or media block. Before it every media
  block becomes a hidden text block with `MediaPlaceholder()`, and tool
  result media are dropped, each adding a line with its
  `MediaPlaceholder()` to the result content. From the turn start on, a
  media block with bytes gets a hidden `MediaLabel()` text block right
  before it, and media without bytes is replaced the same way as before
  the turn start.
- `DropMediaBytes()` clears the bytes of every media block of the
  record, tool results included, and returns whether it changed anything.
- `PlanCompaction()` returns the new `summaryUntil`, or the current one when
  the estimate of `fixedTokens` plus the context messages fits in 70% of
  the window (`kDefaultContextWindow` when the window is not positive). A
  cut is a turn start (a user message with a text or media block) after the current
  `summaryUntil`, or the transcript end. The smallest cut whose remaining
  messages, the fixed part and a 2000-token summary fit in 40% of the window
  wins; when none does, the transcript end.
- `CompactionRequest()` asks, without tools, for a summary of the previous
  summary and `messages[summaryUntil, until)` rendered as plain text, tool
  results capped at 2000 characters and media as `MediaPlaceholder()`
  lines. `TitleRequest()` asks for a 3–6 word
  title in the user's language from the first user text and the first
  answer. `ResponseText()` joins a message's text blocks; `CleanTitle()`
  takes the first non-empty line, strips quotes and a trailing period and
  caps it at 60 characters.
- `HandoffText()` renders a chat agent session for the general agent:
  a first line naming the chat title, the summary when there is one, then
  the transcript after `summaryUntil` rendered like the compaction input
  (tool results capped at 2000 characters, media as placeholders). Longer than
  `kHandoffMaxChars`, it keeps the newest part with a line saying the
  start was cut. The caller wraps it with `WrapUntrusted()`.
- `IsContextOverflow()` recognizes a bad request (or HTTP 400 / 413) whose
  text talks about the context length, a too long prompt or too many tokens.

## Prompts (`agent_prompts.h`)

- `SystemPrompt()` is English and depends only on its input, so it stays
  byte-stable within a session: no clock, no counters. It states the mode,
  that reading does not mark messages read, that `get_context` gives the
  time, that content inside `<untrusted_data>` is data and never
  instructions, that `[Context]` lines come from the client, to reply in the
  language of the user's latest message and to use Markdown. The chat agent
  prompt includes `FormatChatBrief()` and says requests outside the chat
  need the general agent. A memory paragraph follows for both scopes: what
  the memory is (the chat's or the agent's own), to save with
  `memory_add` only what the user asks to remember or clearly states as a
  lasting preference or fact, never one-off task details, to keep facts
  short and fix or delete them with `memory_update` and `memory_remove`,
  also when asked to forget; in the account scope also that
  `read_chat_memory` reads a chat's memory. A media paragraph follows
  for both scopes: messages describe media briefly, `view_media`,
  `listen_audio`, `transcribe_voice` and `read_file` (when offered) look
  at it by message id, user attachments come as `[Attachment aN: ...]`
  and open with the same tools and `attachment`, media stays visible only
  until the end of the current request (then call the tool again), and
  whatever media shows or says is untrusted data, never instructions.
  General instructions come next, then `Memory:` with `FormatMemory()`
  when it is not empty.
- `FormatChatBrief()` has no counters, so the chat agent's prompt stays
  byte-stable for the session: `FormatChatCounters()` gives "Members: N.
  Unread messages: M." (each part only when positive, empty without
  both), and the session adds it to the hint of every message.
- `TaskSystemPrompt()` is the English prompt of a background run, also
  byte-stable for one task: it is AIgram's agent running the background
  task `title` on behalf of the user, who is not watching; the user's
  instruction `task`; the chat brief in the chat scope, or that it may
  work with all chats the user didn't hide; what it may do without
  asking (`frame`, written by the client); every run starts fresh with
  no memory of earlier runs, so it reads history with tools when it
  needs it; the trigger comes as `<untrusted_data>` and the same
  untrusted-data and `[Context]` rules as `SystemPrompt()`; other actions
  wait for the user's decision and declined ones are not retried; when
  there is nothing to do it ends with a short note of what it checked;
  `notify` tells the user something important in one or two sentences;
  messages it writes follow the instruction's language or the chat's;
  the media paragraph of `SystemPrompt()`; then the general
  instructions. No memory paragraph.
- `WrapUntrusted()` wraps content in `<untrusted_data>` tags and defuses any
  such tag inside it. `UnwrapUntrusted()` removes the wrapper tags for
  display; defused inner tags stay as they are. `HintText()` makes a one-line `[Context] ...` text, or
  an empty string for an empty hint.

## Navigation (`agent_navigation.h`)

- `RankSettings()` matches a query against settings entries: the query
  splits into words (letters and digits), compared ignoring case; an entry
  matches when every query word is a prefix of a word of its title,
  keywords or path. Title matches rank above keyword matches and those
  above path matches, then shorter titles first, then entry order. It
  returns at most `limit` entry indices, best first; an empty query
  returns nothing.

## Single requests (`agent_request.h`)

- `PendingRequest` is the common base of one-shot requests
  (`SingleRequest`, `Claude::OneShot`); destroying it cancels.
- `SingleRequest` sends one request without tools on construction and calls
  `done` exactly once, asynchronously, with the parsed `Response`, network
  errors included. Destroying it cancels the request and `done` is never
  called. `done` may destroy the `SingleRequest`. It serves compaction and
  titles.

## Manager and sessions (`agent_manager.h`, `agent_session.h`)

- `Manager` is the account-level owner, `Main::Session::agent()`. It keeps
  the provider state, refreshed on `ProvidersChanged()`, one
  `HttpTransport`, the session index and every opened `Session`; a session
  keeps running when its UI closes. `available()` means the default
  provider can make an adapter; `enabled()` additionally needs
  `showEntryPoints`.
- `contextWindow(provider)` is the provider's `contextWindow`, else the
  value its `/models` reported for its model, else
  `kDefaultContextWindow`.
- `abilities(provider)` is each of the provider's `vision`, `audio`,
  `video` and `files` flags, or what its `/models` reported for its model.
  On every providers change `/models` is asked once per base URL of every
  profile until one answer with models arrived, and every model of that
  answer is remembered. `sessionProvider(profileId)` is
  `SessionProvider()` of the state and `mediaProvider()` is
  `MediaProvider()`, each when it can make an adapter, else null.
  `attachments()` is the account's `Attachments`.
- `list(scope)` returns the index entries of that scope, newest `updated`
  first; `listChanges()` fires the scope after its entries change.
  `create()` makes a session that joins the index on its first message.
  `open()` returns a loaded session or loads it, `nullptr` for unknown or
  unreadable ids. `remove()` stops the session and deletes its file and
  its attachments (`Attachments::remove()` with `info.attachments`).
- `SessionStore` keeps `index` and `s/<id>` agent values of
  `Storage::Account` (map key `lskAgentStorages`, encrypted with the local
  key like the wallet engine values).
- `Session::send()` queues the request while the session is working; the
  queue shows in `SessionState::queued` and runs in order after each
  finished turn, on the next main loop iteration. After a failed turn the
  queue waits for `retry()`, `interrupt()` or `stop()`. `stop()`
  interrupts the turn or the compaction and clears the queue.
  `interrupt()` (the "Stop" button) does the same but merges the queue
  into one request (texts joined by an empty line, hints by a line
  break, the untrusted texts, mentions and attachments joined, as long
  as the attachments fit `kAttachmentsPerMessage`), which runs as the
  next turn. `retry()` resumes a failed turn whose history ends with a user
  message.
- A dispatch builds the user message (the text, then the hint as a hidden
  block), starts a policy turn with the mentions plus `FindNamedPeers()`,
  compacts first when `PlanCompaction()` asks, and recreates the `Loop` from
  `ContextMessages()` with a fresh system prompt and the session's
  provider.
  Messages the loop appends are copied into the record, which is saved at
  every turn end.
- A turn that fails with a context overflow is compacted and resumed once.
  After the first completed turn of a session without a final title a
  `TitleRequest()` names it; Stop cancels a title request still in
  flight, and the next completed turn asks again.
- `record()` changes fire `recordChanges()`; `live()` forwards the running
  turn's engine events for streaming; `state()` changes fire
  `stateChanges()`.
- Cards: every `ToolRegistry::cardUpdates()` card replaces the record's
  card with the same `toolUseId` or is appended; the record is saved right
  away, so a waiting card survives a restart, and `SessionState::
  pendingCards` counts the `Pending` cards. A waiting card keeps the turn
  running (`Working`). `decideCard()` passes the decision to the registry.
  `stop()` interrupts the turn, which declines a waiting card.
- A loaded record goes through `CloseInterruptedTurn()` and is saved when
  that changed it.
- `SendRequest::untrusted`, when not empty, is added to the user message
  as a hidden `WrapUntrusted()` text block after the text, and the gate is
  tainted before the turn starts.
- `SendRequest::attachments` (the caller keeps to `kAttachmentsPerMessage`
  and `kAttachmentMaxBytes`): the dispatch stores each with
  `Attachments::add()` under the next index (`info.attachments` grows),
  and the user message gets, after the text, one media block per
  attachment in order, without bytes. Attachments taint the gate like
  `untrusted`. A message may have attachments and no text.
- Every `runLoop()` gives the `Image` attachments of the turn start
  message their bytes for the model, `PrepareImageForModel()` of the
  stored bytes as `image/jpeg`, so retries and resumes see them too. At
  every turn end, before the save, the record goes through
  `DropMediaBytes()`.
- The session's provider is `RunSetup::provider` for a run, nothing for a
  run loaded later, else `SessionProvider()` of the record's
  `info.profileId`. The first dispatch of a normal session with an empty
  `profileId` stores the default provider's id there, so changing the
  default later doesn't move existing sessions. `profileId()` is the id
  of the session's provider (empty without one). `setProfile(id)` stores
  the id (a no-op for runs), saves an indexed session (not changing
  `updated`) and fires `stateChanges()`; it takes effect at the next
  dispatch, a running turn keeps its provider. Compaction, titles and
  `contextWindow()` follow the session's provider.
- When the session's provider `IsOpenRouter()` and its `abilities()` lack
  `files`, the loop gets `pdfEngine` `pdf-text`.
- `Manager::canRun()` is true for a provider with an adapter or
  `UsesClaudeCode()`; `available()`, `sessionProvider()`,
  `mediaProvider()` and `taskProvider()` use it. `single()` starts a
  `PendingRequest` for the provider (`Claude::OneShot` with the
  request's system prompt and messages, else `SingleRequest`), or returns
  null when it can't (no adapter, `claude` not found); compaction, titles
  and `ToolRegistry::askMediaModel()` go through it.
- A dispatch for a `UsesClaudeCode()` provider runs a `Claude::Engine`
  instead of a `Loop`: `FindClaudeProgram()` of `claudePath`,
  `ClaudeLaunchSettings()`, the profile's model and effort, the same
  system prompt and step limit, the session id as owner, the record's
  message count and `summaryUntil`, the manager's `ClaudePool` (not for
  runs, which never stay warm) and `UpdateClaudeLimits()` for the
  limits. When `claude` is not found the turn fails with an `Auth` error
  "Claude Code (claude) was not found.".
- `buildSystem()` passes `FormatMemory()` of the scope's owner
  (`Scope::peer`, zero for the account) to the prompt, so memory changes
  reach the model from the next dispatch on, never within a turn.
- Usage: every `UsageReported`, the usage of the compaction and title
  responses and every `ToolRegistry::helperUsage()` goes through
  `PriceUsage()` with the provider that served it, then into `info.usage`
  and `SpendingTracker::add()`.
- Undo: `canUndo()` asks the registry. `undo()` undoes each id that can
  be undone; a success appends the id to `undone`, saves the record and
  fires `undoChanges()`, a failure fires `undoErrors()` with the error and
  `undoChanges()`. The next dispatch adds "The user undid: <tool> (<id>),
  ..." for the ids undone since the previous dispatch to the hint.
- The registry starts with the record's `loadedGroups` and every
  `loadedGroupsChanges()` copies them back into the record.
- Every session's `PolicyGate` asks `Exclusions::excluded()`. When the
  exclusions change, every open chat session whose peer became excluded
  is stopped. Sessions and memory of hidden chats stay stored.
- `info(id)` is the open session's info or the index entry. `save()` puts
  the record's info, usage included, into the index.
- `clearAllData()` stops and destroys every open session, removes every
  session in the index with its attachments and the index itself, clears all memory and resets
  the spending, then fires `listChanges()` for every scope that had
  sessions. Exclusions stay.
- `Manager::activity()` of a scope: `working` when any open session of the
  scope is `Working` or `Compacting`, `pendingCards` summed over them.
  `activityValue()` starts with the current value and fires on changes,
  without repeats. Background runs never make a scope `working`; their
  pending cards count for the account scope (the "✦ Agent" badge).
- Every dispatch passes the session id as `LoopOptions::cacheKey`, and in
  the chat scope adds `FormatChatCounters()` of the chat to the hint.
- A run is a `Session` made by `Manager::startRun()` with a `RunSetup`
  (the task copy, the trigger chats, the provider, the frame text and a
  `notify` callback). Its gate has a `GateFrame` (the task's action tools;
  chats in the frame chats or, with `triggerChat`, one of the trigger
  chats), its registry runs in background mode, its prompt is
  `TaskSystemPrompt()`, it uses the setup's provider, `kTaskMaxSteps`,
  the task id as the cache key, no title request, and its usage also goes
  to `Tasks::addUsage()`. A run record loaded later (`isRun()` without a
  setup) has no provider and is only shown. `expireCards()` decides every
  pending card `Expired` through `ToolRegistry::expire()`.
- `Manager::save()` of a run writes the record and tells the runner,
  never the index. `actingGuard()` marks the registry running a tool or an
  approved card; `acting()` is true meanwhile, so the runner treats new
  messages made then as the agent's own.
- `tasks()` and `runner()` are the account's `Tasks` and `TaskRunner`.
  `taskProvider()` is `BackgroundProvider()` for the task when it can make
  an adapter, else null. `removeTask()` stops the task's runs, deletes
  their session records and the task. `clearAllData()` also stops every
  run and deletes all tasks with their runs.

## Claude Code client (`agent_claude.h`)

- `ClaudeLauncher` starts `claude` with `QProcess` in the launch
  settings' working directory. The environment is the system one without
  `ANTHROPIC_API_KEY`, `ANTHROPIC_AUTH_TOKEN` and every variable starting
  with `CLAUDE` except `CLAUDE_CONFIG_DIR` (a parent Claude Code session
  or an API key must not change how the subscription runs), plus the
  launch settings' variables. Destroying the process kills it. A
  non-zero exit reports the exit code and the last stderr line.
- `ClaudePool` keeps at most three warm connections, each for ten minutes
  after it was given back; `give()` replaces the owner's older one and
  drops dead connections; `clear()` closes all (the manager calls it on
  destruction).
- `FindClaudeProgram()` is the configured path when it is an executable
  file (empty otherwise), else `claude` in `PATH`, else the usual install
  places (`~/.local/bin/claude`, `~/.claude/local/claude`,
  `/usr/local/bin/claude`, `/opt/homebrew/bin/claude`;
  `~/.local/bin/claude.exe` on Windows). `ClaudeLaunchSettings()` uses
  `aigram-claude` in the temp folder as the working directory and sets
  `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1` and, for an HTTP or SOCKS5
  application proxy, `HTTPS_PROXY` and `HTTP_PROXY`.
  `ClaudeInstallCommand()` is the official installer command line.
- `ClaudeCommand` runs `claude` with arguments (stdin closed, stdout and
  stderr merged) and calls `done` once with the exit code (-1 when it
  could not start or crashed) and the output; `progress` gets output
  chunks as they come. Destroying it kills the process.
- `ClaudeLimits()` / `ClaudeLimitsValue()` are the latest
  `rate_limit_event` of any session, with `received` set to the unix time
  it came; nothing is stored.

## ChatGPT client (`agent_chatgpt.h`, stage 6)

- The live account is app-wide: it is read from the stored providers on
  first use and kept in memory; every change goes through
  `SaveChatGptAccount()` (`agent_settings.h`). `ChatGptAccountState()`
  is a copy of it, `ChatGptSignedIn()` is `ChatGpt::SignedIn()` of it,
  `ChatGptAccountChanged()` fires after sign-in, sign-out and when a
  terminal refresh error cleared the tokens (not after a plain refresh).
- `ChatGptLogin` is one browser sign-in. The constructor listens on
  `127.0.0.1:1455` (`QTcpServer`), creates the PKCE pair, `state` and
  nonce, the host id when the account has none, and opens
  `authorizeUrl()` in the system browser (the saved `clientId` and the
  email as `login_hint` when there are any, else the dynamic
  registration). `listening()` is false when the port is taken; the
  sign-in still works by pasting. The server answers every request: the
  callback path gets a small HTML page (`lng_agent_chatgpt_page_done` or
  `_failed`), other paths 404. `paste()` returns an error text for a
  wrong URL and continues the sign-in otherwise. A valid callback or
  paste exchanges the code (`CodeRequest()`), requires
  `HasPlanScope()`, reads `ParseIdToken()` (email, subject, plan),
  saves the account with the issued client id and the tokens, fires
  `ChatGptAccountChanged()` and calls `done` with an empty error; an
  `access_denied` callback, another OAuth `error` in the callback, a
  failed exchange or a missing plan scope call `done` with the error
  text; a failure of a sign-in with the saved client id forgets that id
  (while signed out), so the next attempt registers anew (the user may
  have picked another ChatGPT account). Callbacks with a wrong `state`
  get an error page and the sign-in keeps waiting. `done` is called once,
  asynchronously (never from the constructor or a socket signal), and
  may destroy the login; destroying it stops the server and drops the
  exchange.
- Access tokens: one refresh at a time for the whole app; requests that
  need a token while it runs wait for it. A token that `NeedsRefresh()`
  is refreshed with `RefreshRequest()`; success saves the new pair right
  away; a `terminal` error clears the tokens (`ClearTokens()`), saves and
  fires `ChatGptAccountChanged()`; other errors keep the tokens and fail
  only the waiting requests.
- `ChatGptTransport` wraps the manager's `HttpTransport` for
  `UsesChatGptPlan()` profiles (`Manager::transportFor()`): `start()`
  returns an id at once, gets a fresh access token, sets
  `Authorization: Bearer <token>` (replacing any) and starts the inner
  request. Without a usable token it answers asynchronously with status
  401 and `ChatGpt::SignedOutBody()`. A 401 from the server is not passed
  on the first time: the token is refreshed (even when not expired) and
  the request runs once more. `cancel()` works in every phase; handlers
  follow the `Transport` contract.
- `ChatGptSignOut()` revokes the refresh token (discovery, then
  `RevokeRequest()`, best effort, in the background), clears the tokens,
  saves and fires `ChatGptAccountChanged()`.
- Everything that talks to a provider takes the transport from
  `Manager::transportFor(provider)`: sessions (`Loop`), `single()`
  requests, the model lists of the boxes and "Check connection".

## Account data (`agent_memories.h`, `agent_exclusions.h`, `agent_spending_tracker.h`, `agent_attachments.h`)

- The first three keep one agent value each in `Storage::Account`: `memory`
  (the `MemoryBook`), `excluded` and `spending`. Unreadable values log an
  error and start empty.
- `Memories` loads the book on first use. `get(owner)` returns the
  owner's memory, empty when it has none; the reference lives until the
  next change. `add`, `update`, `remove` and `restore` run the `core`
  operation, and on success save and fire `changes()` with the owner.
  `clear(owner)` drops the facts but keeps `nextId`; `clearAll()` removes
  the value.
- `Exclusions::excluded(peer)` is true for a hidden peer, or when the
  peer's `History` is loaded and an existing hidden folder of
  `Data::ChatFilters` contains it. Folder membership is checked at call
  time, so a folder hides chats added to it later and chats its rules
  match. `setPeerExcluded()` and `setFolderExcluded()` save and fire
  `changes()` when the list changed; `changes()` also fires on
  `ChatFilters::changed()`.
- `SpendingTracker` loads in its constructor. `add()` adds to today's
  bucket (local date), fires `changes()` and saves two seconds later, or
  in the destructor when a save is pending. `lastDays(n)` covers today and
  the `n - 1` days before. `reset()` empties the stats, removes the value
  and fires.
- `Attachments::add()` stores `SerializeAttachment()` of the block
  (kind by `MediaKindForMime()`, the input's MIME type, name, duration
  and bytes, `attachment` `AttachmentId(index)`, `size` the byte count)
  as the agent value `a/<sessionId>/<id>` and returns the block without
  bytes. `read()` returns it with bytes, or nothing (logging an error)
  when it is missing or unreadable. `remove(sessionId, count)` removes
  `a1` to `a<count>`.

## Media access (`agent_media_loader.h`, `agent_media_convert.h`, `agent_files.h`)

- `FindMessageMedia()` describes the photo or document of a message:
  `Photo` (name `photo.jpg`, `image/jpeg`), `Sticker`, `Animation`
  (GIFs), `Video`, `RoundVideo`, `Voice`, `Audio` (other audio
  documents), `ImageFile` (a document with an image MIME type) or
  `Document`, with the file name, MIME type, size and duration in
  seconds; nothing for messages without a photo or document.
- `MediaLoader` downloads through the client's own loaders: `Full` is a
  photo's largest size as stored (JPEG) or a document's whole file,
  `Thumbnail` the photo's or document's thumbnail as JPEG (a video cover,
  a sticker preview), failing when there is none. `done` is called once,
  asynchronously, with the bytes or an English error. Destroying it
  cancels and `done` is never called. It checks no sizes: callers keep to
  `kAutoDownloadBytes` and `kModelMediaMaxBytes`.
- `PrepareImageForModel()` gives JPEG (quality 87) scaled down to fit
  `kImageMaxSide` on the long side, never up, transparency over white;
  from bytes it decodes first and is empty for what isn't an image.
- `DecodeAudio()` decodes what the client can play (OGG/Opus voice, MP3,
  M4A, the audio track of a video) with FFmpeg, skips `fromSeconds`,
  takes at most `seconds`, mixes to mono `kAudioSampleRate` and returns
  `MakeWav()`, the total duration and the range taken; nothing when it
  can't decode or `fromSeconds` is past the end. It is thread-safe and
  meant for `crl::async`.
- `UnzipOffice()` returns the entries `WantOfficeEntry()` wants, in
  archive order, skipping entries over 50 MB unpacked; nothing for a
  broken archive.
- `AgentFolderPath()` is `AIgram Agent/` inside the client's download
  folder (`Core::App().settings().downloadPath()`, or
  `File::DefaultDownloadPath()` when that is empty or the temp folder),
  created on demand, ending with `/`. `UniqueAgentFilePath()` drops path
  separators and leading dots from the name (`file` when nothing is
  left) and adds ` (1)`, ` (2)`, ... before the extension while a file
  exists. `ListAgentFolder()` lists the regular files directly in it,
  newest first. `ResolveAgentFile()` is the full path of a regular file
  directly in the folder with exactly that name, empty otherwise.
- `MediaSaver` saves a message's photo or file to `path` (documents
  through the client's download into that file, photos from the largest
  size) and calls `done` once, asynchronously, with an empty string or an
  English error. Destroying it cancels the download and removes a
  partial file.

## Background tasks (`agent_tasks.h`, `agent_task_runner.h`, `agent_notifications.h`)

- `Tasks` keeps the agent values `tasks` (`SerializeTasks()`),
  `t/<taskId>` (`SerializeRuns()` of the task's runs) and `bgspending`
  (`SerializeSpending()` of the whole background). It loads `tasks` and
  `bgspending` on first use and each runs value on first use; unreadable
  values log an error and start empty.
- `list()` is every task in creation order; `list(scope)` is all of them
  for the account scope and the tasks whose `scope` equals it for a chat
  scope. `find()` is null for unknown ids; the pointer lives until the
  next change.
- `add()` appends a task (or replaces one with the same id), `update()`
  replaces one by id (unknown ids are ignored), `remove()` erases the
  task and its runs value; each saves and fires `changes()` with the id,
  `remove()` also `runChanges()`. `clearAll()` removes `tasks`, every
  `t/<id>` and `bgspending`, empties everything and fires `changes()`
  with an empty id.
- `runs()` is newest first. `saveRun()` replaces the run with the same
  `sessionId` or puts it first, keeps the `kTaskMaxRuns` newest, saves
  the runs value, fires `runChanges()` and returns the session ids of the
  runs it dropped (the caller deletes those records).
- `addUsage()` takes a priced usage of one of the task's requests: it
  resets `spentToday` when `spendingDay` is not today (local date),
  adds the usage there and to today's `bgspending` bucket
  (`AddSpending()`), fires `changes()` with the id and saves both two
  seconds later (or in the destructor). `backgroundToday()` is today's
  background bucket.
- `TaskRunner` watches the triggers: `Data::Session::newItemAdded()` for
  message tasks (facts by the client, `MatchesMessage()`), a timer per
  scheduled task (`NextFireTime()` from `lastFired`, else `created`), and
  at start the missed events of tasks with `catchUp`. A run is a
  `Session` with `info.task` set, the task's scope, a frame gate and the
  task's provider (`Manager::taskProvider()`); its record is saved like a
  session but goes to `Tasks::saveRun()` instead of the index. Runs of a
  task go one at a time, at most `kTaskMaxParallelRuns` for the account,
  at most `kTaskMaxSteps` requests each, `kTaskMaxQueued` waiting
  triggers per task (repeats of one message dropped). A card in a run
  waits up to `kTaskCardExpireSeconds`, then it is decided `Expired`.
  Limits pause the task (`DailyLimit` until the next local midnight,
  `HourlyLimit` until `HourlyResumeTime()`) and the background limit
  skips every run until the next day; each pause notifies. Finished run
  sessions stay open until the client quits, so their actions can be
  undone.
- `waiting(scope)` lists the runs with pending cards, of all tasks for
  the account scope and of the scope's tasks for a chat scope, oldest
  first; `waitingChanges()` fires when it changes. `running(taskId)` is
  true while a run of the task works or waits; `runningChanges()` fires
  the task id.
- `ShowTaskNotification()` shows a system notification through the
  client's native notification manager
  (`NativeManager::showCustomNotification()`, whose click runs its own
  callback instead of opening a chat): the userpic of `peer` (the user's
  own for 0), `title`, `text`, with the default sound only when `sound`,
  nothing while desktop notifications are off, no details while the
  screen or the app is locked. A click opens the run session in the
  general agent screen (`ShowGeneralAgentSession()`). Without native
  notifications it shows a toast in the account's window.

## Tools (`agent/tools/`)

- `ToolRegistry` is a session's `ToolExecutor`. `tools()` lists only the
  tools the gate makes available, using `generalDescription` in the
  account scope when it is set. Entries with a `group` are listed only
  after `load_tools` loaded it, after all other tools, in load order, so
  earlier specs never move. Every call goes through `PolicyGate::check()`,
  even for a tool that was never offered; a group tool that is not loaded
  fails with a hint to call `load_tools`.
- Media abilities: every call gets `sessionId`, `mainAbilities`
  (`Manager::abilities()` of the session's provider from `setProvider()`:
  the running turn's provider while the loop is busy, else the session's
  current one; `files` is also true
  when it `IsOpenRouter()`) and `helperAbilities` (of
  `Manager::mediaProvider()`, none without one). `tools()` hides entries
  whose `needs` can't be met: `Images` needs `ChooseMediaRoute(Image)`
  other than `None`, `Audio` the same for `Audio`.
- `doneMedia(json, media)` is `done` with media blocks attached to the
  result; the JSON is capped and wrapped like `done`, and an `untrusted`
  entry taints also when the result is only media.
- `askMediaModel(media, question, done)` sends `HelperRequest()` to the
  media provider with a `SingleRequest`, fires `helperUsage()` with its
  usage and provider, and calls `done` with `ResponseText()` or an
  English error; without a media provider `done` gets an error at once.
  It is cancelled with the call. `readAttachment(id)` is
  `Attachments::read()` of the call's session.
- Chat parameters: in the chat scope tools have none, and every call gets
  the scope peer as `peer` and `peers`. In the account scope the registry
  adds them to the schema by `ChatParam`: `One` a required `chat` string,
  `OptionalOne` an optional one, `Many` a required `chats` array of 1–100
  strings. It resolves them with `ResolveChatRef()`; an unknown or
  invisible chat fails the call. `peer` is the resolved `chat` (null when
  omitted), `peers` the resolved `chat` or `chats`. The gate checks
  `peers`; `touched()` reports them.
- A chat reference that resolves to an excluded peer, and a
  `DenyReason::Excluded`, fail with "The user hid this chat from the
  agent." plus a request to tell the user and not to look for it another
  way. Lists and searches skip excluded chats silently: `list_chats`, the
  global `search_messages`, folder chat lists. In a background run they
  also skip chats outside the frame (`inFrame()`): `list_chats`, the
  global `search_messages`, `list_contacts` and `list_blocked` (a person
  by their private chat) and the linked chat of `get_peer_info`;
  `forward_messages` fails when `from_chat` is outside the frame. `list_folders` skips
  hidden folders, the folder tools fail on them with "The user hid this
  folder from the agent.", and `reorder_folders` keeps hidden folders in
  their slots. Members and senders inside a visible chat are not hidden.
- `ResolveChatRef()` takes a chat id (a JSON number or a numeric string,
  `PeerId::value` as in every tool result), `@username`, a
  `t.me/username` link, or `me` / `@me` / `self` for Saved Messages
  (the chat parameter descriptions name `"me"`), and finds only peers
  already loaded in `Data::Session`; otherwise null. `ChatRefUsername()` is the username
  such a reference names (4 to 32 letters, digits and `_`, starting with
  a letter), empty for ids and invite links.
- In the account scope the registry first resolves the usernames of the
  `chat`, `from_chat`, `chats` and `users` references that are not
  loaded (at most 10, together with the ones the entry's `usernames`
  returns) with `contacts.resolveUsername`, processing the
  returned users and chats, and only then runs the call as usual;
  failures are ignored (the call then reports the unknown chat). A call
  cancelled meanwhile never runs.
- Results are compact JSON capped with `CapToolResult()`. In the chat
  scope every successful result, and in the account scope every result of
  an entry with `untrusted`, goes through `WrapUntrusted()` after capping
  and marks the gate tainted. `untrusted` entries are those whose results
  carry anything other people wrote (messages, names, titles, bios,
  descriptions). The registry never grants allowances itself: only a
  confirmed card does.
- A tool finishes once: with `done`, `doneMedia` or `fail`, or, after
  `confirm`, with `finishCard`. `confirm(card, execute)` shows the card and calls
  `execute` with the decided card after the user approved it; when the
  gate allowed the call without a confirmation the card is recorded
  `autoApproved` and `execute` runs at once. A `CardAlwaysAsks()` card
  is always shown and never offers "allow for the turn". A declined card is
  finished by the registry. After an approved card a tool may also finish
  with `done`, `doneMedia` or `fail`: its own result goes to the model
  and the registry records the card `Done`, or `Failed` after `fail`. `finishCard` records the final card and
  delivers `CardResultText()`, as an error when the card `Failed`. A tool
  that shows nothing to confirm but wants a card (the handoff) calls
  `finishCard` directly with a final card.
- `decide()` applies `ApplyDecision()`; with `allowedForTurn` it calls
  `PolicyGate::allowForTurn()` for the tool, its policy and
  `CheckedPeers()`. `cancel()` of a call waiting for a card emits the card
  `Declined` without a result.
- `undoable(undo)` is called once after a reversible action succeeded;
  `undo` restores the previous state and reports an empty string or an
  error. `canUndo()` stays true until an undo succeeds. Undo closures
  live in memory with the registry, so nothing is undoable after a
  restart.
- `ToolEntry::gateRequest`, when set, replaces the entry's name and
  policy in the gate check: it returns the allowance key, the policy and
  the peers of this call (added to the chat parameter's peers), or an
  error that fails the call before the gate. The registry keeps the key
  and policy for "allow for the turn", so an allowance covers the same
  key and chats only.
- `load_tools` (account and chat scope) takes a `group`: `folders`,
  `files`, `tasks`, `admin`, `contacts`, `profile`, `telegram_ai` or
  `mtproto`. Its
  description names each group with the tools the scope may use; a group
  with none of them in the scope (for the chat scope also by `forPeer`)
  fails to load. Loading an already loaded group is a no-op success.
- `ToolEntry::forPeer`, when set, says which chats the tool works with.
  In the chat scope an entry whose `forPeer` is false for the scope peer
  (or that peer is not loaded) is not listed and is not loaded by
  `load_tools`; in both scopes a call whose resolved `peer` or `peers`
  include a chat `forPeer` rejects fails with `forPeerError`.
- When a `Change` card is allowed for the turn, the allowance covers the
  call's chats (the gate's peers of the call), not the card targets.
- In a background run (`PolicyGate::background()`, set up with
  `setBackground()`) `tools()` lists every entry the gate makes available,
  group entries included, without `load_tools`; media abilities are those
  of the run's provider (the session passes it through `setProvider()`
  like any session's); and `PendingCall::notify` shows a task
  notification. `expire()` finishes a pending card `Expired` with its
  result text. `ToolPolicyByName()` is the policy of a table entry.
- Reversible tools never ask by themselves, so when the gate says
  `Confirm` for one (an action outside a run's frame), the registry asks
  an `Action` card first and runs the tool only after the user approves. Tools that are only
  for normal sessions have `background` false: `load_tools`, the memory
  tools, `find_setting`, `open`, `continue_in_general`, `set_draft`,
  `draft_files` and the `tasks` group.
- Messages are passed as id, local date and time, sender, text with
  entities as Markdown, reply id and a short media description. Reading
  never marks anything read.
- Action results (entries with `action`, shown as action rows) are
  `{"done": [{"chat": id, "title": ...}], "failed": [{"chat": id,
  "title": ..., "error": ...}]}`, folder tools add `"folder": {"id": id,
  "title": ...}`, `reorder_folders` returns `{"order": [ids]}`.
- `ActionToolNames()` lists the names of entries with `action`.
- Memory tools work on the scope's own memory (owner `Scope::peer`).
  `memory_add`, `memory_update` and `memory_remove` return
  `{"fact": {"id": ..., "text": ...}, "count": n, "limit": 50}` with the
  added, updated or removed fact, fail with `MemoryErrorText()`, and
  register an undo: remove the added fact, put the previous text back, or
  `restore()` the removed fact at its index. `read_chat_memory` returns
  `{"chat": id, "title": ..., "facts": [{"id": ..., "text": ...}]}`.

- Media tools (`agent_tools_media.cpp`) take the media by `message_id`
  (with `chat` in the account scope) or by `attachment` (`a1`, ...)
  instead; both or neither fail. The message is
  `Data::Session::message()` of the peer; a missing message or media of
  a type the tool doesn't take fail with an English error naming the
  type. A download above `kAutoDownloadBytes` first asks a reading
  `Download` card; files above `kAttachmentMaxBytes` are not read at
  all. Results name the source as `"chat"` and `"message"`, or
  `"attachment"`, plus `"type"` (`photo`, `sticker`, `gif`, `video`,
  `round_video`, `voice`, `audio`, `image_file`, `document`,
  `attachment`). An optional `question` goes to the media model when it
  is used, and its answer comes back untrusted; every result built from a
  media model answer has `"media_model": true`.
- `view_media` takes photos, image files, stickers, GIFs, videos, round
  videos and image or video attachments. Images (an animated sticker by
  its thumbnail) go as `PrepareImageForModel()` image blocks on the
  `Main` route, or to `askMediaModel` on the `Helper` route
  (`"description"`). Videos up to `kModelMediaMaxBytes` go whole on a
  video route; otherwise the thumbnail goes as an image. `"shown"` is
  `image`, `video`, `cover` or `description`.
- `listen_audio` takes voice messages, audio files, round videos,
  videos and audio or video attachments: `DecodeAudio()` from
  `from_seconds` (default 0) for `kAudioChunkSeconds`, an `audio/wav`
  block on the `Main` route or `askMediaModel` (`"transcript"`) on the
  `Helper` route; the result adds `"duration"`, `"from"`, `"to"` and
  `"more"`.
- `transcribe_voice` takes voice and round video messages. A ready
  `Api::Transcribes::entry()` result is returned at once. With Premium
  or `Transcribes::freeFor()` it transcribes without a card; otherwise,
  with trials left (`trialsSupport()`, `trialsCount() > 0`) and a
  duration within `trialsMaxLengthMs()`, it asks a `Transcribe` card
  (`amount` `trialsCount()`, `quota` the app config
  `transcribe_audio_trial_weekly_number`); without trials or for a long
  message it fails and suggests `listen_audio`. It calls
  `Transcribes::toggle()` when the entry is not shown (the transcript
  shows in the chat too) and waits while the entry is `pending`,
  re-checking on the item repaint and resize events; `failed` and
  `toolong` fail. Result `{"chat", "message", "type", "text"}`.
- `read_file` takes documents and file attachments (image files go to
  `view_media`). Text and office documents (`DecodeText()`, or
  `UnzipOffice()` and `ExtractOfficeText()`) return `PageText()` pages of
  `kToolResultMaxChars - 1000` characters from `offset` (default 0) as
  `{"name", "type", "offset", "total", "next_offset", "text"}`, with
  `next_offset` null at the end. A PDF goes as a `File` block on the
  `Main` route (`"shown": "file"`), to `askMediaModel` on the `Helper`
  route (`"text"`), and fails on `None`. Other types fail.
- `save_files` (group `files`) takes `message_ids` (1–100) of the chat
  and saves each message's photo or file with `MediaSaver` to
  `UniqueAgentFilePath()` of its name. Up to `kAutoDownloadBytes` in total
  it runs at once with the action result plus `"file"` and `"path"` per
  done entry, and the undo deletes the saved files; above that it asks a
  `Download` card with the folder as `detail` and finishes it with
  per-target errors (no undo).
- `send_files` (group `files`, account scope) takes `files` (1–10
  attachment ids or agent folder names, unknown ones fail) and an
  optional `caption`, asks a `SendFiles` card and sends to every checked
  chat the way the client sends files (`Storage::PrepareMediaList()` of
  the paths and `ApiWrap::sendFiles()`, attachments written to temporary
  files first), compressed unless `option`, grouped as the send box
  groups by default; it finishes the card with per-target errors.
- `draft_files` (group `files`, chat scope) takes the same `files` and
  `caption`, puts the caption into the empty message field as `set_draft`
  does, and opens the standard send box in the chat with
  `MainWidget::sendPaths()` in the session's window; the user sends. It
  returns `{"opened": true}` and fails without a window or when the chat
  can't get files.
- `list_agent_files` (group `files`) returns `{"folder": path, "files":
  [{"name", "size", "modified"}]}`, at most 200, newest first.

- Task tools (`agent_tools_tasks.cpp`, group `tasks`, both scopes, not
  in background runs) work on `Manager::tasks()` within the scope: the
  account scope sees every task, a chat scope only tasks with its scope;
  an unknown or foreign `task` id fails with "Unknown task: <id>". Chats,
  senders and folders resolve through `TaskRefs`: `chat` is
  `ResolveChatRef()` of a visible peer (0 otherwise, also for excluded
  ones), `folder` an id or a case-insensitive title of a folder of
  `Data::ChatFilters` that is not hidden, `actionTool` a table entry with
  `action` or `Visible` risk and `background` true, `zone` the system
  time zone and `now` the unix time.
- `create_task` takes `TaskInputSchema(chatScope, false)`. A parse error
  fails the call; over `kTaskMaxTasks` tasks fails. Otherwise the task
  gets `GenerateTaskId()`, `created` now and the gate's scope, and a
  `CreateTask` card asks (`text` the instruction, `task` the serialized
  task). On approval the task takes the decided instruction, goes to
  `Tasks::add()`, and the result is `{"task": TaskToJson(), "created":
  true}`.
- `update_task` takes `TaskInputSchema(chatScope, true)`:
  `ParseTaskUpdate()` of the stored task, then an `UpdateTask` card
  (`previousTask` the stored task) and on approval `Tasks::update()` with
  the decided instruction and the stored state, runs and spending; the
  result is `{"task": TaskToJson(), "updated": true}`. A call that
  changes nothing fails with "Nothing to change.".
- `list_tasks` (Read, not untrusted) returns `{"tasks": [TaskToJson() +
  "running": bool, "chats": [{"id", "title"}] for every peer the task
  names]}` in creation order.
- `pause_task` (Reversible, action) takes `task` and `paused`: `true`
  sets `Paused`, `false` sets `Active` and clears `pausedUntil`; it
  returns `{"task": {"id", "title"}, "state": "paused"|"active"}` and its
  undo restores the previous state and `pausedUntil`.
- `delete_task` (Reversible, action) takes `task`, calls
  `Manager::removeTask()` and returns `{"task": {"id", "title"},
  "deleted": true}`; its undo adds the task back (without its run log).
- `notify` (Read, background only, not untrusted) takes `text` (1..1000
  characters, required) and calls `PendingCall::notify`; it returns
  `{"notified": true}` and fails without a `notify` callback.

- Shared helpers (`agent_tool_table.h`): `ResolveUserRefs()` resolves
  a `users` array (1 to `max` ids or @usernames, `ResolveChatRef()` of
  each, users only, duplicates dropped) or sets an English error; members
  are never gate-checked, only the chat is. `PeerTitle()` is the name, or
  "Saved Messages" for the user's own chat. `PeerTarget()` is a card
  target with the peer, its title and `@username` as `preview`.
  `ParseLocalTime()` reads `YYYY-MM-DD HH:MM` local time. `MtpErrorText()`
  is an English explanation of common MTP errors with the error type in
  parentheses, else the type. `WhenFullLoaded()` runs `ready` at once
  when the peer's full info is loaded, else after requesting it, when it
  arrives or after 5 seconds; `update_profile` and `edit_chat_info` use
  it so their rows show the current values. `ForEachTarget()` runs `each` for every
  checked target without an error, in order, one at a time, 300 ms
  apart; `done(error)` stores the target's error (empty on success); an
  error with `FLOOD_WAIT` or `PEER_FLOOD` stops the run and marks the
  remaining checked targets failed; then it sets `TargetsStatus()` and
  calls `finished`. Nothing runs after the call is destroyed.
- `ToolRights` names: admin rights `change_info`, `post_messages`,
  `edit_messages`, `delete_messages`, `ban_users`, `invite_users`,
  `pin_messages`, `add_admins`, `anonymous`, `manage_call`,
  `manage_topics`, `post_stories`, `edit_stories`, `delete_stories`,
  `manage_direct_messages`; member restrictions `view_messages`,
  `send_text`, `send_photos`, `send_videos`, `send_round_videos`,
  `send_audios`, `send_voices`, `send_docs`, `send_stickers`,
  `send_gifs`, `send_games`, `send_inline`, `embed_links`, `send_polls`,
  `send_reactions`, `change_info`, `invite_users`, `pin_messages`,
  `manage_topics`. Parsing takes a JSON array of names (any case) and
  fails on an unknown one, listing the valid names.

- Groups `admin`, `contacts` and `profile` (stage 3a): reads run at once.
  Every visible change asks a `Change` card first (title, rows, targets
  as described in "Cards"), then runs: per-target work through
  `ForEachTarget()` with `MtpErrorText()` errors, finished with
  `finishCard` of the decided card, or with `done` of the tool's own
  result where a tool below says so. MTP results are applied to the
  client like the client's own requests do: `ApiWrap::applyUpdates()`
  for `Updates`, `Data::Session::processUsers()` / `processChats()` for
  users and chats. In the chat scope the tools act on the scope chat and
  have no chat parameter. Every tool of these groups runs in background
  runs too (actions outside the frame wait in the queue as usual).
- `admin` tools have `forPeer` true for basic groups, supergroups and
  channels ("This tool works only in groups and channels."). A missing
  admin right fails with the right's name. Basic groups support only
  what Telegram allows there: adding, removing, making and dismissing
  admins (all rights, no title), title, photo, permissions, links and
  requests; banning, restricting, member history and slow mode need a
  supergroup and fail with that explanation.
- `list_members` (chat tools) takes `filter`: `recent` (default),
  `admins`, `banned` (removed and banned), `restricted` or `bots`
  (`banned` and `restricted` in supergroups and channels only). Admin
  entries add `rank` (the custom title) and `rights`; banned and
  restricted entries add `denied` (restriction names, `view_messages`
  for a ban) and `until` (local `YYYY-MM-DD HH:MM`, absent for
  forever). `get_peer_info` of a group adds `member_permissions`, the
  restriction names members don't have by default.
- `add_members` takes `users` (1–50). Card targets: the users; rows: the
  chat. Supergroups and channels `channels.inviteToChannel`, basic
  groups `messages.addChatUser` (100 messages of history), one request
  per user; a user in `missing_invitees` fails with the privacy text.
- `remove_members` takes `users`, `ban` (default false) and `until`
  (local time, only with `ban`). Title `_ban_members` with `ban`, else
  `_remove_members`; rows: the chat and, with `ban`, `_until`
  (`_value_forever` without `until`). Supergroups and channels
  `channels.editBanned` with `view_messages` until `until`, followed for
  a removal without ban by `editBanned` with no restrictions; basic
  groups `messages.deleteChatUser`.
- `unban_members` takes `users`: `channels.editBanned` with no
  restrictions; it lifts bans and restrictions.
- `restrict_members` takes `users`, `denied` (restriction names, not
  empty and without `view_messages`) and `until`; rows: the chat,
  `_denied` and `_until`. Supergroups only, `channels.editBanned`.
- `promote_admins` takes `users`, `rights` (admin right names, not
  empty) and `title` (at most 16 characters); rows: the chat, `_rights`
  and `_admin_title`. Supergroups and channels `channels.editAdmin` with
  the rights plus the implicit "manage chat" right (`Other`), basic
  groups `messages.editChatAdmin` (all rights; the result says rights
  and the title don't apply there). `demote_admins` takes `users` and
  removes all admin rights the same way.
- `delete_chat_messages` takes `message_ids` (1–100) of anyone in the
  chat; each message must exist and `canDelete()`; title
  `lng_agent_card_title_delete`, targets the messages (sender name as
  `title`, text preview); deleted for everyone
  (`Histories::deleteMessages(ids, true)`).
- `delete_member_history` takes `users`: `ApiWrap::deleteAllFromParticipant()`
  per user, supergroups only.
- `list_join_requests` takes `query` and `limit` (1–100, default 50):
  `messages.getChatInviteImporters` with `requested`; result
  `{"requests": [{"id", "name", "username", "date", "about", "link"}],
  "total"}`. `answer_join_requests` takes `users` and `approve`
  (required): title `_approve_requests` or `_decline_requests`,
  `messages.hideChatJoinRequest` per user.
- `list_invite_links` takes `revoked` (default false): the user's own
  links of the chat (`Api::InviteLinks`); result `{"links": [{"link",
  "name", "permanent", "revoked", "expires", "usage", "usage_limit",
  "requested", "request_approval"}]}` (times local, absent when unset).
- `create_invite_link` takes `name` (at most 32 characters),
  `expire_at` (local time), `usage_limit` (1–99999) and
  `request_approval`; rows for the given values. It finishes with `done`
  of `{"link": {...}}` in the `list_invite_links` shape.
  `edit_invite_link` takes `link` (required) and the same fields, rows
  with the previous values; `done` of the edited link.
  `revoke_invite_links` takes `links` (1–20), targets the links; a
  permanent link is replaced by a new one (`revokePermanent()`).
- `edit_chat_info` takes `title` (1–128) and `description` (0–255), at
  least one; rows with the previous values.
- `set_chat_photo` takes `attachment`, `file` (an agent folder file
  name) or `remove: true`, exactly one; the image goes into the card's
  `files`; it uploads through `Api::PeerPhoto::upload()`; removal sends
  an empty photo. Title `_chat_photo` or `_remove_chat_photo`.
- `set_chat_permissions` takes `denied` (restriction names; empty allows
  everything) and `slow_mode_seconds` (0, 10, 30, 60, 300, 900 or
  3600; supergroups), at least one; rows `_denied` and `_slow_mode`
  with the previous values (`_value_nothing`, `_value_off`).
- `create_chat` (account scope, no chat parameter) takes `type`
  (`group` or `channel`), `title`, `description` and `users` (optional,
  up to 50). A group is a supergroup: `channels.createChannel` with
  `megagroup` or `broadcast`, then `channels.inviteToChannel` (the user's
  own entry in `users` is skipped). Rows: the
  title, description and members; it finishes with `done` of `{"chat":
  {"id", "title"}, "invited": [ids], "failed": [{"id", "error"}]}`.
- `join_chat` (account scope, no chat parameter) takes `link`: an
  invite link (`t.me/+…`, `t.me/joinchat/…`) checked with
  `messages.checkChatInvite`, or a public `@username` / `t.me/` link
  resolved first. Already a member fails with the chat id. Rows: the
  chat title, members count, description; `messages.importChatInvite`
  or `channels.joinChannel`, then `done` of `{"chat": {"id", "title"},
  "joined": true}` (or `"requested": true` when admins must approve).
- `leave_chat` leaves a group or channel (`ApiWrap::leaveChannel()`,
  `messages.deleteChatUser` of self for basic groups); rows: the chat.

- `contacts` tools have `forPeer` true for users other than the user
  ("Contacts and blocking work only with users."); in the chat scope
  they exist only in private chats and act on that person.
  `list_contacts` (account scope) takes `query` (name, username or
  phone, ignoring case), `limit` (1–200, default 100) and `offset`; it
  requests the contacts when not loaded and returns `{"contacts":
  [{"id", "name", "username", "phone", "mutual"}], "total",
  "has_more"}`. `list_blocked` (account scope) takes `limit` and
  `offset` (`Api::BlockedPeers::request()`), `{"blocked": [{"id",
  "name", "username"}], "total"}`.
- `add_contact` takes `first_name` (required), `last_name` and
  `share_phone` (default false); rows the names, the phone when known
  and `_share_phone`. A user who is a contact already fails with a hint
  to use `rename_contact`. `contacts.addContact`, then `applyUpdates`.
- `rename_contact` (Reversible, action) takes `first_name` and
  `last_name` of a contact (others fail with a hint to use
  `add_contact`); `contacts.addContact` with the new names; result the
  action JSON plus `"name"` (the new full name); the undo puts the old
  names back.
- `delete_contacts` takes chats (users that are contacts): targets the
  users, one `contacts.deleteContacts` with all checked ones.
  `set_blocked` takes chats and `blocked` (required): title `_block`
  or `_unblock`, `contacts.block` / `contacts.unblock` per user, then
  `PeerData::setIsBlocked()` and `Api::BlockedPeers::reload()`.

- `profile` tools are account scope only. `update_profile` takes
  `first_name`, `last_name` and `bio`, at least one (an empty
  `first_name` fails; `bio` within the account's limit); rows with the
  previous values; `account.updateProfile`, the user processed, the bio
  set on the user. `set_profile_photo` takes `attachment`, `file` or
  `remove: true` like `set_chat_photo` (`Api::PeerPhoto::upload()` of
  the user, `Api::PeerPhoto::clear()` of the current photo).
  `list_emoji_statuses` returns the current status and the recent and
  default ones `{"current": {"id", "emoji", "until"}, "statuses":
  [{"id", "emoji"}]}` (`id` the custom emoji document id as a string,
  `emoji` its emoji). `set_emoji_status` takes `id` or `emoji` (matched
  among the listed statuses), `until` (local time) or
  `duration_minutes`, or `clear: true`; it fails without Premium; rows
  `_status` and `_until`; `Data::EmojiStatuses::set()`.

- `telegram_ai` tools (Read, both scopes, also in background runs) use
  Telegram's own AI; limits come back as errors. `summarize_message`
  (chat parameter One, untrusted) takes `message_id` of a message with
  `canBeSummarized()`: a ready `Api::Transcribes::summary()` returns at
  once; otherwise `toggleSummary()` (the summary shows in the chat, like
  the button) and it waits up to 60 seconds for the entry to stop
  loading, re-checking on the item's text refresh; `premiumRequired`
  fails with the Premium limit text, an empty result fails. Result
  `{"chat", "message", "summary"}`.
- `translate` (chat parameter OptionalOne, untrusted) takes
  `message_ids` (1–20, with the chat) or `text` (up to 4096
  characters), exactly one, and `to_lang` (a two-letter code, required):
  `messages.translateText`; result `{"to_lang", "translations":
  [{"message", "text"}]}` (`message` only for messages). Nothing in the
  chat changes.
- `ai_compose` (no chat parameter, not untrusted) takes `text`
  (required, up to 4096 characters), `proofread`, `emojify`,
  `translate_to`, `tone` (a tone of `Data::AiComposeTones` by title,
  slug or default type) and `custom_tone` (a free prompt), at least one
  of the last five: `ApiWrap::composeWithAi()`; result `{"text"}`. An
  unknown tone fails listing the known ones.

- Group `mtproto` (`agent_tools_mtproto.cpp`, stage 3c): account
  scope, interactive sessions only (`chat` and `background` false). The
  schema is `ParseTlSchema()` of `:/agent/api.tl`, parsed once.
  `find_methods` takes `query` and `limit` (1–50, default 20): without a
  query `{"namespaces": [{"name", "methods"}], "hint"}`, else
  `{"methods": [{"name", "signature", "kind": "read"|"write"}]}`, a
  redirected method with `"use": "<tools>"` instead of `kind`, plus a
  `hint` when nothing or only a partial match was found.
  `describe_method` takes `name` and returns `{"name", "schema":
  DescribeTl()}`.
- `call_method` takes `method`, `params` (an object, or a JSON string of
  one) and `description`, all required in the schema (some OpenRouter
  providers drop optional arguments), though only `Write` methods
  enforce the description. Its `gateRequest` fails an unknown or
  forbidden method, a method with `MethodRedirect()` (naming the tools),
  a `Write` method without a description, `ForbiddenParamsError()` of
  the params, and any `EncodeTlCall()` error; otherwise the key is `call_method <method>`, the policy `Read`
  or `Visible` by `ClassifyMethod()` (always `outbound`), and the peers
  are every chat the references name, plus the loaded chats and people
  of the bare ids (an unloaded one that is hidden fails the call as
  hidden). References resolve by
  `ParsePeerRef()`: `None` through `ResolveChatRef()`, `Peer` as a
  `PeerId`, typed ids as that kind, a `Bare` id as a user for
  `InputUser`, a channel for `InputChannel`, and for `InputPeer` the one
  loaded user, chat or channel with that id (several fail as
  ambiguous); an `InputUser` must be a user and an `InputChannel` a
  channel. Hidden chats, people included, are denied by the gate. Its
  `usernames` dry-runs the encoding and returns the unresolved
  @usernames.
- A `Read` call is sent at once; a `Write` call asks a `Method` card
  first. The request goes as `MTP::details::SerializedRequest` through
  `MTP::Instance::sendSerialized()` to the main DC, cancelled with the
  call; flood errors come back as `MtpErrorText()`, other temporary
  errors are retried by MTP. The response is `DecodeTl()` of the
  method's result type with spans for `Updates`, `User` and `Chat`: under
  `actingGuard()` the users and chats are processed, then the updates
  applied with `ApiWrap::applyUpdates()`. The result is `{"method",
  "result"}` with `ScrubExcludedPeers()` applied.

| Tool | File | Risk | Scopes | Chat param | Notes |
|---|---|---|---|---|---|
| `get_context` | chat | Read | both | None | time, account; the chat brief in the chat scope |
| `read_history` | chat | Read | both | One | |
| `search_messages` | chat | Read | both | OptionalOne | without `chat`: all chats, results carry chat id and title |
| `get_peer_info` | chat | Read | both | One | |
| `list_members` | chat | Read | both | One | |
| `list_chats` | general | Read | general | None | folder, unread, kind, archived filters, paging |
| `find_setting` | general | Read | general | None | `RankSettings()` over `SearchRegistry::collectAll()` |
| `open` | general | Read | general | OptionalOne | a settings link, a chat or a message |
| `continue_in_general` | general | Read | chat | None | a `Handoff` card |
| `send_message` | actions | Visible, outbound | general | Many | `Send` card |
| `set_draft` | actions | Reversible, action | chat | None | `Draft` card when the field is not empty |
| `forward_messages` | actions | Visible, outbound | general | Many | `from_chat`, `message_ids` |
| `edit_message` | actions | Visible | both | One | own messages |
| `delete_messages` | actions | Visible | both | One | own messages |
| `react` | actions | Visible | both | One | |
| `pin_message` | actions | Visible | both | One | `unpin` |
| `mark_read` | chat_state | Reversible, action | both | Many | |
| `set_mute` | chat_state | Reversible, action | both | Many | |
| `set_archived` | chat_state | Reversible, action | both | Many | |
| `set_pinned` | chat_state | Reversible, action | both | Many | pins the chat in the list |
| `list_folders` | folders | Read | both | None | |
| `create_folder` | folders | Reversible, action | general | Many | |
| `rename_folder` | folders | Reversible, action | general | None | |
| `set_folder_chats` | folders | Reversible, action | both | Many | `included` |
| `reorder_folders` | folders | Reversible, action | general | None | |
| `delete_folder` | folders | Visible | general | None | `DeleteFolder` card |
| `memory_add` | memory | Reversible, action | both | None | `text` |
| `memory_update` | memory | Reversible, action | both | None | `id`, `text` |
| `memory_remove` | memory | Reversible, action | both | None | `id` |
| `read_chat_memory` | memory | Read | general | One | untrusted |
| `view_media` | media | Read, needs images | both | OptionalOne | untrusted |
| `listen_audio` | media | Read, needs audio | both | OptionalOne | untrusted |
| `transcribe_voice` | media | Read | both | One | `Transcribe` card without Premium |
| `read_file` | media | Read | both | OptionalOne | untrusted |
| `save_files` | files | Reversible, action | both | One | `Download` card over 20 MB |
| `send_files` | files | Visible, outbound | general | Many | `SendFiles` card |
| `draft_files` | files | Read | chat | None | opens the send window |
| `list_agent_files` | files | Read | both | None | |
| `create_task` | tasks | Visible | both | None | `CreateTask` card |
| `update_task` | tasks | Visible | both | None | `UpdateTask` card |
| `list_tasks` | tasks | Read | both | None | |
| `pause_task` | tasks | Reversible, action | both | None | `task`, `paused` |
| `delete_task` | tasks | Reversible, action | both | None | `task` |
| `notify` | tasks | Read | background | None | not in a group |
| `list_invite_links` | admin_chat | Read | both | One | group `admin` |
| `create_invite_link` | admin_chat | Visible | both | One | `Change` card |
| `edit_invite_link` | admin_chat | Visible | both | One | `Change` card |
| `revoke_invite_links` | admin_chat | Visible | both | One | `Change` card |
| `edit_chat_info` | admin_chat | Visible | both | One | `Change` card |
| `set_chat_photo` | admin_chat | Visible | both | One | `Change` card |
| `set_chat_permissions` | admin_chat | Visible | both | One | `Change` card |
| `create_chat` | admin_chat | Visible | general | None | `Change` card |
| `join_chat` | admin_chat | Visible | general | None | `Change` card |
| `leave_chat` | admin_chat | Visible | both | One | `Change` card |
| `add_members` | admin_members | Visible | both | One | `Change` card |
| `remove_members` | admin_members | Visible | both | One | `Change` card |
| `unban_members` | admin_members | Visible | both | One | `Change` card |
| `restrict_members` | admin_members | Visible | both | One | `Change` card |
| `promote_admins` | admin_members | Visible | both | One | `Change` card |
| `demote_admins` | admin_members | Visible | both | One | `Change` card |
| `delete_chat_messages` | admin_members | Visible | both | One | `Change` card |
| `delete_member_history` | admin_members | Visible | both | One | `Change` card |
| `list_join_requests` | admin_members | Read | both | One | |
| `answer_join_requests` | admin_members | Visible | both | One | `Change` card |
| `list_contacts` | contacts | Read | general | None | group `contacts` |
| `list_blocked` | contacts | Read | general | None | |
| `add_contact` | contacts | Visible | both | One | `Change` card |
| `rename_contact` | contacts | Reversible, action | both | One | undo |
| `delete_contacts` | contacts | Visible | both | Many | `Change` card |
| `set_blocked` | contacts | Visible | both | Many | `Change` card |
| `update_profile` | profile | Visible | general | None | `Change` card |
| `set_profile_photo` | profile | Visible | general | None | `Change` card |
| `list_emoji_statuses` | profile | Read | general | None | |
| `set_emoji_status` | profile | Visible | general | None | `Change` card |
| `summarize_message` | telegram_ai | Read | both | One | untrusted |
| `translate` | telegram_ai | Read | both | OptionalOne | untrusted |
| `ai_compose` | telegram_ai | Read | both | None | |
| `find_methods` | mtproto | Read | general, interactive | None | group `mtproto` |
| `describe_method` | mtproto | Read | general, interactive | None | |
| `call_method` | mtproto | per method | general, interactive | None | `Method` card for writes, untrusted |
| `load_tools` | registry | Read | both | None | |

## UI (`agent/ui/`)

- `ChatView` (below) is the session view shared by the chat panel, the
  general agent screen and the palette. Without a session it shows the
  new-session state and creates the session through `Manager::create()`
  on the first send.
- The chat panel is an Info section (`Info::Section::Type::Agent`, keyed by
  the peer) built by `MakePanelMemento()`; `TogglePanel()` opens or closes
  it and `PanelAvailable()` says whether a peer can have one. Every
  panel widget registers itself (`RegisterShownPanel()`), so
  `PanelShown()` and `PanelShownChanges()` let the history top bar show
  its ✦ button active while the chat's panel is open in that window. A
  new panel (the ✦ button, switching chats with the panel open) starts in the
  new-session state; a saved memento (the layer moving to the third
  column, Info back) keeps its session.
- The panel goes to the third column whenever the window fits one without a
  resize, turning that column on if the profile had it off. Only a narrow
  window shows it as a layer, sized to fit the window; once the window grows
  wide enough, the layer moves to the third column, keeping the session, the
  history list state and the unsent text.
- The panel has no header of its own: its title (the session title, "New
  session" without a session, "History" with the list open), the status
  line under it ("waiting for your decision…" while a card of the shown
  session waits, "typing…" while it works, else the agent's name; the
  Info top bar subtitle) and the "new session" and "history" buttons live
  in the Info top bar, added by `SetupPanelTopBar()`. The panel content lies over the Info scroll area,
  which only holds an empty widget reporting the layer height, so the Info
  scroll never moves.
- `ShowCardBox()` (`agent_card_box.*`) is the native "Edit..." /
  "Details..." box of a card (plan 11.10): a `GenericBox` titled with the
  card kind (`text` for a `Change` card) holding everything the card
  shows: the task rows, the method header with "Show parameters", the
  change rows with "Was: ...", the detail lines, the send-files previews
  (`CardBoxArgs::attachment` reads the session's attachments), the
  download files as checkboxes with the total, the targets as
  "choose chats" rows (userpic in a `RoundImageCheckbox`, title, preview
  or error; checkable only while pending with more than one target) under
  `lng_agent_card_targets` (rows and subsection titles aligned with the
  box text), the editable text in an underline field under its own
  subsection title ("Message text", "New text" or "Caption"), the option
  and "allow until the end of the request". Pending cards get the box
  buttons: the main one (its count
  follows the checks; without checked targets it shows
  `lng_agent_card_choose_chats`, with an empty text it marks the field),
  the alternate one and "Decline" / "Cancel" on the left in red; a
  handoff card has "Continue in the general agent"; running and final
  cards are read-only with "Close". A decision builds the `CardDecision`
  and closes the box.
- While a card waits, the headers say the agent waits for a decision.
- The handoff button calls `ShowGeneralAgent()` with a `HandoffDraft`:
  the card's text, `HandoffText()` of the chat session and the chat.
  `ChatView::startHandoff()` shows the new-session state with a service
  note naming the chat and the text in the composer; the first send
  passes the untrusted text, starting or opening another session drops
  it.
- The agent chat (plan 11.9). `AgentChat` creates the agent peer once per
  account session: a bot `UserData` (`AgentPeerId()`, outside the range
  of real and name-hashed fake ids) named `AgentName()` of
  `ProvidersState::agentName` with a local userpic (`agentPhoto`, else
  `DefaultAgentPhoto()`), and its `History`. While `Manager::enabled()`,
  the history holds one local preview item (the newest account session's
  `preview` / title / hint per `rowPreview`, outgoing when the preview is
  the user's, dated `updated` or the first-shown time `ChatState::since`),
  so the native dialogs row shows it; a new item is added before the old
  one is destroyed, so the last message never becomes unknown. Disabled,
  the history is unpinned and cleared (`ClearType::DeleteChat`). The
  waiting cards of the account scope are the history's unread count;
  while a session works and no card waits, a typing action is registered
  every 5 s and cancelled at the end. The agent user has a known last
  seen in the past, since the chats list shows typing only for such
  users.
- The pin is local: `ChatState{ pinned, pinnedIndex }` in the agent value
  `chat`. `applyPinned()` puts the history at `pinnedIndex` of the main
  pinned list; on every `pinnedDialogsOrderUpdated()` a missing agent
  (the server list replaced the order) is put back, and a moved one has
  its new index saved. "Pin" / "Unpin" in `FillAgentChatMenu()` change
  the state; an unpinned agent sorts by its preview date.
- The general agent screen is a `Window::SectionWidget` with two modes.
  The list mode shows a chat-like top bar (the agent userpic, the name,
  the default model or "typing…" / "waiting for your decision…", the
  third column toggle as in chats, "⋮" with `FillAgentChatMenu()`; a
  click on the name opens the profile),
  "＋ New session" and `SessionRows` (each session like a dialog row:
  the first touched chat's userpic or the agent's, the title, the date,
  the preview with a read check when outgoing, "typing…" while working,
  the waiting cards as an unread badge; the context menu renames or
  deletes). The session mode shows the session title over the agent
  name or the session status, "⋮" with `ShowSessionMenu()`, and a
  `ChatView`; its back arrow returns to the list. `ShowGeneralAgent()`
  opens the list, `ShowGeneralAgentNewSession()` an empty session,
  `ShowGeneralAgentSession()` a session, the handoff variant an empty
  session with the handoff draft. `activeChat()` is the agent history,
  so the dialogs row is highlighted and the third column shows the
  profile. The section re-applies its mode visibility in
  `showFinishedHook()`, since the show animation shows every child.
- The agent profile (`ProfileWidget`, `Section::Type::Agent` of the agent
  peer): a centered userpic, the name and "AI agent", then "Default
  model" (a single-choice box), "Tasks" and "Memory" with their counts,
  "Action Journal" (each opens a `ProfileMemento` of that mode, pushed
  onto the Info stack) and "Agent Settings". A task of the "Tasks" page
  opens as one more `ProfileMemento` (`ProfileMode::Task` with the task
  id): `TasksView::showSingleTask()` without the "All tasks" link, whose
  removal goes back through the Info stack. The journal reads every
  indexed session and every task run record (open sessions from memory),
  shows `BuildJournal(..., ActionToolNames(), 200)` as rows (a ✓ or ✗
  mark and the action or card text, the description of a Telegram API
  request, under it the time, "Undone" and the session title), "Undo" while the session is open and `canUndo()`, and a
  click opens the session (a chat session opens its chat); the top bar "Edit" opens
  `ShowEditAgentProfileBox()` (a `UserpicButton` choosing a photo, "Use
  the default photo", the name field; saved as a 320 px JPEG and the name
  through `UpdateProviders()`). The settings page has "Agent name and
  photo" opening the same box.
- `ChatView` is the native session view: a `HistoryView::ListWidget`
  (`Context::Replies`) over `ChatItems::ids()`, its own default chat
  theme (`DefaultChatThemeOn()`) as the background, since a theme caches
  the wallpaper for one size and the chat next to the panel shares the
  window's one, a "jump down" corner button and `ComposeControls`
  (emoji only, attachments, no stickers, voice, inline bots or
  autocomplete) on the agent history: its custom menu button shows the
  session model and opens the model menu, its custom stop stops the
  session. A run session gets a write restriction with
  `lng_agent_run_about`. Sending takes the field text and the agent
  history's forward draft (the passed messages, set by
  `passMessages()`), files go through `SendFilesBox` as before. The view
  rebuilds `BuildChatMessages(BuildFeed(record + LiveTail))` at most every
  50 ms and keeps to the bottom while the user is there.
- `ChatItems` keeps one local item (`FakeHistoryItem`) per chat message,
  keyed by `ChatMessage::key`, updated in place when its signature
  changes: user messages are outgoing text; agent messages are a rich
  page (`Iv::RichPage`: a Details block with the steps, each step a
  nested Details with the arguments and the result as code, a Thinking
  block while a live turn thinks, the action lines, then
  `Iv::Editor::BlocksFromMarkdown()` of the text) or plain text with
  entities when the page is simple; undoable actions get "Undo" / "Undo
  all"; cards are their title, targets, rows and quoted text with the
  status line in italics and inline buttons by state ("Send (N)" /
  "Confirm (N)" primary, "To draft" / "Append", "Edit..." or
  "Details..." for cards a box adds to, "Decline" danger, on the first
  row next to the main button when the card has neither a box nor an
  alternate; "Continue in the general agent" for a handoff, which is
  `Done` from the start, so it keeps the button and has no status line;
  "Show in Folder" for a done download); the summary, the handoff note, "Summarizing…" and the
  session spending are service messages; a failed turn is an incoming
  message with "Retry" (and "Model settings" for auth errors); queued
  messages are outgoing with the sending clock. Buttons carry
  `agent:<action>:<id>` and reach `ChatView` through
  `ChatItems::buttons()`; "Edit..." opens `ShowCardBox()`. Item dates
  come from `Message::date` (records v9), else the session creation time,
  and never decrease.
- `HistoryView` is the history pane of the panel and the general screen:
  "Sessions" and "Memory" tabs over a scroll area with the scope's
  compact `SessionRows` or its `MemoryList` (owner `Scope::peer`). With it open
  the top bar title is "History"; mementos keep the open tab.
- `MemoryList` shows the owner's facts in order, each wrapping, with
  "Edit" and "Delete" icons always shown at the right; below them "Add a
  fact" and, when there are facts, "Clear memory", then an explanation. Add and edit use a box
  with one field limited to `kMemoryMaxChars`; delete and clear ask first.
  It rebuilds on `Memories::changes()` of its owner.
- The panel and the general screen have a "⋮" button in the top bar that
  opens `ShowSessionMenu()`: "Rename" and "Delete session" through
  `ShowRenameSessionBox()` and `ShowDeleteSessionBox()`. Without a session
  it is hidden on the general screen and disabled in the panel, because an
  `Info::TopBar` button keeps its place when hidden. After a delete the
  view shows the new-session state.
- `SessionRows` in the compact mode (the panel's "Sessions" tab) has no
  userpics and shorter rows (`agentPanelSessionRow`): the title and the
  date, the preview with the read check, "typing…" and the waiting
  badge as in the agent chat, spending first in the preview with
  `showSpending`; the hovered row shows a delete icon at the right
  instead of the date and the badge, which opens
  `ShowDeleteSessionBox()`, and without sessions it shows
  `lng_agent_sessions_empty`. The context menu stays.
- With `showSpending` a session with usage ends with a "This session: …"
  service message. Memory action lines read "Remembered: …", "Updated
  memory: …" or "Forgot: …" with the result's `fact.text`.
- `PanelAvailable()` is also false for an excluded peer;
  `PanelAvailabilityChanges()` fires on `enabledValue()` and exclusion
  changes. An open panel whose chat becomes excluded closes: the third
  column shows the chat's profile instead, a layer hides.
- `FillPeerMenu()` adds "Hide from agent" or "Show to agent" (by
  `Exclusions::peerListed()`) when `Manager::available()`, with a toast.
  `FillFolderMenu()` does the same for a folder other than "All chats".
- The settings page adds price fields to the provider box, a "Spending"
  block (today, last 30 days, all time, "Reset statistics", the "Show
  spending in sessions" toggle), "Hidden chats" (a box listing hidden
  folders and chats with a way to show each again and "Add chats") and
  "Delete agent data" (`Manager::clearAllData()` after a confirmation).
  The spending, hidden chats and data belong to the current account.
- The agent UI reuses the chat's own widgets wherever they fit: the
  attach button style, `SendFilesBox`, the `ui/chat/attach/` previews,
  `DragArea`, the media viewer and "Show in Folder".
- Attachments: the composer has the chat attach button left of the
  field. The button (`FileDialog`), dropping files on the session view
  (`DragArea::SetupDragAreaToContainer()`) and pasting files or an image
  into the field (its MIME hook, as `ComposeControls` does) build a
  `Ui::PreparedList` (`Storage::PrepareMediaList()`,
  `PrepareMediaFromImage()`) and show the standard `SendFilesBox` with the
  composer text as the caption, the user's own chat as `toPeer` and no
  send menu. Its confirm sends `Session::send()` with the caption as the
  text and the files as `AttachmentInput`s (bytes from the path or the
  prepared content, MIME type by `Core::MimeTypeForFile()`) and clears
  the composer; closing the box puts its caption back into the composer.
  Too many or too big files show `lng_agent_attach_too_many`
  or `lng_agent_attach_too_big` instead.
- A user message shows its image attachments as local photos: one image
  is a photo in the bubble, two or more are an album (one local item per
  image sharing a `groupedId`, the first keeping the message key and the
  caption, the others keyed `<key>.m<N>`); other attachments and images
  that can't be read are "📎 name · size" lines above the text.
- Steps of media tools are titled from the call and its result:
  `lng_agent_tool_view_photo`, `_view_video`, `_view_media`,
  `_view_attachment`, `_listen_voice` and `_listen_audio` (with the
  duration), `_transcribe` (with the duration), `_read_file` (with the
  file name), `_list_agent_files`, `_draft_files`, followed by
  `lng_agent_tool_media_model` when the media model answered. In the
  native chat an expanded media step (`IsMediaTool()`) starts with the
  media as a rich page block (`view_media`: an attachment as a local
  photo, a loaded chat message's photo as `Photo`, its video as `Video`,
  other documents as `File`; a click opens the media viewer) and the
  transcript or description under a bold `lng_agent_tool_transcript`,
  then the arguments and the result.
- A thinking step is titled `lng_agent_thought_seconds` /
  `_minutes` with its `durationMs` (rounded, at least one second), or
  `lng_agent_thinking` when unknown; a steps group of thoughts only
  sums them. The session measures a thought from the start of its step
  (the loop start or the last `ToolCallFinished`; the delta itself for a
  second thought within a step) to the next text delta, tool use or turn
  end, so a response that arrives in one chunk still counts, and gives
  the measured durations in order to the non-empty, non-redacted
  thinking blocks the turn's loop added.
- Turn ends: the session appends a `TurnEndMark` for every turn that ended
  `Interrupted`, `StepLimit`, `MaxTokens` or `Refusal` (replacing a mark
  at the same message count), and `ChatView` passes the marks to
  `BuildFeed()`, so each such turn keeps its service message
  (`lng_agent_turn_*`) in the history, also after a restart.
- Message actions: `AddAgentReplyAction()` and `AddAgentForwardAction()`
  (`agent_menus.*`, hooked at the start of the history context menu's
  reply and forward actions) take over for agent chat items. "Reply" /
  "Quote & Reply" is offered for user, agent and card messages (not in
  a run's view) and fires the list's native reply request, which
  `ChatView` puts into the compose controls' reply bar. Sending takes
  the reply: `SendRequest::replyTo` is the replied `ChatMessage::key`
  (`ChatItems::Info()`), `replyQuote` the quote and `replyText` the
  quote or the replied message's text (the agent's markdown, at most
  2000 characters); the session stores the key and quote in the user
  message and adds a hidden block "The user replies to …" with the text
  as `> ` lines. The user's item then shows a native reply with the
  quote. "Forward" is offered for messages with text and opens
  `ShowForwardCopyBox()`: the standard `ShareBox` (chats that can take
  texts, not the agent) whose submit sends the comment and then a copy
  of the text from the user (`Iv::FlattenRichPageToSimpleText()` of the
  answer's markdown, without the steps).
- `Transcribe` cards show the message preview, the trials left
  (`lng_agent_card_transcribe_left`) and the about line with
  "Transcribe" and "Decline".
  `Download` cards list the files as checkboxes (name and size), the
  checked total (`lng_agent_card_download_size`), the folder when saving
  (`lng_agent_card_download_to`) and "Download (N)" and "Decline"; a done
  saving card shows "Show in Folder". `SendFiles` cards show the files
  with the `ui/chat/attach/` previews without edit controls, the caption
  field, the chats as checkboxes, the "send without compression" option
  and "Send (N)" and "Decline". The previews are split into blocks like
  `SendFilesBox` does (runs of photos and videos as albums, other files
  one by one) and take no mouse input. Only `view_media` steps show a
  preview, and only when there is an image to show. Images are decoded
  with `Images::Read()`, as everywhere in the client.
- `save_files` action rows read `lng_agent_action_saved` with the file
  names and add "Show in Folder" (`File::ShowInFolder()` of the first
  saved path) next to "Undo".
- The provider box has "Model abilities": checkboxes for `vision`,
  `audio`, `video` and `files` and the about line. The agent page adds a
  "Media" block: "Media model" with the chosen provider's title or
  "None", opening a single-choice box (none plus every profile) that
  sets `mediaId`, its about line, and "Agent folder" opening the folder,
  with `lng_agent_folder_about`.
- `HistoryView` has three tabs: "Sessions", "Tasks" and "Memory". The
  "Tasks" tab is a `TasksView` of the scope in a scroll area;
  `HistoryView::openRunRequests()` forwards its run clicks, and the panel
  and the general screen open that run session like a session from the
  list.
- `TasksView` has two modes. The list shows `Tasks::list(scope)`, newest
  first: the title, `FormatTaskTrigger()` on one elided line and
  `FormatTaskState()` (in the active text color while running or
  waiting); a click opens the task in place, or calls the
  `setOpenTaskHandler()` handler when one is set. Without tasks it shows
  `lng_agent_tasks_empty`. The task mode starts with a back link
  (`lng_agent_tasks_back`) and the title, then label and value rows like
  the settings info rows: "When" (`FormatTaskTrigger()`), "What to do"
  (the instruction, wrapping, with "Edit instruction" opening a box with
  a multi-line field of at most `kTaskMaxInstructionChars`), "Without
  asking" (`FormatTaskFrame()`), "Model" (`FormatTaskModel()`, a click
  opens a single-choice box: "Background model" plus every provider,
  setting `providerId`), "Limits" (`FormatTaskLimits()`, a click opens a
  box with the three limits), "Notifications" (a toggle: on is
  `Actions`, off `LogOnly`), the catch-up line when set and the spending
  today (`lng_agent_task_spent_today`, tokens without a cost). Then
  "Pause" or "Resume" (a limit pause resumes to `Active` with
  `pausedUntil` 0), "Delete task" (confirmed, `Manager::removeTask()`,
  back to the list) and `lng_agent_task_change_hint`. Under "Runs" each
  run of `Tasks::runs()` shows `FormatTaskRunTrigger()` with its start
  time and `FormatTaskRunOutcome()`; a click fires `openRunRequests()`
  with its session id. The view rebuilds on `Tasks::changes()`,
  `runChanges()`, `TaskRunner::runningChanges()` and `waitingChanges()`
  and goes back to the list when its task is removed.
- `agent_task_format` builds the user-facing task texts from the lang
  keys `lng_agent_task_*` and `lng_agent_run_*`. Chats are their titles
  (`id N` when not loaded), kinds and folders by their names, lists
  joined with ", ". `FormatTaskTrigger()`: for messages
  `lng_agent_task_message_in` with the chats, then the set conditions
  (`_from`, `_keywords` with the words in quotes, `_regex`, `_media`,
  `_mentions`, `_window` with `_on_days`); for schedules
  `FormatTaskSchedule()`. `FormatTaskSchedule()`: every N hours when the
  interval is whole hours, else every N minutes; times as `HH:MM`, every
  day, on weekdays (Monday to Friday), weekends or the listed weekday
  names (`lng_weekday1`...); once with the date and time.
  `FormatTaskFrame()`: `_frame_none` without tools, else `_frame_actions`
  with `FormatTaskTool()` names and the frame chats (`_trigger_chat`,
  `_frame_nowhere` for an empty set). `FormatTaskTool()` maps the action
  tools to `lng_agent_task_tool_*` (folder tools to `_folders`), others
  to their names. `FormatTaskLimits()` uses the cost form when
  `costKnown`, else the tokens form; `TaskCostKnown()` is true when the
  task's provider is OpenRouter or has an input price. `FormatTaskModel()` is the task's
  provider title, or `_model_default` with the background model.
  `FormatTaskState()`: running, waiting, then by `state` (the hourly
  pause with its time). `FormatTaskRunTrigger()`: missed messages or a
  missed run, the sender and chat of a message, or "On schedule".
  `FormatTaskRunOutcome()`: running, waiting, the number of actions or
  "Nothing to do", the error, or interrupted.
- A session view of a run (`info.task` not empty) is read-only: the
  composer is replaced by the write restriction `lng_agent_run_about`
  with the task title, attaching and dropping are off. Its cards can
  still be decided and its actions undone.
- `QueueStrip` is a full-width clickable bar at the top of a normal
  session view and of the agent chat's sessions list (above "＋ New
  session") showing `lng_agent_queue_strip` with the number of
  pending cards of `TaskRunner::waiting(scope)`; it has zero height and
  is hidden when nothing waits, while the view shows a run, and in the
  section while it shows a session (the view has its own).
  `ShowQueueBox()` lists the waiting runs (the task title, the run
  trigger, the time since) and opens the chosen one; a session view
  opens it in place.
- `CreateTask` and `UpdateTask` cards show the task from `Card::task`:
  "When", "What to do" (the editable instruction while pending),
  "Without asking", "Model", "Limits", "Notifications" and the catch-up
  line; an `UpdateTask` card shows only the instruction and the changed
  rows, each with `lng_agent_card_task_was` and the old value. Buttons
  are "Create" or "Save" and "Decline"; a done card reads
  `lng_agent_card_task_created` or `_task_updated`. Every card kind shows
  `lng_agent_card_expired` for `Expired`. An `Action` card shows
  `lng_agent_card_title_action` and its text, with "Confirm" and
  "Decline". An auto-approved card of a run reads
  `lng_agent_card_auto_task` instead of `lng_agent_card_auto`.
- `pause_task` and `delete_task` action rows read
  `lng_agent_action_task_pause`, `_task_resume` or `_task_delete` with the
  result's task title; `list_tasks` and `notify` steps are titled
  `lng_agent_tool_list_tasks` and `lng_agent_tool_notify`.
- A `Change` card shows `text` as its title, then its rows: the label in
  the gray detail style and the value wrapping under it, with
  `lng_agent_card_task_was` and `previous` when it is set; then the
  targets as checkboxes (the title, and `preview` as a second line when
  set), an image from `files` as a rounded preview, "allow until the
  end of the request" when offered, and "Confirm (N)" (with targets) or
  "Confirm", and "Decline". A final card shows its status line.
- A `Method` card is titled `lng_agent_card_title_method`, then shows
  `lng_agent_card_method_description` over the model's `text`,
  `lng_agent_card_method_name` over the method in monospace, its rows
  like a `Change` card, and a "Show parameters" / "Hide parameters" link
  that slides the `proposedText` JSON open in a code block
  (`MakeCodeBlock()` of the card box); the state survives rebuilds of
  the card. Steps read `lng_agent_tool_find_methods`,
  `lng_agent_tool_describe_method` with `name` and
  `lng_agent_tool_call_method` with `method`.
- Steps: `list_members` with `filter` `admins` reads
  `lng_agent_tool_list_admins`, with `banned` or `restricted`
  `lng_agent_tool_list_banned`; `list_invite_links`,
  `list_join_requests`, `list_contacts`, `list_blocked`,
  `list_emoji_statuses`, `summarize_message`, `translate` and
  `ai_compose` read `lng_agent_tool_<name>` (`summarize`, `translate`,
  `ai_compose` for the last three). `rename_contact` action rows read
  `lng_agent_action_rename_contact` with the result's `name`.
- `FormatTaskTool()` maps every action and visible tool of stage 3a to its
  `lng_agent_task_tool_<name>`; the three invite link tools share
  `lng_agent_task_tool_invite_links`.
- `ChatView::setComposerOnTop()` puts the composer right under the queue
  strip and the list below it, hides the welcome text and lays the list
  out at its content height (aligned to the top).
  `naturalHeightValue()` is the height the view needs: the strip, the
  composer and, with messages, the list's content height up to
  `agentPaletteNaturalList` (always that height without the composer on
  top). `setBottomRadius()` rounds the bottom corners of the wallpaper;
  `hasMessages()` says whether the list has items.
- Drafts (plan 11.10): `Manager::drafts()` (`agent_drafts.*`) keeps the
  composer text, cursor and (in memory only) the reply per key: a
  session id, `new` for the agent chat without a session, `new:<peer>`
  for a chat panel without a session and `palette` for the palette
  (`ChatView::setNewSessionDraftKey()`). It is the agent value `drafts`
  (version, then key, text, serialized tags and the cursor of each
  non-empty draft), written a second after the last change. `ChatView`
  saves the field under the current key on every change and swaps the
  text in when the shown session changes; the first send of a new
  session moves the key to the created session, so the sent text never
  stays behind. Removing a session drops its draft, `clearAllData()`
  all of them. The compose controls still write the history draft of
  their topic root, but the view overwrites the field when it is
  created.
- `ChatView::setMentionsProvider()` adds peers to every request's
  `mentions`, so they count as named for the taint rule.
- `ChatView::passMessages()` puts the passed messages into the agent
  history's forward draft at topic root 0, which the compose controls
  show as their native forward bar with a cancel button (a composer on a
  fake topic root cannot show it: forward drafts exist only for real
  threads). The next request carries them: `untrusted` gets "Messages the user passed to
  you:" and `{"chat": {"id", "title"}, "messages": [...]}` with every
  message as `ToolFormat::SerializeMessage()` gives it, the chat joins
  `mentions`, and the bar empties.
- The palette (`agent_palette.*`) is a `Ui::LayerWidget` over the main
  window shown by `ShowPalette()` (false when `Manager::enabled()` is
  false), `Ctrl+Shift+Space` by default. It is `agentPaletteWidth` wide
  (less in a narrow window), `agentPaletteTop` from the top, and as high
  as its header plus the view's natural height (plus a `boxRadius` strip
  of the box background under the composer while the list is empty), at
  most down to `agentPaletteMargin` from the bottom; the view rounds its
  wallpaper's bottom corners by `boxRadius`. The header shows the agent
  userpic, the session title (or the agent name) and "new", "open in
  the agent" (only with a session) and "close" buttons. It holds an
  account scope `ChatView` with the composer on top and its own draft
  (`palette`) until it has a session; the first request creates a session, the next ones continue it, "new" starts
  over, and every opening of the palette starts without a session.
  "Open in the agent" closes the palette and opens the session with
  `ShowGeneralAgentSession()`. Escape, the close button and a click
  outside close it; the session keeps working.
- The palette knows the chat open in the window when it opened (unless
  it is hidden from the agent or is the agent chat itself): every request
  adds the hint `The user
  has the chat "<title>" (id <id>) open; "this chat" and similar words
  mean it.` and the chat to `mentions`.
- `CanPassToAgent()` is true for regular non-service messages of one
  chat that is not hidden from the agent, while `Manager::enabled()`.
  `PassToAgent()` resolves the ids (a single id expands to its album),
  sorts them by id and opens the palette with them.
  `AddPassToAgentAction()` adds "Pass to agent" (one message) or "Pass
  selected to agent" when the messages can be passed; the selected
  variant clears the selection after passing. The selection bar shows
  "To agent" next to "Forward" when forwarding is allowed and the chat
  can be passed.
- Model profiles (plan 11.7). The page starts with "Models": a row per
  profile (`ModelTitle()`, `ModelDescription()` under it, the
  `ai_prompt` icon for OpenRouter and `ip_address` for the rest; a click
  is `ShowEditModel()`), "Add model" (`ShowAddModel()`) and "Default
  model" with a single-choice box over the profiles that sets
  `defaultId`. The page reloads its state on every `ProvidersChanged()`;
  every box saves through `UpdateProviders()` (load, change, save).
- `agent_model_boxes.*`: `ModelTitle()` is the title, else the model id,
  else `lng_agent_provider_default`. `ModelDescription()` is "OpenRouter
  · <model>" or "<base URL host> · <model>". `ShowAddModel()` offers
  "OpenRouter" and "Other provider"; `added` gets the new profile id.
  OpenRouter asks for the key only when `ServiceKey()` is empty (checked
  with `KeyRequest()`: 200 goes on, 401 or 403 mark the field), then the
  model list, then the name (`SuggestedName()` drops the `Vendor: `
  prefix) with "Default model" checked when no default profile exists.
  The list is `/models` of OpenRouter (cached for ten minutes), models
  whose `tools` is false left out, filtered by `SearchModels()`, each row
  the name and "<context> context · <prices> · <id>", a check at the
  current model; Enter in the search picks the first match. Choosing a
  model copies its id, context window, abilities and prices into the
  profile. A new OpenRouter profile gets `SetServiceKey()` of its key.
- `ShowEditModel()` opens the OpenRouter box (name, key, "Model ID" with
  the same list, "OpenRouter providers", "Check connection", "Default
  model", Delete) or the "Other provider" form (the manual fields plus
  "Check connection" and "Default model"). Changing the model of an
  OpenRouter profile clears its `ignoredRoutes`; saving one sets the key
  of every OpenRouter profile. "OpenRouter providers" lists the
  `ParseEndpoints()` routes that have `tools` as checkboxes (unchecked
  ones are ignored), keeps ignored names that are no longer listed and
  refuses to uncheck all. Deleting asks first, then clears `defaultId`,
  `mediaId` and `backgroundId` that named the profile.
- Stage 4 (plan 11.8): `ShowAddModel()` also offers "Anthropic", which
  opens `ShowAnthropicChoice()` ("Claude subscription" and "API key").
  `agent_model_boxes_common.h` exposes the shared pieces in
  `Agent::Ui::details`: `ModelListBox()` over a `ModelSource` (`load`,
  `describe`, `usable`), `Fetch()`, `AddKeyField()`, `ConfirmRemove()`,
  `ApplyDefault()`.
- `agent_claude_boxes.*`: the subscription setup box finds the program
  (`FindClaudeProgram()` of `claudePath`; when missing it shows
  `ClaudeInstallCommand()` as code, "Check again" and "Choose the
  program"), runs `claude --version` (older than `kMinVersion` asks to
  update) and `claude auth status`; when not signed in, "Sign in to
  Claude" runs `claude auth login --claudeai` and waits for it (the
  binary opens the browser itself; AIgram never reads its output for
  codes), then checks again. Signed in, the models come from
  `Claude::ModelsQuery` (the `default` entry is skipped, each row shows
  the description and the value) and the name box suggests "Claude
  <displayName>". The profile: kind `ClaudeCode`, service
  `ClaudeSubscription`, the model value, context window 200 000, `vision`.
- "API key": the key box checks the key with `Anthropic::ModelsRequest()`
  (401/403 marks the field) unless `ServiceKey(Anthropic)` exists; the
  list is `Anthropic::ParseModels()` with prices from `KnownPrices()`;
  the profile copies the window, `vision`, `files` (PDF), the prices and
  `priceCacheWrite`, and keeps `effort` only when the model supports it.
- Edit boxes: the subscription box has name, model (the same list),
  "Thinking effort" (`EffortText()`; the levels the model reported, else
  all of `Anthropic::EffortLevels()`), "Account" (`auth status`; a click
  opens the setup box), "Claude Code program" (choose a file or go back
  to automatic search), the two limit rows, "Check connection", "Default
  model" and Delete. The API key box has name, key, model, effort, "Check
  connection", "Default model" and Delete. `ShowCheckBox()` runs a
  `Claude::Engine` with the test tool for subscription profiles.
- Stage 6 (plan 11.11): `ShowAddModel()` also offers "OpenAI", which
  opens `ShowOpenAiChoice()` ("ChatGPT subscription" and "API key",
  `lng_agent_openai_about`). `agent_openai_boxes.*`:
  - The subscription box: when `ChatGptSignedIn()` the sign-in step is
    skipped. Otherwise `lng_agent_chatgpt_about` and the "Continue with
    ChatGPT" button (the ChatGPT logo left of the text); a click starts a
    `ChatGptLogin` and shows `lng_agent_chatgpt_waiting`, "Open the
    browser again" (`openBrowser()`) and a field for the pasted address
    (`lng_agent_chatgpt_paste_about`, or `_paste_busy` when not
    `listening()`; Enter or the box button calls `paste()`, an error
    marks the field). An error from `done` is shown in the box with the
    button to try again (`access_denied` → `lng_agent_chatgpt_denied`,
    no plan scope → `lng_agent_chatgpt_no_plan`, else
    `lng_agent_chatgpt_login_failed`). Signed in, the model list is
    `ResponsesModelsRequest()` of a plan profile through
    `Manager::transportFor()` and `ParseResponsesModels()`; then the
    name box (the model name) with "Default model". The profile: kind
    `OpenAiResponses`, service `ChatGptSubscription`, `OpenAiBaseUrl()`,
    the slug as the model, the window, `vision`, `files`, no prices.
  - "API key": the key box (`lng_agent_openai_key_about` with
    `platform.openai.com/api-keys`) checks the key with
    `ResponsesModelsRequest()` (401/403 marks the field,
    `lng_agent_openai_key_wrong`) unless `ServiceKey(OpenAi)` exists;
    the list is `ParseResponsesModels()`; the profile (kind
    `OpenAiResponses`, service `OpenAi`) copies the window, abilities and
    prices; a new profile sets the key of every OpenAI profile.
  - `ShowEditChatGpt()`: name, model (the same list), "Thinking effort"
    (`EffortText()`, `lng_agent_effort_none` for `none`; the model's
    levels, else all of `ResponsesEffortLevels()`;
    `lng_agent_effort_about_openai`), "ChatGPT account" (the email and
    the plan, or "Not signed in"; a click when signed out opens the
    sign-in), "Sign out" (confirm, `ChatGptSignOut()`), "Manage usage"
    (opens `ChatGpt::UsageUrl()`, with `lng_agent_chatgpt_usage_about`),
    "Check connection", "Default model" and Delete.
    `ShowEditOpenAiKey()`: name, key, model, effort, the three prices,
    "Check connection", "Default model" and Delete.
  - `ShowEditModel()` opens these for the two services;
    `ModelDescription()` is "ChatGPT subscription · <model>" and "OpenAI
    · <model>" for them; the settings list shows them with the OpenAI
    choice icon.
- The model menu of `ChatView` shows each profile in two lines: the
  title and `ModelDescription()` under it (plan 11.11: OpenAI wants the
  plan named at the model selector).
- A failed turn of a plan profile: `chatgpt_signed_out` and other `Auth`
  errors show `lng_agent_chatgpt_error_signed_out` (or
  `lng_agent_chatgpt_error_plan` with the text for
  `subscription_sharing_user_not_eligible`) with "Model settings";
  `subscription_sharing_usage_limit_exceeded` shows
  `lng_agent_chatgpt_error_limit` with "Manage usage" (opens
  `UsageUrl()`) and "Change model" (the model menu) instead of
  "Model settings".
- The settings "Spending" block adds "ChatGPT usage" (opens
  `UsageUrl()`) when a plan profile exists.
- `ClaudeLimitValue(week)` is "<percent>% used · resets <time or date>"
  of the latest limits, or "Known after the first request";
  `AddClaudeLimits()` adds both rows. The settings "Spending" block adds
  them when a subscription profile exists, shows the totals without the
  cache share and a "Prompt cache" row (`CachedPercent()` of the last 30
  days); `FormatSpending()` adds "cache N%" when asked to and the share
  is positive (the session line).
- A failed turn with an `Auth` error shows "Model settings" next to
  "Retry" under the error message (`ShowEditModel()` of the session's
  profile); for a subscription profile the text is the localized "not
  found" (`ClaudeMissingText()`) or "not signed in" message, for other
  profiles `lng_agent_failed_auth` with the provider's text.
- The session model is the bot-menu-style button left of the field
  (`customMenuText` of the compose controls, hidden when the title is
  empty). `ChatView` shows the session's profile title there (none for
  runs), for a view without a session the profile it will use: the one
  chosen in the menu, else the default. A click opens a popup menu with every
  profile (a check at the current one) and "Add model..."; choosing sets
  the session's profile (`Session::setProfile()`), or remembers it until
  the view creates its session; a model added from the menu is chosen
  right away. Showing another session or a new one forgets that choice.
- The settings page adds a "Background tasks" block after "Media":
  "Background model" with the chosen provider's title or "Default
  model", opening a single-choice box (the default plus every profile) that
  sets `backgroundId`, its about line, and "Daily limit" with
  `lng_agent_background_limit_value`, opening a box with the dollars and
  tokens fields, with `lng_agent_background_limit_about`.
