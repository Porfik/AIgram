# agent/ui map

The agent's widgets (namespace `Agent::Ui`, which pulls in `::Ui` like
`Calls::Group::Ui`): the session view, the agent chat screens, the chat
panel, the palette, cards, settings and model boxes. Read `../AGENTS.md`
first; the session and data contracts are there and in
`../core/AGENTS.md`.

## Modules

- `agent_chat_view.*` — `ChatView`, the session view used by the panel,
  the general agent screen and the palette.
- `agent_chat_items.*` — `ChatItems`, the local history items of a view.
- `agent_feed_format.*` — step, action and card texts.
- `agent_card_box.*` — the "Edit..." / "Details..." box of a card.
- `agent_panel.*`, `agent_panel_widget.*` — the chat panel (an Info
  section) and its top bar setup.
- `agent_section.*` — the general agent screen (sessions list and session
  mode); `agent_session_rows.*` its rows; `agent_sessions_list.*` the
  rename, delete and session menus.
- `agent_profile.*` — the agent profile and its Tasks, Memory and Action
  Journal pages; `agent_userpic.*` the default userpic.
- `agent_history_view.*` — the Sessions / Tasks / Memory tabs;
  `agent_memory_list.*` the memory tab; `agent_tasks_view.*` the tasks tab;
  `agent_task_format.*` the task texts; `agent_queue.*` the "waiting for
  your decision" strip and box.
- `agent_palette.*` — the palette overlay.
- `agent_menus.*` — agent items of chat, message, folder and agent chat
  menus, "Pass to agent", reply and forward of agent items.
- `agent_model_boxes.*`, `agent_model_boxes_common.h` — model profile
  boxes (OpenRouter, "Other provider" and the shared pieces in
  `Agent::Ui::details`); `agent_claude_boxes.*` — Anthropic;
  `agent_openai_boxes.*` — OpenAI and ChatGPT.
- `agent_spending_format.*` — spending and subscription limit texts.
- Styles: `agent.style`, `agent_panel.style`, `agent_general.style`,
  `agent_media.style`, `agent_queue.style`, `agent_tasks.style`,
  `agent_card_box.style`.

## Placement

- Reuse the chat's own widgets wherever they fit: `HistoryView::ListWidget`,
  `ComposeControls`, `SendFilesBox`, the `ui/chat/attach/` previews,
  `DragArea`, the media viewer, "Show in Folder", the native dialogs row.
- Lang keys: `lng_agent_tool_<name>` for steps, `lng_agent_action_*` for
  action rows, `lng_agent_card_title_*` for card titles,
  `lng_agent_task_tool_<name>` for task frames. Every key gets its Russian
  in `ru.strings` (see `../AGENTS.md`).

## Session view

- `ChatView` without a session shows the new-session state and creates the
  session (`Manager::create()`) on the first send. It is a
  `HistoryView::ListWidget` (`Context::Replies`) over `ChatItems::ids()`
  with `ComposeControls` on the agent history (emoji and attachments only)
  and a "jump down" button. It rebuilds
  `BuildChatMessages(BuildFeed(record + LiveTail))` at most every 50 ms and
  sticks to the bottom while the user is there.
- It has its own chat theme (`DefaultChatThemeOn()`): a theme caches the
  wallpaper for one size, and the chat next to the panel uses the window's.
- The bot-menu-style button left of the field (`customMenuText`) shows the
  session's profile and opens the model menu (every profile in two lines:
  title and `ModelDescription()`, plus "Add model..."); choosing calls
  `Session::setProfile()`, or is remembered until the view creates its
  session. The custom stop button (`customStopClicked`) stops the session.
- `ChatItems` keeps one `FakeHistoryItem` per chat message, keyed by
  `ChatMessage::key`, updated in place: user messages are outgoing text
  (image attachments as a photo or an album sharing a `groupedId`, other
  files as "📎 name · size" lines); agent messages are a rich page
  (`Iv::RichPage`: steps in Details blocks, the Markdown text) or plain
  text; cards are a title, targets, rows, quoted text, a status line and
  inline buttons. Buttons carry `agent:<action>:<id>`. Service messages
  are summaries, handoff notes, "Summarizing…", the session spending and
  turn ends (`lng_agent_turn_*`); a failed turn is an incoming message with
  "Retry" (and "Model settings" for `Auth` errors). Item dates never
  decrease.
