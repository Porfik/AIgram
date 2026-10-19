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
  `agent/ui/agent_panel.style` and `agent/ui/agent_general.style`). It may
  include `agent/core`, never the other way round.
- Tools: `agent_tool_table.h` holds `ToolEntry`, `PendingCall` and the
  schema helpers; each `agent_tools_<group>.cpp` returns its entries from
  `<Group>Tools::Entries()`: `chat` (reading one chat), `general` (chat
  list, settings navigation, handoff), `actions` (card actions),
  `chat_state` (mute, archive, pin, read) and `folders`. `agent_tools.cpp`
  is the `ToolRegistry` and `load_tools`.
- UI: `agent_feed_cards.*` are the card and action rows of the feed,
  `agent_section.*` the general agent screen and `agent_chats_row.*` the
  "✦ Agent" row above the chat list.
- Hooks into existing code stay one call or one enum value wide:
  `Main::Session::agent()`, the agent values in `Storage::Account`,
  `Info::Section::Type::Agent`, `Agent::Ui::SetupPanelTopBar()` in
  `Info::WrapWidget::setupTopBarMenuToggle()`, the ✦ button in the history
  top bar, the third column follow in
  `MainWidget::updateThirdColumnToCurrentChat()`,
  `Shortcuts::Command::AgentPanel` and `Agent::Ui::SetupChatsListRow()` in
  `Dialogs::Widget`.
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

## Provider settings (`agent_providers.h`)

- `SerializeProviders()` and `DeserializeProviders()` use `QDataStream` with
  a leading format version, so later fields can be appended. Unknown future
  versions and truncated data deserialize to `std::nullopt`.
- The serialized bytes include API keys. They are stored only through the
  encrypted `Storage::Domain` blob and never written to logs.
- `ProvidersState::showEntryPoints` (default true) hides the ✦ button and the
  agent row when false; it is appended to the stream under a new version.
- `Agent::SaveProviders()` fires `Agent::ProvidersChanged()` after writing.

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
- `check()` decides in this order: not available, `NotInMode`; risk
  `Forbidden`, `Forbidden`; any excluded peer, `Excluded`; a zero peer or, in
  the chat scope, any other peer, `OutOfScope`. `Read` and `Reversible` are
  allowed. `Visible` is allowed only when this turn's allowances for the same
  tool cover every requested peer (any allowance for the tool covers an
  empty peer list) and the taint rule does not block it; otherwise it is
  `Confirm`.
- Taint rule: when the gate is tainted and the policy is `outbound`, any
  requested peer missing from the turn's named peers blocks allowances, and
  `Decision::canAllowForTurn` is false. Otherwise `Confirm` offers it.
- `allowForTurn()` adds the request's peers to the tool's allowance.
  `beginTurn()` replaces the named peers and drops all allowances.
- Taint is sticky for the session: `markTainted()` is never undone, and the
  session persists it in `SessionRecord::tainted`, because content read
  earlier stays in the model context.

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
- `CardTextEditable()` is true for `Send`, `Draft` and `Edit`.
  `CardHasAlternate()` is true for `Send` ("to draft") and `Draft`
  ("append"); `Execute` means "send", "replace" or "confirm".
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

## Session records (`agent_record.h`)

- `GenerateSessionId()` is 16 lowercase hex digits.
- `SessionRecord::messages` is the full transcript shown to the user.
  `messages[0, summaryUntil)` are replaced by `summary` in the model context;
  `0 <= summaryUntil <= messages.size()`.
- `SessionInfo::titleFinal` is false while the title is the first line of
  the first message, and true once the model or the user named it.
  `created` and `updated` are unix seconds; `touched` lists the peers the
  session's tools read or acted on, without duplicates.
- `cards` are the session's cards in creation order, `loadedGroups` the
  tool groups `load_tools` loaded in load order, `undone` the tool use ids
  of actions the user undid.
