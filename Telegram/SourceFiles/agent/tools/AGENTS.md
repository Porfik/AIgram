# agent/tools map

The tools the model calls: one registry per session, tool groups, and the
shared helpers that read and change Telegram data for them. Policy rules
are in `../core/AGENTS.md` ("Scope and policy", "Cards"); read
`../AGENTS.md` first.

## Modules

- `agent_tool_table.h` — `ToolEntry`, `PendingCall`, the schema helpers and
  shared helpers (`ResolveUserRefs()`, `ParseMessageIds()`, `PeerTitle()`,
  `PeerTarget()`, `ParseLocalTime()`, `MtpErrorText()`,
  `WhenFullLoaded()`, `ForEachTarget()`).
- `agent_tools.cpp` — `ToolRegistry`, `load_tools` and the catalog table
  `CatalogInfo()`.
- `agent_tools_<group>.cpp` — `<Group>Tools::Entries()`: `chat` (one
  chat), `general` (chat list, settings navigation, handoff), `actions`
  (card actions), `chat_state`, `folders`, `memory`, `media`, `files`,
  `tasks`, `admin_members` and `admin_chat` (group `admin`), `contacts`,
  `profile`, `telegram_ai`, `mtproto` (the raw API; `api.tl` is the
  resource `:/agent/api.tl`).
- `agent_tool_format.*` — `SerializeMessage()` and the message, peer and
  media texts for the model.
- `agent_tool_rights.*` — admin right and member restriction names (any
  case on input; an unknown one fails listing the valid names).

## Placement

- A new tool is an entry in the group file it belongs to plus a row in
  `CatalogInfo()` (snippet of at most 60 characters, section) and in the
  table at the end of this map. A new group also needs its `load_tools`
  name and `GroupWhen()` line.
- Tools never ask the user in text: a visible change asks a card through
  the registry.

## Registry

- `ToolRegistry` is the session's `ToolExecutor`. `tools()` lists what the
  gate makes available (with `generalDescription` / `generalSchema` in the
  account scope) and whose media `needs` are met. Group entries are listed
  after `load_tools` loads them, after all other tools, in load order, so
  earlier specs never move. A call of a group tool that is not loaded loads
  its group when the scope may use it (the catalog shows every tool).
- Every call goes through `PolicyGate::check()`, also for a tool never
  offered. Before that the input goes through `CoerceToolInput()` (models
  send `["244"]`; the record keeps the call as sent), required parameters
  are checked ("Missing required parameter ... Call it as <signature>"),
  and an unknown name fails with `NearestToolNames()` suggestions.
- A repeated `Read` call (same tool key and compact input earlier in the
  turn, `load_tools` exempt) fails without running and points to the
  earlier call; any non-`Read` call or `beginTurn()` clears the reads.
  `beginTurn()` also clears the turn's seen messages (`PendingCall::seen()`
  / `markSeen()` let tools collapse messages the model already saw).
- Chat parameters: in the chat scope tools have none and get the scope
  peer. In the account scope the registry adds them by `ChatParam`: `One`
  (`chat`), `OptionalOne`, `Many` (`chats`, 1–100), `OptionalMany` (omitted
  means all chats; a `chat` counts as a list of one). `ResolveChatRef()`
  takes an id, `@username`, a `t.me/` link or `me` / `self`, only for peers
  already loaded; unknown or invisible chats fail the call. Usernames that
  are not loaded (at most 10) are resolved with `contacts.resolveUsername`
  first; a call cancelled meanwhile never runs.
- `ToolEntry::forPeer` limits the chats a tool works with: in the chat
  scope such an entry is not listed or loaded for other peers; in both
  scopes a rejected peer fails with `forPeerError`.
- `ToolEntry::gateRequest` replaces the name and policy in the gate check
  with a key, policy and peers of this call (`call_method <method>`), or
  fails before the gate; allowances then cover that key only.
- Results are compact JSON capped with `CapToolResult()`. Every successful
  result in the chat scope, and every result of an `untrusted` entry in the
  account scope (anything other people wrote: messages, names, titles,
  bios), is wrapped with `WrapUntrusted()` and taints the gate. The registry
  never grants allowances itself: only a confirmed card does.
