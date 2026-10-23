# AIgram

AIgram is an unofficial fork of [Telegram Desktop][tdesktop] with a built-in
AI agent. You describe a task in plain words, and the agent reads, searches
and acts in your chats on your behalf. Anything other people would see
waits for your confirmation first.

AIgram is not affiliated with Telegram. It is the regular Telegram Desktop
client plus the agent, and it follows upstream development.

## The agent

- **General agent.** A local "✦ Agent" chat pinned in your chat list. It can
  work with every chat you didn't hide from it, open settings and run
  background tasks. Each conversation is a session you can reopen later.
- **Chat agent.** The ✦ button in a chat's top bar opens a side panel bound
  to that chat only. It reads that chat, writes drafts into its message field
  and hands bigger requests over to the general agent.
- **Command palette.** <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Space</kbd>
  opens the general agent over the window for a quick one-off request.
  "Pass to agent" in the message menu hands it the selected messages.
- **What it can do:** read and search history across chats; look at photos
  and videos, listen to voice messages, read documents; send, forward, edit
  and delete messages; reactions, pins, mute, archive and read state;
  folders; files in its own folder; group and channel administration;
  contacts and blocking; your profile; Telegram's own summaries and
  translation; and, with a confirmation for every change, any other
  Telegram API method.
- **Background tasks.** Rules such as "when a message mentioning the
  invoice arrives in these chats, forward it to Saved Messages" or "every
  morning, summarize the channels in this folder". Tasks run while the
  client is open, within the actions you allowed them, with daily and
  hourly limits. Everything else waits in a decision queue.
- **Memory and instructions.** Every chat has its own memory, the general
  agent has another one, and your general instructions apply to all of
  them. You can review and edit every fact.

## Staying in control

- Actions others can see (sending, editing, admin changes, API writes) show
  a card first. You can edit the text, uncheck chats or decline. One card
  can allow the same action for the rest of the request.
- Reversible actions (mute, archive, folders, memory) run right away and
  get an "Undo" button. The action journal in the agent's profile lists
  the agent's actions.
- Text that other people wrote is passed to the model as data, never as
  instructions. After the agent has read such text, it can't send anything
  to a chat you didn't name in the request without asking you.
- You can hide chats and folders from the agent completely.
- Logging in, passwords, payments, Stars, gifts and account deletion are
  never available to the agent.
- Model keys and tokens are stored encrypted in the local client data. The
  agent's sessions, memory and tasks stay on your computer; only what the
  agent sends to the model you chose (and to Telegram, when it uses
  Telegram's own AI features) leaves it.

## Models

Add one or more model profiles in Settings → AI Agent:

| Service | How it connects |
|---|---|
| OpenRouter | API key, any model with tool support |
| Anthropic | API key, or your Claude subscription through the Claude Code CLI you install and sign in to |
| OpenAI | API key, or your ChatGPT plan through "Sign in with ChatGPT" |
| Other provider | Any OpenAI-compatible endpoint, local servers included |

Separate profiles can serve as the default model, the background task model
and the media model (for media the main model can't handle). The settings
show what the agent spent, today and in total.

The agent's interface is available in English and Russian.

## Building

AIgram builds exactly like Telegram Desktop: follow the instructions for
your system, cloning this repository instead of `telegramdesktop/tdesktop`.
You need your own `api_id` and `api_hash` ([how to get them][api_credentials]).

* [Windows (32-bit and 64-bit)][win]
* [macOS][mac]
* [GNU/Linux using Docker][linux]

The agent's unit tests are a separate target: build and run
`td_agent_test`.

There are no prebuilt releases yet.

## For contributors

- `Telegram/SourceFiles/agent/` holds the agent. The pure core in
  `agent/core/` has no client dependencies and is covered by
  `td_agent_test`.
- `Telegram/SourceFiles/agent/AGENTS.md` and its submaps describe the code
  contracts. `docs/ai-agent-plan.md` is the product plan and decision log
  (in Russian).
- Changes to upstream files stay one call wide, so the regular merges from
  upstream stay easy.

## License

AIgram is published under GPLv3 with the OpenSSL exception, like Telegram
Desktop; the license is available [here][license].

## Third-party

* Qt 6 ([LGPL](http://doc.qt.io/qt-6/lgpl.html)) and Qt 5.15 ([LGPL](http://doc.qt.io/qt-5/lgpl.html)) slightly patched
* OpenSSL 3.2.1 ([Apache License 2.0](https://openssl-library.org/source/license/apache-license-2.0.txt))
* WebRTC ([New BSD License](https://github.com/desktop-app/tg_owt/blob/master/LICENSE))
* zlib ([zlib License](http://www.zlib.net/zlib_license.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* GYP ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
* Ninja ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
* OpenAL Soft ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
* Opus codec ([BSD License](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Guideline Support Library ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
* Range-v3 ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
* Vazirmatn font ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))
* Emoji alpha codes ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
* xxHash ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))
* QR Code generator ([MIT License](https://github.com/nayuki/QR-Code-generator#license))
* CMake ([New BSD License](https://github.com/Kitware/CMake/blob/master/Copyright.txt))
* Hunspell ([LGPL](https://github.com/hunspell/hunspell/blob/master/COPYING.LESSER))
* Ada ([Apache License 2.0](https://github.com/ada-url/ada/blob/main/LICENSE-APACHE))
* zxcvbn-c ([MIT License](https://github.com/tsyrogit/zxcvbn-c/blob/master/LICENSE.txt))
* FrequencyWords by Hermit Dave ([Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/))

[//]: # (LINKS)
[tdesktop]: https://github.com/telegramdesktop/tdesktop
[api_credentials]: docs/api_credentials.md
[license]: LICENSE
[win]: docs/building-win.md
[mac]: docs/building-mac.md
[linux]: docs/building-linux.md