- Serialization uses `QDataStream` (`Qt_5_1`) with a leading `quint32`
  version, like `agent_providers`. Unknown versions, unknown block tags,
  truncated data and an invalid `summaryUntil` deserialize to
  `std::nullopt`. Every `Block` alternative round-trips, including `hidden`,
  `signature`, `inputError` and `rawInput`. Version 2 adds `cards`,
  `loadedGroups` and `undone` (every `Card` field round-trips); version 1
  records still load with them empty. The index format is unchanged.
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
  an empty line, or nothing when there is no visible text. Its tool results
  attach to the `FeedTool` with the same id and never show on their own;
  images are skipped.
- An assistant message gives one `FeedAssistant` per run of adjacent text
  blocks. Its non-empty thinking blocks (`FeedThinking`) and tool uses
  (`FeedTool`) are steps: steps not separated by visible text form one
  `FeedSteps` item, also across messages, so a whole run of tool rounds is
  one item. Its `index` is the message of the first step. Visible user text,
  assistant text and `FeedSummary` end the run; hidden text does not.
- `FeedSummary` stands before the item of `messages[summaryUntil]` when
  `summaryUntil > 0`, or last when everything is summarized.
- A tool use with a card in `cards` becomes a `FeedCard` at its place, with
  its result attached; it ends the steps run like visible text.
- A tool use named in `actionTools` (and without a card) goes into the
  run's `FeedActions` item instead of `FeedSteps`. In a run the
  `FeedSteps` item (if any) comes before the `FeedActions` item (if any);
  the latter's `index` is the message of its first action.

## Context (`agent_context.h`)

- `EstimateTokens()` is a cheap upper bound: one token per three characters
  of text, tool names, arguments and results; a flat 1000 per image; four
  per message. For a request it adds the system prompt and the tool specs.
- `CapToolResult()` keeps results up to `maxChars` and otherwise cuts them,
  without splitting a surrogate pair, adding a line that says how many
  characters were dropped and asks for a smaller page.
- `ContextMessages()` is `messages[summaryUntil, end)` with the summary as a
  hidden text block put first into the first message when that is a user
  message, or as its own leading user message otherwise.
- `PlanCompaction()` returns the new `summaryUntil`, or the current one when
  the estimate of `fixedTokens` plus the context messages fits in 70% of
  the window (`kDefaultContextWindow` when the window is not positive). A
  cut is a turn start (a user message with a text block) after the current
  `summaryUntil`, or the transcript end. The smallest cut whose remaining
  messages, the fixed part and a 2000-token summary fit in 40% of the window
  wins; when none does, the transcript end.
- `CompactionRequest()` asks, without tools, for a summary of the previous
  summary and `messages[summaryUntil, until)` rendered as plain text, tool
  results capped at 2000 characters. `TitleRequest()` asks for a 3–6 word
  title in the user's language from the first user text and the first
  answer. `ResponseText()` joins a message's text blocks; `CleanTitle()`
  takes the first non-empty line, strips quotes and a trailing period and
  caps it at 60 characters.
- `HandoffText()` renders a chat agent session for the general agent:
  a first line naming the chat title, the summary when there is one, then
  the transcript after `summaryUntil` rendered like the compaction input
  (tool results capped at 2000 characters). Longer than
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
  need the general agent. General instructions and memory come last.
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

- `SingleRequest` sends one request without tools on construction and calls
  `done` exactly once, asynchronously, with the parsed `Response`, network
  errors included. Destroying it cancels the request and `done` is never
  called. `done` may destroy the `SingleRequest`. It serves compaction and
  titles.

## Manager and sessions (`agent_manager.h`, `agent_session.h`)

- `Manager` is the account-level owner, `Main::Session::agent()`. It keeps
  the provider state, refreshed on `ProvidersChanged()`, one
  `HttpTransport`, the session index and every opened `Session`; a session
  keeps running when its UI closes. `available()` means the active provider
  can make an adapter; `enabled()` additionally needs `showEntryPoints`.
- `contextWindow()` is the active provider's `contextWindow`, else the value
  learned once from its `/models`, else `kDefaultContextWindow`.
- `list(scope)` returns the index entries of that scope, newest `updated`
  first; `listChanges()` fires the scope after its entries change.
  `create()` makes a session that joins the index on its first message.
  `open()` returns a loaded session or loads it, `nullptr` for unknown or
  unreadable ids. `remove()` stops the session and deletes its file.