- A tool finishes once: `done`, `doneMedia` (media blocks with the result;
  `untrusted` taints also for media only) or `fail`, or `finishCard` after
  `confirm`. `confirm(card, execute)` shows the card; when the gate allowed
  the call the card is recorded `autoApproved` and `execute` runs at once
  (not for `CardAlwaysAsks()`). The registry finishes declined cards. After
  an approved card a tool may finish with its own result, and the card is
  recorded `Done` or `Failed`.
- `decide()` applies the decision; "allow for the turn" passes the call's
  peers and `CardAllowanceKeys(card, true)` to the gate, so a later card is
  approved without asking only when `coversTargets()` holds for all its
  keys. `cancel()` of a waiting call declines its card without a result.
- Reversible tools never ask by themselves: when the gate says `Confirm`
  (outside a run's frame) the registry asks an `Action` card first. A
  `taintedAsks` tool (`memory_add`, `memory_update`) runs at once until the
  gate is tainted, then asks its own `Change` card.
- `undoable(undo)` registers the undo of a successful reversible action;
  `canUndo()` holds until an undo succeeds. Undo closures live in memory,
  so nothing is undoable after a restart.
- Media: every call gets `mainAbilities` (the turn's provider while busy,
  else the session's; `files` is true for OpenRouter) and
  `helperAbilities` (the media provider). `askMediaModel()` sends
  `HelperRequest()`, reports `helperUsage()` and is cancelled with the call.
- `load_tools` takes a group name or a few English words: a query ranks the
  available group tools with `RankToolDocuments()` (at most 5), loads every
  group with a match and returns them; no match fails listing the groups.
  A group with no tool for the scope (or `forPeer`) fails to load.
- `catalog()` is `FormatToolCatalog()` of the available entries with met
  needs and the available groups; in background runs every tool is listed
  without groups and `load_tools`.
- Background runs (`setBackground()`): run providers' abilities,
  `PendingCall::notify`, `expire()` finishes a waiting card `Expired`.
  Interactive-only tools have `background` false: `load_tools`, memory
  tools, `find_setting`, `open`, `continue_in_general`, `set_draft`,
  `draft_files`, the `tasks` group.

## Hidden chats

- A reference to a hidden chat and `DenyReason::Excluded` fail with "The
  user hid this chat from the agent." and ask the model to tell the user
  and not look another way.
- Lists and searches skip hidden chats silently (`list_chats`, global
  `search_messages`, folder lists). In runs they also skip chats outside
  the frame (`inFrame()`), also in `list_contacts`, `list_blocked` and
  `get_peer_info`'s linked chat; `forward_messages` fails for a
  `from_chat` outside it.
- Hidden folders are skipped by `list_folders`, refused by folder tools and
  kept in place by `reorder_folders`.
- Members and senders of a visible chat stay visible. A person whose
  private chat is hidden is refused in `users` parameters
  (`ResolveUserRefs()`) and skipped by contact lists.

## Results the UI reads

- Action entries (`action`, shown as action rows; `ActionToolNames()`)
  return `{"done": [{"chat", "title"}], "failed": [{"chat", "title",
  "error"}]}`; folder tools add `folder`, `reorder_folders` returns
  `order`. The journal's `failed` and the UI rows depend on this shape.
- Memory tools return `{"fact": {"id", "text"}, "count", "limit"}`;
  `save_files` adds `file` and `path` per done entry; `rename_contact` adds
  `name`; task tools return `task` with `id` and `title`.
- Every result built from a media model answer has `"media_model": true`.

## Groups

- Messages go to the model as id, local time, sender, Markdown text, reply
  id and a short media description (`SerializeMessage()`, cut at
  `maxTextChars` with `truncated` and `length`). Reading never marks
  anything read.
- `search_messages` takes up to 5 `queries` (or `query`), `media`, dates,
  `chat_types`, `context` neighbors and `mode: counts`; words are optional
  with media or dates. Several chats run one request at a time; a failing
  chat goes to `failed` and the rest go on. Hits are deduplicated, newest
  first, with `MatchSnippet()` snippets; hits seen earlier in the turn are
  listed in `seen` instead.
- `read_history` also takes `ids` and `from_date`; other reads cut each
  text at 1500 characters. `view_media` takes up to 10 `message_ids` (videos
  as covers then).
- Media tools take `message_id` (with `chat` in the account scope) or
  `attachment`, exactly one. Above `kAutoDownloadBytes` they ask a reading
  `Download` card; above `kAttachmentMaxBytes` they refuse. `view_media`
  and `listen_audio` send media on the `Main` route or ask the media model
  on `Helper`; `transcribe_voice` uses Telegram transcription (a
  `Transcribe` card spends a trial without Premium, otherwise it suggests
  `listen_audio`); `read_file` pages text and office files and sends PDFs.
- `files` group: `save_files` to the agent folder (undo deletes; over
  `kAutoDownloadBytes` a `Download` card without undo), `send_files` (a
  `SendFiles` card, sent like the client's own send box),
  `draft_files` (chat scope: opens the send box, the user sends),
  `list_agent_files`.
- `tasks` group works within the scope (a chat sees only its tasks);
  `create_task` / `update_task` ask `CreateTask` / `UpdateTask` cards;
  `pause_task` and `delete_task` are undoable actions; `notify` exists only
  in runs.
- `admin`, `contacts`, `profile`: reads run at once; every visible change
  asks a `Change` card (title `lng_agent_card_title_*`, rows with previous
  values), then runs per target through `ForEachTarget()` (one at a time,
  300 ms apart, `FLOOD_WAIT` / `PEER_FLOOD` stops the rest). MTP results
  are applied like the client's own requests (`applyUpdates()`,
  `processUsers()` / `processChats()`). Admin tools work only in groups and
  channels and need the right they use; basic groups support only what
  Telegram allows there (no bans, restrictions, member history or slow
  mode). Contact tools work only with users.
- `telegram_ai` (Read, also in runs): `summarize_message` and `translate`
  (untrusted) and `ai_compose` use Telegram's own AI; limits come back as
  errors.
- `mtproto` (account scope, interactive only): `find_methods`,
  `describe_method`, `call_method`. All three `call_method` parameters are
  required in the schema because some OpenRouter providers drop optional
  arguments; only writes enforce `description`. Its `gateRequest` refuses
  unknown, forbidden and redirected methods, Stars parameters and encoding
  errors; the policy is `Read` or `Visible` by `ClassifyMethod()`, always
  `outbound`, the peers every chat the parameters name. Writes ask a
  `Method` card. The request goes through `MTP::Instance::sendSerialized()`
  to the main DC; users, chats and updates of the result are applied under
  `actingGuard()`, and the result goes through `ScrubExcludedPeers()`.

## Tool table

| Tool | Group file | Risk | Scopes | Chat param | Notes |
|---|---|---|---|---|---|
| `get_context` | chat | Read | both | None | |
| `read_history` | chat | Read | both | One | `ids`, `from_date` |
| `search_messages` | chat | Read | both | OptionalMany | queries, media, dates, counts |
| `get_peer_info` | chat | Read | both | One | |
| `list_members` | chat | Read | both | One | |
| `list_chats` | general | Read | general | None | |
| `find_setting` | general | Read | general | None | `RankSettings()` |
| `open` | general | Read | general | OptionalOne | settings link, chat, message |
| `continue_in_general` | general | Read | chat | None | `Handoff` card |
| `send_message` | actions | Visible, outbound | general | Many | `Send` card |
| `set_draft` | actions | Reversible, action | chat | None | `Draft` card when the field has text |
| `forward_messages` | actions | Visible, outbound | general | Many | |
| `edit_message` | actions | Visible, outbound | both | One | own messages |
| `delete_messages` | actions | Visible | both | One | own messages |
| `react` | actions | Visible | both | One | |
| `pin_message` | actions | Visible | both | One | `unpin` |
| `mark_read` | chat_state | Visible | both | Many | `Change` card: read receipts |
| `set_mute` | chat_state | Reversible, action | both | Many | |
| `set_archived` | chat_state | Reversible, action | both | Many | |
| `set_pinned` | chat_state | Reversible, action | both | Many | |
| `list_folders` | folders | Read | both | None | |
| `create_folder` | folders | Reversible, action | general | Many | |
| `rename_folder` | folders | Reversible, action | general | None | |
| `set_folder_chats` | folders | Reversible, action | both | Many | |
| `reorder_folders` | folders | Reversible, action | general | None | |
| `delete_folder` | folders | Visible | general | None | `DeleteFolder` card |
| `memory_add` | memory | Reversible, action | both | None | `taintedAsks` |
| `memory_update` | memory | Reversible, action | both | None | `taintedAsks` |
| `memory_remove` | memory | Reversible, action | both | None | |
| `read_chat_memory` | memory | Read | general | One | untrusted |
| `view_media` | media | Read, needs images | both | OptionalOne | untrusted |
| `listen_audio` | media | Read, needs audio | both | OptionalOne | untrusted |
| `transcribe_voice` | media | Read | both | One | `Transcribe` card |
| `read_file` | media | Read | both | OptionalOne | untrusted |
| `save_files` | files | Reversible, action | both | One | `Download` card when big |
| `send_files` | files | Visible, outbound | general | Many | `SendFiles` card |
| `draft_files` | files | Read | chat | None | |
| `list_agent_files` | files | Read | both | None | |
| `create_task` | tasks | Visible | both | None | `CreateTask` card |
| `update_task` | tasks | Visible | both | None | `UpdateTask` card |
| `list_tasks` | tasks | Read | both | None | |
| `pause_task` | tasks | Reversible, action | both | None | |
| `delete_task` | tasks | Reversible, action | both | None | |
| `notify` | tasks | Read | background | None | not in a group |
| `list_invite_links` | admin_chat | Read | both | One | |
| `create_invite_link` | admin_chat | Visible, outbound | both | One | |
| `edit_invite_link` | admin_chat | Visible, outbound | both | One | |
| `revoke_invite_links` | admin_chat | Visible | both | One | |
| `edit_chat_info` | admin_chat | Visible, outbound | both | One | |
| `set_chat_photo` | admin_chat | Visible, outbound | both | One | |
| `set_chat_permissions` | admin_chat | Visible | both | One | |
| `create_chat` | admin_chat | Visible, outbound | general | None | |
| `join_chat` | admin_chat | Visible | general | None | |
| `leave_chat` | admin_chat | Visible | both | One | |
| `add_members` | admin_members | Visible | both | One | |
| `remove_members` | admin_members | Visible | both | One | `ban`, `until` |
| `unban_members` | admin_members | Visible | both | One | |
| `restrict_members` | admin_members | Visible | both | One | supergroups |
| `promote_admins` | admin_members | Visible | both | One | |
| `demote_admins` | admin_members | Visible | both | One | |
| `delete_chat_messages` | admin_members | Visible | both | One | for everyone |
| `delete_member_history` | admin_members | Visible | both | One | supergroups |
| `list_join_requests` | admin_members | Read | both | One | |
| `answer_join_requests` | admin_members | Visible | both | One | |
| `list_contacts` | contacts | Read | general | None | |
| `list_blocked` | contacts | Read | general | None | |
| `add_contact` | contacts | Visible, outbound | both | One | |
| `rename_contact` | contacts | Reversible, action | both | One | |
| `delete_contacts` | contacts | Visible | both | Many | |
| `set_blocked` | contacts | Visible | both | Many | |
| `update_profile` | profile | Visible, outbound | general | None | |
| `set_profile_photo` | profile | Visible, outbound | general | None | |
| `list_emoji_statuses` | profile | Read | general | None | |
| `set_emoji_status` | profile | Visible | general | None | Premium |
| `summarize_message` | telegram_ai | Read | both | One | untrusted |
| `translate` | telegram_ai | Read | both | OptionalOne | untrusted |
| `ai_compose` | telegram_ai | Read | both | None | |
| `find_methods` | mtproto | Read | general, interactive | None | |
| `describe_method` | mtproto | Read | general, interactive | None | |
| `call_method` | mtproto | per method | general, interactive | None | `Method` card for writes, untrusted |
| `load_tools` | registry | Read | both | None | |

Visible tools of `admin_*`, `contacts` and `profile` and `mark_read` ask a
`Change` card.
