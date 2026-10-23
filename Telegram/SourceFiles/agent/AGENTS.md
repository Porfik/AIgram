# agent map

AIgram's built-in AI agent: a model that reads and acts in Telegram on the
user's behalf, with every visible action confirmed by the user. The product
plan and the reasons behind decisions are in `docs/ai-agent-plan.md`
(section 11 is the decision log); this map and its submaps are the code
contracts.

## Maps

- `core/AGENTS.md` — the pure `td_agent` library: conversation model,
  adapters, engines, providers, records, policy, cards, tasks, TL codec,
  prompts. Read it for any change under `core/` or to a contract the
  client relies on.
- `tools/AGENTS.md` — the tool registry, groups and the tool table.
- `ui/AGENTS.md` — widgets, settings and model boxes.
- This file — placement, upstream hooks and the client side in `agent/`.

Read this file and the submap of the directory you touch. A change of a
contract updates its map in the same commit. Comments in code stay short
(the comment hook allows two lines and one `// WHY:` block per commit), so
semantics belong in the maps. Keep entries to contracts, invariants and
deliberate choices that look odd; wire formats, model tables and version
histories that the code and tests already pin stay out.

## Placement

- `core/` must not include anything from the client. `agent/` may include
  `core/`, never the other way round.
- `agent/` (outside `core/`, `tools/`, `ui/`) is the client side:
  - `agent_manager.*` — `Manager`, the account owner
    (`Main::Session::agent()`).
  - `agent_session.*`, `agent_session_store.*` — sessions and their storage.
  - `agent_settings.*` — `LoadProviders()`, `SaveProviders()`,
    `UpdateProviders()`, `SaveChatGptAccount()`, `ProvidersChanged()`.
  - `agent_http_transport.*` — `HttpTransport` over `QNetworkAccessManager`.
  - `agent_check.*` — "Check connection" (`ShowCheckBox()`).
  - `agent_claude.*` — the Claude Code client side; `agent_chatgpt.*` —
    the ChatGPT account, sign-in and transport.
  - `agent_memories.*`, `agent_exclusions.*`, `agent_spending_tracker.*`,
    `agent_attachments.*`, `agent_drafts.*` — account data.
  - `agent_media_loader.*`, `agent_media_convert.*`, `agent_files.*` —
    message media, conversion and the agent folder.
  - `agent_tasks.*`, `agent_task_runner.*`, `agent_notifications.*` —
    background tasks.
  - `agent_chat.*` — the local agent chat peer.
- The settings page is `settings/sections/settings_agent*`. Provider
  settings are stored encrypted in `Storage::Domain`
  (`storage/storage_domain*`), next to the wallet keyring.
- `cmake/td_agent.cmake` builds `td_agent` and `td_agent_test`.

## Invariants

- API keys and tokens live only in the encrypted `Storage::Domain` blob.
  Nothing logs request bodies, keys, tokens or the `Authorization` header.