- `SessionStore` keeps `index` and `s/<id>` agent values of
  `Storage::Account` (map key `lskAgentStorages`, encrypted with the local
  key like the wallet engine values).
- `Session::send()` queues the request while the session is working; the
  queue shows in `SessionState::queued` and runs in order after each
  finished turn, on the next main loop iteration. After a failed turn the
  queue waits for `retry()` or `stop()`. `stop()` interrupts the turn or the compaction and clears
  the queue. `retry()` resumes a failed turn whose history ends with a user
  message.
- A dispatch builds the user message (the text, then the hint as a hidden
  block), starts a policy turn with the mentions plus `FindNamedPeers()`,
  compacts first when `PlanCompaction()` asks, and recreates the `Loop` from
  `ContextMessages()` with a fresh system prompt and the active provider.
  Messages the loop appends are copied into the record, which is saved at
  every turn end.
- A turn that fails with a context overflow is compacted and resumed once.
  After the first completed turn of a session without a final title a
  `TitleRequest()` names it.
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
- Undo: `canUndo()` asks the registry. `undo()` undoes each id that can
  be undone; a success appends the id to `undone`, saves the record and
  fires `undoChanges()`, a failure fires `undoErrors()` with the error and
  `undoChanges()`. The next dispatch adds "The user undid: <tool> (<id>),
  ..." for the ids undone since the previous dispatch to the hint.
- The registry starts with the record's `loadedGroups` and every
  `loadedGroupsChanges()` copies them back into the record.
- `Manager::activity()` of a scope: `working` when any open session of the
  scope is `Working` or `Compacting`, `pendingCards` summed over them.
  `activityValue()` starts with the current value and fires on changes,
  without repeats.

## Tools (`agent/tools/`)

- `ToolRegistry` is a session's `ToolExecutor`. `tools()` lists only the
  tools the gate makes available, using `generalDescription` in the
  account scope when it is set. Entries with a `group` are listed only
  after `load_tools` loaded it, after all other tools, in load order, so
  earlier specs never move. Every call goes through `PolicyGate::check()`,
  even for a tool that was never offered; a group tool that is not loaded
  fails with a hint to call `load_tools`.
- Chat parameters: in the chat scope tools have none, and every call gets
  the scope peer as `peer` and `peers`. In the account scope the registry
  adds them to the schema by `ChatParam`: `One` a required `chat` string,
  `OptionalOne` an optional one, `Many` a required `chats` array of 1–100
  strings. It resolves them with `ResolveChatRef()`; an unknown or
  invisible chat fails the call. `peer` is the resolved `chat` (null when
  omitted), `peers` the resolved `chat` or `chats`. The gate checks
  `peers`; `touched()` reports them.
- `ResolveChatRef()` takes a chat id (a JSON number or a numeric string,
  `PeerId::value` as in every tool result), `@username`, or a
  `t.me/username` link, and finds only peers already loaded in
  `Data::Session`; otherwise null.
- Results are compact JSON capped with `CapToolResult()`. In the chat
  scope every successful result, and in the account scope every result of
  an entry with `untrusted`, goes through `WrapUntrusted()` after capping
  and marks the gate tainted. `untrusted` entries are those whose results
  carry anything other people wrote (messages, names, titles, bios,
  descriptions). The registry never grants allowances itself: only a
  confirmed card does.
- A tool finishes once: with `done` or `fail`, or, after `confirm`, with
  `finishCard`. `confirm(card, execute)` shows the card and calls
  `execute` with the decided card after the user approved it; when the
  gate allowed the call without a confirmation the card is recorded
  `autoApproved` and `execute` runs at once. A `Draft` card is always
  shown and never offers "allow for the turn". A declined card is
  finished by the registry. `finishCard` records the final card and
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
- `load_tools` (account and chat scope) takes a `group`; the only group is
  `folders`. Loading an already loaded group is a no-op success.
