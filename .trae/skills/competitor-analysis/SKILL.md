---
name: competitor-analysis
description: Analyze a given Windows podcast/feed-reader competitor and propose concrete LiteTune features worth borrowing. Use when the user shares a competitor repo URL or asks how we compare to another app. Do not use for implementing the proposed features — that is a separate step.
---

# LiteTune Competitor Analysis

LiteTune is a **285KB, single-file, zero-dependency Win32 podcast player** in pure C. Every borrowed idea must survive this filter: if it costs a DLL, a runtime, a registry write, or ~100KB, it fails.

## Workflow

1. Fetch the competitor's README and any visible docs/screenshots. Do not guess.
2. Summarize it under these dimensions (Chinese, concise, under 600 words):
   - **Tech / size / portability**: stack, exe size, install method (MSIX/cert vs portable exe), registry usage, where config & cache live.
   - **Core positioning**: what problem it solves, who it's for.
   - **Feature set**: feed formats (RSS/Atom/JSON Feed), playback, download/cache, OPML, search, auto-update, notifications, sleep timer.
   - **Distinctive UX**: tray behavior, flyouts, shortcuts, any clever Win32 tricks.
   - **Storage/config**: single file? folders? ini/json/sqlite?
   - **Weaknesses / trade-offs** the competitor accepts.
3. Compare against LiteTune in a compact table (a few rows: size, install, offline, sleep timer, etc.).
4. Split findings into three buckets:
   - **已落地** — already implemented in LiteTune (mention to avoid re-suggesting).
   - **可吸收** — concrete features that fit the minimal philosophy, with an estimate of size/cost and which competitor inspired each.
   - **不抄** — features that contradict the single-file / zero-dependency / no-registry philosophy, with a one-line reason.
5. Propose a **priority order** for the absorbable items, cheapest/most-visible first.

## LiteTune-specific invariants (do not break)

- Pure C + Win32 API, GCC, `-mwindows -municode`, no third-party libs.
- Single exe, no install, no registry writes, no external DLLs.
- Config: embedded in exe tail (PE overlay, magic `LTST`) **or** external `feeds.ini`.
- Cache lives under `%TEMP%\LiteTune\` (or a user-picked dir); auto-clean targets `*.mp3` only.
- Occam's razor: prefer the smallest change that delivers the value.
- Compile with `-Wall -Wextra` zero warnings; hand off to user for testing (no screenshots).

## Output format

- Brief positioning statement of the competitor.
- Comparison table.
- Three buckets (已落地 / 可吸收 / 不抄).
- Suggested priority list for the "可吸收" bucket — that becomes the user's todo input.