- Anything other people wrote reaches the model wrapped by
  `WrapUntrusted()` and taints the session (`../core/AGENTS.md`, "Scope and
  policy"). Trusted hints never contain titles or texts others wrote.
- Hidden chats (`Exclusions`) are invisible to every tool, list, prompt and
  task; sessions and memory of hidden chats stay stored.
- Visible actions run only after a card the user approved, or an allowance
  such a card granted for the turn.
- Reading never marks messages read.

## Hooks into upstream code

Each hook stays one call or one enum value wide, so upstream merges stay
easy. When merging upstream, check that every hook below survived.

- `Main::Session::agent()`; the agent values in `Storage::Account`;
  `Info::Section::Type::Agent`; `Agent::Ui::SetupPanelTopBar()` in
  `Info::WrapWidget::setupTopBarMenuToggle()`; the ✦ button and
  `Agent::Ui::PanelAvailabilityChanges()` in the history top bar; the third
  column follow in `MainWidget::updateThirdColumnToCurrentChat()`.
- `Shortcuts::Command::AgentPanel` and `AgentPalette` (handled in
  `Dialogs::InnerWidget::setupShortcuts()`).
- `Agent::Ui::AddPassToAgentAction()` in both message context menus, the
  agent reply and forward actions at the start of the history context
  menu's own ones, and the "To agent" button of `HistoryView::TopBarWidget`
  in selection mode (`agentSelectionRequest()`, handled by `HistoryWidget`
  and `HistoryView::ChatWidget`).
- `Agent::Ui::FillPeerMenu()` in the peer menu filler
  (`window_peer_menu.cpp`),
  `Agent::Ui::FillFolderMenu()` in both folder menus,
  `Window::FillDialogsEntryMenu()` (`Agent::Ui::FillAgentChatMenu()`).
- `NativeManager::showCustomNotification()` and its activation check at the
  start of `Window::Notifications::Manager::notificationActivated()`.
- `HistoryView::ComposeControlsDescriptor::customMenuText` /
  `customMenuClicked` (the session model button) and `customStopShown` /
  `customStopClicked`; `ChatHelpers::ComposeFeatures::aiEditor` false hides
  Telegram's AI editor in agent composers; `ComposeControls::inputField()`
  for the drafts (`fieldForMention()` is null while the field is hidden).
- `ShowUserBotIcon()` in `dialogs_layout.cpp`; `TabbedPanel` fits narrow
  parents (`moveHorizontally()`).
- The agent peer stays off the server with one-line `Agent::IsAgentPeer()`
  / `IsAgentHistory()` checks in: `MainWidget::showHistory()` (opens the
  general agent screen), `ApiWrap::savePinnedOrder()`, the pinned limit in
  `Data::Session::pinnedCanPin()` (`CountPinnedForLimit()`) and
  `maxPinnedChatsLimitValue()`, `ApiWrap::requestFullPeer()`,
  `Data::Histories::requestDialogEntry()`,
  `Data::ScheduledMessages::request()`, cloud drafts
  (`ShouldSkipPlainDraftCloudSave()`,
  `ApiWrap::savePreparedDraftToCloud()`), `Data::ChatFilter::contains()`,
  `Info::Memento` (the agent profile; `thirdSectionIsAgentPanel()`),
  `Api::ActivateBotButton()` (`ChatItems::Activate()`) and
  `HistoryView::Message::textState()` (agent items are no history entries,
  so their inline keyboard is hit-tested like an admin log entry's).
  `Exclusions::excluded()` is true for the agent peer.

## Localization

- User-facing texts are `lng_agent_*` keys in `lang.strings`. Telegram's
  cloud packs never have them, so their Russian lives in
  `Resources/langs/ru.strings` (`:/langs/ru.strings`), which
  `Lang::Instance` applies under the cloud values. Every new or changed key
  gets its Russian in the same commit, with the same tags and the four
  plural forms (`#one`, `#few`, `#many`, `#other`).
- Phrases built from pieces (`ui/agent_task_format.cpp`) use prepositional
  pieces after "в": "Новое сообщение в {chats}" with "личных чатах",
  "папке {folder}", "чате {chat}" (names don't decline).
- `{count}` is the plural tag. A key with a new `{tag}` regenerates
  `lang_auto.h` and rebuilds most of the client: prefer existing tags.

## Manager and sessions (`agent_manager.h`, `agent_session.h`)

- `Manager` keeps the provider state (refreshed on `ProvidersChanged()`),
  one `HttpTransport`, the session index and every opened `Session`; a
  session keeps running when its UI closes. `available()` means the default
  provider can run (`canRun()`: an adapter, or `UsesClaudeCode()`);
  `enabled()` also needs `showEntryPoints`.
- `contextWindow()` and `abilities()` of a provider are its own values,
  else what its `/models` reported (asked once per base URL after every
  providers change). `sessionProvider()`, `mediaProvider()` and
  `taskProvider()` are null when the provider can't run.
  `transportFor(provider)` is the transport every provider request takes
  (sessions, `single()`, model lists, "Check connection").
- `list(scope)` is newest first; `create()` joins the index on the first
  message; `open()` loads; `remove()` deletes the record and its
  attachments. `SessionStore` keeps `index` and `s/<id>` agent values of
  `Storage::Account` (encrypted with the local key).
- `Session::send()` queues while working; the queue runs in order after
  each turn and waits after a failure for `retry()`, `interrupt()` or
  `stop()`. `stop()` clears the queue; `interrupt()` ("Stop") merges it into
  one request that runs next.
- A dispatch builds the user message (text, then a hidden hint: undone
  actions, `FormatTurnContext()`, chat counters), starts a policy turn with
  mentions plus `FindNamedPeers()`, compacts when `PlanCompaction()` asks,
  and recreates the engine from `ContextMessages()` with a fresh system
  prompt. Loop messages are copied into the record, saved at every turn
  end. A context overflow is compacted and resumed once. The first
  completed turn of an untitled session asks `TitleRequest()`.
- `untrusted` text and attachments of a request taint the gate before the
  turn. Attachments are stored with `Attachments::add()` and the message
  gets media blocks without bytes; `runLoop()` gives the turn start's images
  their bytes (`PrepareImageForModel()`), and every turn end drops all bytes
  before saving.
- The session's provider: `RunSetup::provider` for a run, else
  `SessionProvider()` of `info.profileId`; the first dispatch stores the
  default's id, so changing the default never moves existing sessions.
  `setProfile()` applies from the next dispatch. A `UsesClaudeCode()`
  provider runs a `Claude::Engine` with the manager's `ClaudePool` (runs
  never stay warm). OpenRouter without `files` gets `pdfEngine` `pdf-text`.
  Every dispatch passes the session id as `cacheKey`.
- Memory changes reach the prompt from the next dispatch, never within a
  turn. Usage of every response, compaction, title and media model call is
  priced with the provider that served it and goes to `info.usage` and
  `SpendingTracker`.
- Cards from `ToolRegistry::cardUpdates()` replace or append the record's
  card and are saved at once, so a waiting card survives a restart; a
  waiting card keeps the turn `Working`; `stop()` declines it. A loaded
  record goes through `CloseInterruptedTurn()`.
- Undo: a success appends to `undone` and the next hint says what the user
  undid. When exclusions change, open chat sessions of newly hidden chats
  stop.
- `activity(scope)`: `working` and the summed pending cards of the scope's
  open sessions; runs never make a scope working, but their pending cards
  count for the account scope.
- `clearAllData()` stops everything and deletes sessions, attachments,
  memory, spending, drafts, tasks and runs. Exclusions stay.
- A run is a `Session` from `Manager::startRun()` with a `RunSetup`: a
  frame gate, the registry in background mode, `TaskSystemPrompt()`,
  `kTaskMaxSteps`, the task id as cache key, no title. Its record goes to
  `Tasks::saveRun()`, never the index. `actingGuard()` marks the time a tool
  or an approved card runs, so the runner treats new messages made then as
  the agent's own.

## Claude Code client (`agent_claude.h`)

- `ClaudeLauncher` starts `claude` with `QProcess` in `aigram-claude` in the
  temp folder. The environment drops `ANTHROPIC_API_KEY`,
  `ANTHROPIC_AUTH_TOKEN` and every `CLAUDE*` variable except
  `CLAUDE_CONFIG_DIR` (a parent Claude Code session or an API key must not
  change how the subscription runs), sets
  `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1` and the application proxy.
- `ClaudePool` keeps at most three warm connections for ten minutes each.
  `FindClaudeProgram()` is the configured path, else `PATH`, else the usual
  install places. `ClaudeCommand` runs one-off commands (`auth status`,
  `--version`). `ClaudeLimits()` is the latest `rate_limit_event`, not
  stored.

## ChatGPT client (`agent_chatgpt.h`)

- The live account is app-wide, read once and changed only through
  `SaveChatGptAccount()`. `ChatGptAccountChanged()` fires after sign-in,
  sign-out and a terminal refresh error.
- `ChatGptLogin` listens on `127.0.0.1:1455`, opens the browser and also
  accepts a pasted address (`listening()` false when the port is taken). A
  failed sign-in with a saved client id forgets it, so the next attempt
  registers anew. `done` is called once, asynchronously, and may destroy
  the login.
- One token refresh at a time for the app; waiting requests share it. A
  `terminal` error clears the tokens; other errors keep them. An answer
  that is no longer `RefreshStillCurrent()` changes nothing.
- `ChatGptTransport` adds `Authorization: Bearer` only for
  `ChatGpt::IsPlanApiUrl()` (https `api.openai.com:443`); other URLs fail
  without the token. Without a token it answers 401 with `SignedOutBody()`;
  a server 401 refreshes once and retries.
- `ChatGptSignOut()` revokes the refresh token in the background (best
  effort) and clears the tokens.

## Account data

- `Memories`, `Exclusions` and `SpendingTracker` keep the agent values
  `memory`, `excluded` and `spending` in `Storage::Account`; unreadable
  values log and start empty. `Memories::clear(owner)` keeps `nextId`.
- `Exclusions::excluded(peer)` is a hidden peer, or a loaded peer whose
  `History` (created when missing) is in a hidden folder at call time, so
  folder rules also hide chats never opened and chats added later.
- `SpendingTracker::add()` saves two seconds later (or in the destructor).
- `Attachments` stores `a/<sessionId>/<id>` values with the bytes;
  `read()` returns them or nothing. `Drafts` keeps the agent value
  `drafts`, written a second after the last change; replies stay in memory.
- `Storage::Account` writes asynchronously: `readAgentValue()` syncs and
  retries after a write.

## Media access (`agent_media_loader.h`, `agent_media_convert.h`, `agent_files.h`)

- `FindMessageMedia()` describes a message's photo or document.
  `MediaLoader` downloads through the client's loaders (`Full` or
  `Thumbnail` JPEG); it checks no sizes, callers keep to
  `kAutoDownloadBytes` and `kModelMediaMaxBytes`.
- `PrepareImageForModel()` is JPEG within `kImageMaxSide`, never scaled up,
  transparency over white. `DecodeAudio()` (FFmpeg, thread-safe for
  `crl::async`) gives a mono WAV range. `UnzipOffice()` refuses broken
  archives and zip bombs (50 MB per entry, 64 MB total).
- The agent folder is `AIgram Agent/` in the download folder;
  `UniqueAgentFilePath()` sanitizes names and adds ` (N)`;
  `ResolveAgentFile()` accepts only a regular file directly in the folder.
  `MediaSaver` removes a partial file when destroyed.

## Background tasks (`agent_tasks.h`, `agent_task_runner.h`, `agent_notifications.h`)

- `Tasks` keeps the agent values `tasks`, `t/<taskId>` (runs) and
  `bgspending`. `saveRun()` keeps the `kTaskMaxRuns` newest and returns the
  dropped session ids for the caller to delete. `addUsage()` resets
  `spentToday` on a new local day.
- `TaskRunner` watches `Data::Session::newItemAdded()` for message tasks,
  a timer per schedule, and at start the missed events of `catchUp` tasks.
  Runs of one task go one at a time, at most `kTaskMaxParallelRuns` for the
  account and `kTaskMaxQueued` waiting triggers per task. A chat task whose
  chat is hidden never runs. Done non-read calls count toward the hourly
  limit. Limits pause the task (daily until midnight, hourly until
  `HourlyResumeTime()`), the background limit skips runs until the next
  day, and each pause notifies. Finished run sessions stay open until quit,
  so their actions can be undone.
- `ShowTaskNotification()` goes through the native notification manager
  (its click opens the run session; no details while locked) or a toast.

## Agent chat (`agent_chat.h`)

- `AgentChat` makes the agent peer once per account session: a bot
  `UserData` with the fake id `AgentPeerId()` (outside real and name-hashed
  fake ids), the name and photo from `ProvidersState`, and its `History`.
- While `Manager::enabled()` the history holds one local preview item (per
  `rowPreview`); a new item is added before the old one is destroyed, so
  the last message never becomes unknown. Disabled, the history is unpinned
  and cleared. The account scope's waiting cards are the unread count;
  typing is registered every 5 s while a session works. The agent user has
  a known last seen in the past, since the chats list shows typing only for
  such users.
- The pin is local (`ChatState`): put back at `pinnedIndex` whenever the
  server order replaces the pinned list.
