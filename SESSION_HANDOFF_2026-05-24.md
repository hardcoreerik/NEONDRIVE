# NEONDRIVE Session Handoff (2026-05-24)

## Purpose
This file preserves project context for restarting Codex/Claude sessions after a working-directory move.

## Canonical Repo + Working Folder
- Canonical active repo: `X:\AI\NEONDRIVE`
- Remote: `https://github.com/<org-or-user>/NEONDRIVE.git`
- Default branch: `main`

## Verified Git State (today)
- `HEAD` = `12632cd`
- `origin/main` = `12632cd`
- Divergence: `0 ahead / 0 behind` (repo is synced to GitHub on committed history)

## Current Local Uncommitted Changes
Tracked modified files:
- `CLAUDE.md`
- `src/main.cpp`

Untracked local-only files/folders:
- `.claude/`
- `Tab5_Schematics_PDF.pdf` (reference only, should remain untracked)

## What `src/main.cpp` local edits currently do
- Enable SD on Tab5:
  - `BOARD_HAS_SD = true`
- Set Tab5 schematic-verified SD pins:
  - `PIN_SD_SCLK = 43`
  - `PIN_SD_MISO = 39`
  - `PIN_SD_MOSI = 44`
  - `PIN_SD_CS = 42`
- Comments updated to state no GT911 conflict (`SDA=31`, `SCL=32`)

## What `CLAUDE.md` local edits currently do
- Replace previous "SD disabled / verify later" guidance
- Add "SD enabled (2026-05-24)" section with the same verified pin map and verification note

## Important Workspace History Notes
- `X:\NEONDRIVE-TAB5\NEONDRIVE` was previously used and was synced to GitHub.
- `X:\NEONDRIVE-TAB5` (parent repo) and `D:\AI\NEONDRIVE` contain diverged historical states and are not the current canonical workspace.
- Use `X:\AI\NEONDRIVE` going forward to avoid cross-repo confusion.

## Recommended Next-Step Workflow
1. Start new session and explicitly select `X:\AI\NEONDRIVE` as working directory.
2. Reconfirm status:
   - `git status --short --branch`
   - `git rev-parse --short HEAD`
   - `git rev-parse --short origin/main`
3. Continue implementation from current uncommitted Tab5 SD/HAL work.
4. Keep `Tab5_Schematics_PDF.pdf` untracked (reference only).

## Copy/Paste Bootstrap Prompt For New Session
Use this exact prompt in a new Codex/Claude session:

---
Project: NEONDRIVE

Use this folder as the working directory: `X:\AI\NEONDRIVE`

This repo is the canonical workspace now. Please do not use `X:\NEONDRIVE-TAB5` or `D:\AI\NEONDRIVE` unless I explicitly ask.

Context:
- Remote: `https://github.com/<org-or-user>/NEONDRIVE.git`
- Last verified synced commit: `12632cd` on `main`
- Local uncommitted edits are expected in:
  - `CLAUDE.md`
  - `src/main.cpp`
- Untracked local reference file should stay untracked:
  - `Tab5_Schematics_PDF.pdf`

First actions:
1. Run status checks and confirm repo state.
2. Summarize what is currently uncommitted.
3. Continue from existing Tab5 SD/HAL work without discarding local edits.
---