- A thinking step shows its measured `durationMs`. The session measures
  from the step start to the next text, tool use or turn end, so a
  one-chunk response still counts.
- A run's view (`info.task` set) is read-only: the composer is replaced by
  `lng_agent_run_about`; its cards can still be decided and actions undone.
- Reply and forward of agent items (`AddAgentReplyAction()`,
  `AddAgentForwardAction()`): the reply goes into the compose controls and
  the request carries `replyTo`, `replyQuote` and the replied text as a
  hidden block; forward opens `ShareBox` and sends a plain copy of the text.
- `passMessages()` puts passed messages into the agent history's forward
  draft at topic root 0 (a fake topic root can't show a forward bar); the
  next request carries them as untrusted data and mentions their chat.
- `setMentionsProvider()` adds peers that count as named for the taint
  rule. `setComposerOnTop()`, `naturalHeightValue()` and
  `setBottomRadius()` serve the palette.
- Drafts (`../agent_drafts.*`): the composer text is kept per key (a
  session id, `new`, `new:<peer>`, `palette`) and moves to the created
  session on the first send.
- Attachments: the attach button, drag and drop, and pasting build a
  `Ui::PreparedList` for the standard `SendFilesBox` (the user's own chat as
  `toPeer`, no send menu); its confirm sends the caption and the files as
  `AttachmentInput`s; closing it puts the caption back. Limits show
  `lng_agent_attach_too_many` / `_too_big`.

## Cards

- Card messages show buttons by state: the main one ("Send (N)", "Confirm
  (N)", "Download (N)"...), the alternate ("To draft", "Append"),
  "Edit..." or "Details..." when the box adds something, "Decline";
  "Continue in the general agent" for a handoff (`Done` from the start);
  "Show in Folder" for a done download. `Expired` reads
  `lng_agent_card_expired`, an auto-approved run card
  `lng_agent_card_auto_task`.
- `ShowCardBox()` shows everything a card has: task rows, the method with
  "Show parameters", change rows with "Was: ...", send-files previews,
  download files with the total, targets as checkable chat rows (only while
  pending with more than one), the editable text, the option and "allow
  until the end of the request". Running and final cards are read-only.
- `SendFiles` previews are split into blocks like `SendFilesBox` does and
  take no mouse input. Images decode with `Images::Read()`.

## Chat panel

- An Info section (`Info::Section::Type::Agent`, keyed by the peer) made by
  `MakePanelMemento()`; `TogglePanel()` and `PanelAvailable()` (false for a
  hidden peer; `PanelAvailabilityChanges()` fires on enable and exclusion
  changes, and an open panel of a newly hidden chat closes).
  `RegisterShownPanel()` drives the ✦ button's active state.
- It opens in the third column whenever it fits without a resize (turning
  the column on); only a narrow window shows a layer, which moves to the
  third column on widening with the session, list state and unsent text.
  A new panel starts in the new-session state; a saved memento keeps its
  session.
- No header of its own: the title, the status ("waiting for your
  decision…", "typing…", else the agent's name) and the "new session",
  "history" and "⋮" buttons live in the Info top bar (`SetupPanelTopBar()`).
  The content lies over the Info scroll area, which only holds an empty
  widget reporting the height, so the Info scroll never moves.
- "⋮" (`ShowSessionMenu()`: rename, delete) is disabled, not hidden, in the
  panel without a session: an `Info::TopBar` button keeps its place when
  hidden.

## Agent chat and general screen

- `../agent_chat.*` owns the agent peer; its dialogs row shows one local
  preview item. The general agent screen (`agent_section.*`) is a
  `Window::SectionWidget`: the list mode has a chat-like top bar, "＋ New
  session" and `SessionRows` (dialog-like rows with the waiting cards as an
  unread badge); the session mode has the session title and a `ChatView`.
  `ShowGeneralAgent*()` open its modes. `activeChat()` is the agent
  history, so the row is highlighted and the third column shows the
  profile. It re-applies mode visibility in `showFinishedHook()`, since
  the show animation shows every child.
- The handoff button calls `ShowGeneralAgent()` with a `HandoffDraft`; the
  first send passes the chat transcript as untrusted text, and starting
  another session drops it.
- `HistoryView` has "Sessions", "Tasks" and "Memory" tabs; the compact
  `SessionRows` show a delete icon on hover. `MemoryList` edits the owner's
  facts and rebuilds on `Memories::changes()`.
- `TasksView` lists the scope's tasks or shows one with editable rows
  (instruction, model, limits, notifications), pause/resume, delete and its
  runs; a run click opens the run session. `QueueStrip` shows the number of
  waiting cards of `TaskRunner::waiting(scope)` and has zero height when
  nothing waits.
- The agent profile (`ProfileWidget`) has the default model, Tasks,
  Memory, Action Journal and Agent Settings. The journal reads every indexed
  session and run (`BuildJournal(..., ActionToolNames(), 200)`), offers
  "Undo" while the session is open, and opens the session on click. "Edit"
  opens `ShowEditAgentProfileBox()` (photo saved as a 320 px JPEG, name).

## Palette

- `ShowPalette()` (`Ctrl+Shift+Space`, false while the agent is disabled)
  shows a `Ui::LayerWidget` with an account-scope `ChatView`, the composer
  on top and the `palette` draft; it is as high as its content, every
  opening starts without a session, "open in the agent" moves to the
  general screen, and closing leaves the session working.
- The palette knows the chat open when it opened (unless hidden or the
  agent chat): the hint names its id (and `topic_id`) and says its title is
  written by others and comes from `get_peer_info`; the title never goes
  into the trusted hint. The chat joins `mentions`.
- `PassToAgent()` opens the palette with messages of one visible chat
  (albums expanded, sorted); "Pass to agent" in the message menus and "To
  agent" in the selection bar use it.

## Settings and model boxes

- The "AI Agent" page (`settings/sections/settings_agent.cpp`): a cover
  (userpic, name, "Change name and photo"), "Models" (a row per profile
  with its roles, "Add model"), "Model roles" (default, background, media),
  "Agent" (general instructions, Memory, Tasks, Action Journal), "Chat
  list" options, "Spending and limits", "Hidden chats", "Agent folder",
  "Export sessions for debugging" and "Delete agent data"
  (`Manager::clearAllData()`). The page reloads on `ProvidersChanged()`;
  every box saves through `UpdateProviders()` (load, change, save).
- The spending page: today, 30 days, all time, prompt cache share, "Show
  spending in sessions", subscription limits (Claude rows from
  `ClaudeLimits()`, "ChatGPT usage"), the background daily limit and
  "Reset statistics".
- `ShowAddModel()` offers OpenRouter, Anthropic (`ShowAnthropicChoice()`:
  Claude subscription or API key), OpenAI (`ShowOpenAiChoice()`: ChatGPT
  subscription or API key) and "Other provider". Keys are asked only when
  the service has none (`ServiceKey()`) and are checked live (401/403 marks
  the field). Choosing a model copies its id, window, abilities and prices.
  Only add flows have the "Default model" checkbox.
- OpenRouter: the model list is `/models` (cached ten minutes, tool models
  only, `SearchModels()`); "OpenRouter providers" are the routes with tools
  (unchecked ones are ignored, never all); changing the model clears
  `ignoredRoutes`.
- Claude subscription (`agent_claude_boxes.*`): find `claude` (else show
  `ClaudeInstallCommand()`), check the version and `claude auth status`,
  sign in with `claude auth login --claudeai` (the binary opens the browser;
  AIgram never reads its output for codes), then `Claude::ModelsQuery`.
- ChatGPT subscription (`agent_openai_boxes.*`): "Continue with ChatGPT"
  starts a `ChatGptLogin`, offers reopening the browser and pasting the
  address; models come from `ResponsesModelsRequest()` through
  `Manager::transportFor()`. The edit box has the account, "Sign out" and
  "Manage usage".
- Deleting a profile asks first and clears the roles that named it.
- A failed turn of a profile shows "Model settings" for `Auth` errors; for
  the ChatGPT plan, the limit error offers "Manage usage" and "Change model"
  instead.