- Messages are passed as id, local date and time, sender, text with
  entities as Markdown, reply id and a short media description. Reading
  never marks anything read.
- Action results (entries with `action`, shown as action rows) are
  `{"done": [{"chat": id, "title": ...}], "failed": [{"chat": id,
  "title": ..., "error": ...}]}`, folder tools add `"folder": {"id": id,
  "title": ...}`, `reorder_folders` returns `{"order": [ids]}`.
- `ActionToolNames()` lists the names of entries with `action`.

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
| `load_tools` | registry | Read | both | None | |

## UI (`agent/ui/`)

- `SessionView` is the session pane shared by the chat panel and the general
  agent screen: the feed from `BuildFeed()` plus live streaming, and the
  composer. Without a session it shows the new-session state and creates the
  session through `Manager::create()` on the first send.
- The chat panel is an Info section (`Info::Section::Type::Agent`, keyed by
  the peer) built by `MakePanelMemento()`; `TogglePanel()` opens or closes
  it and `PanelAvailable()` says whether a peer can have one.
- The panel goes to the third column whenever the window fits one without a
  resize, turning that column on if the profile had it off. Only a narrow
  window shows it as a layer, sized to fit the window; once the window grows
  wide enough, the layer moves to the third column, keeping the session, the
  history list state and the unsent text.
- The panel has no header of its own: its title (the session title, the
  agent name without a session, "Sessions" with the list open) and the "new
  session" and "history" buttons live in the Info top bar, added by
  `SetupPanelTopBar()`. The panel content lies over the Info scroll area,
  which only holds an empty widget reporting the layer height, so the Info
  scroll never moves.
- The feed is a `Ui::VerticalLayout` in a scroll area. Rows are deleted
  explicitly, never left to `object_ptr`. A `FeedSteps` row is collapsed by
  default and shows the last tool, the step count and failures; its steps
  expand to the thinking text or the arguments and result, shown without the
  untrusted tags and with JSON indented. The feed keeps to the bottom while
  the user is there, except while an expand animation runs.
- The feed is built with the record's `cards` and `ActionToolNames()`.
  A `FeedCard` shows its kind's title, the targets as checkboxes, the
  editable text while pending, `detail`, the option and "allow until the
  end of the request" when offered, and the buttons: "Send (N)", "To
  draft", "Decline" for `Send`; "Replace", "Append", "Cancel" for
  `Draft`; "Confirm (N)", "Decline" otherwise; "Continue in the general
  agent" for `Handoff`. The main button is disabled without checked
  targets (when there are targets) or with an empty editable text. A
  final card shows its status line instead of buttons, read-only.
- A `FeedActions` item shows one row per action: the action text from the
  tool name, its input and the `done` titles, failures marked with their
  errors, and "Undo" while `Session::canUndo()`, "Undone" once undone;
  "Undo all" when two or more of its actions can be undone.
- While a card waits, the status line says the agent waits for a
  decision.
- The handoff button calls `ShowGeneralAgent()` with a `HandoffDraft`:
  the card's text, `HandoffText()` of the chat session and the chat.
  `SessionView::startHandoff()` shows the new-session state with a note
  naming the chat and the text in the composer; the first send passes the
  untrusted text, starting or opening another session drops it.
- `SessionView::setContentMaxWidth()` centers the feed items and the
  composer within that width while the scroll bar stays at the edge.
- The general agent screen is a `Window::SectionWidget` in the center
  column, built like the panel: a top bar with the title, "new session"
  and "history"; the history replaces the feed with the account scope's
  sessions, each with the userpics of the chats it touched; the feed and
  the composer limited to `agentGeneralContentMaxWidth`. Its memento keeps
  the session, the history state and the unsent text.
  `ShowGeneralAgent()` shows it; chats opened by the agent push it into
  the section stack, and its sessions keep running in the background.
- `SetupChatsListRow()` puts the "✦ Agent" row on top of the dialogs list
  while `shown` (only the main "All chats" list, without search, folders
  or forums) and `Manager::enabled()`. It shows "Working..." or the
  number of waiting cards from `Manager::activityValue()` and opens the
  general agent screen.
