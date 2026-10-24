# Claude Code Pointer

Read `AGENTS.md` and treat it as the canonical repository-wide instructions,
with the AIgram notes below taking precedence.

## AIgram fork notes

This checkout is AIgram, a fork of Telegram Desktop with a built-in AI agent
in `Telegram/SourceFiles/agent/` (start with its `AGENTS.md`; the product
plan is `docs/ai-agent-plan.md`). The fork merges `upstream`
(telegramdesktop/tdesktop) and keeps upstream's own agent files unchanged,
so those merges stay clean.

- Upstream's AI workflow does not exist here: there is no `../ai-tdesktop`
  queue, no Codex CLI and no crash report server. Skip the "AI Tasks",
  "Working from Codex on Windows + WSL", "Dependency backports" and "Build
  output locks" sections of `AGENTS.md` and the `Task:` commit format. Don't
  use the `continue`, `perform-task`, `process-inbox`, `code-health`,
  `dependency-watch`, `cross-review`, `crash-reports-process`, `release` and
  `icon` skills and commands. `rebase` works for plain branch integration.
- Linux builds run in the Docker image of `docs/building-linux.md`. The
  agent's unit tests are the `td_agent_test` target, built only on request.
  CI builds with warnings as errors, so keep agent code warning-free.
- An upstream merge keeps every fork-side hunk; check the hooks listed in
  `Telegram/SourceFiles/agent/AGENTS.md` after it.
- `.github` holds only `linux.yml`, `mac.yml`, `win.yml` (one x64 job) and
  `release.yml`; the rest of upstream's CI and repository automation is
  deleted. When a merge brings changes to a deleted file, keep it deleted.
  Releases are `v<upstream version>-aigram.<n>` tags; upstream's tags are not
  pushed to the fork.
- The repository is public: no local paths, keys, tokens or private session
  details in tracked files.
