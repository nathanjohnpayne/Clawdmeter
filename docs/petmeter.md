# Petmeter — what this fork adds

Clawdmeter shows one plan's usage. Petmeter shows several, switched on the
device, with each provider getting its own palette, typeface and mascot so a
glance tells you which one you are looking at.

This document covers everything the fork adds on top of
[HermannBjorgvin/Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter).
For upstream's architecture — the HAL, the board ports, the splash engine —
see the root `CLAUDE.md`.

---

## 1. Provider collection (host side)

### The seam

`daemon/collectors/` defines what the daemon requires of a provider:

```python
class Collector(Protocol):
    provider: str
    async def collect(self) -> UsageSnapshot | None: ...
```

`UsageSnapshot` is the normalized view — `windows` keyed by `"5h"` / `"7d"`,
plus `model_windows` for plans that meter specific models separately. The
daemon consumes it without knowing which vendor produced it.

`collect` is **async** because the daemon loop is asyncio-driven (bleak owns
the BLE connection) and collectors block — HTTP, and rollout logs that run to
hundreds of megabytes. `CodexCollector` offloads via `asyncio.to_thread`
rather than stalling the device link.

### Free-ride credentials

Inherited from upstream and extended to every provider: **a collector never
refreshes an OAuth token.** The CLI that owns the token does all refreshing; a
collector reads whatever is currently stored and reports `None` when it is
dead, which the device renders as "No data". Refreshing races the owning CLI
and can invalidate its session. See `daemon/tests/test_freeride.py`.

### Claude

Unchanged in substance, but the header parsing moved out of the two Python
daemons into `daemon/collectors/claude.py`. They had carried byte-identical
copies that had **drifted**: the Windows one hardened `_billing_period_info`
after a field report (#104) where an out-of-range reset header raised
`OSError(22)` and killed the poll loop, and the macOS copy never got the fix.
The shared version is the hardened one.

### Codex

Two sources, in preference order:

1. `GET https://chatgpt.com/backend-api/wham/usage` with the OAuth token from
   `~/.codex/auth.json`. Live, one request. **Undocumented** — it is what the
   Codex CLI and CodexBar use, and it can change without notice, which is why
   (2) is not optional.
2. The `rate_limits` object the CLI records into its session rollout JSONL
   under `~/.codex/sessions`. No network, but only as fresh as your last
   Codex session — reported honestly via `UsageSnapshot.stale_seconds`.

**The trap in this data:** account limits and per-model limits sit side by
side. A Codex Pro account meters one account-wide weekly window — no 5-hour
window at all — plus a `GPT-5.3-Codex-Spark` bucket with its own 5h and
weekly. They are unrelated numbers. Reading the newest rollout record blindly
returned **0%** off the Spark bucket while the account sat at **78%**. Both
readers select `limit_id == "codex"` explicitly, and per-model quotas live in
`model_windows`, never merged into `windows`.

Also: `primary` / `secondary` are **positional, not semantic**. A Pro plan
puts its 7-day window in `primary` with `secondary` null; a per-model block
puts 5h there. Always classify by duration.

---

## 2. Wire format

Claude stays at the top level so an older firmware ignores everything new.

| Field | Meaning |
|-------|---------|
| `x` | A second provider's usage, same field names nested one level down |
| `has_s` / `has_w` | Whether that panel has a real quota behind it. A Codex Pro account has no 5-hour window; a slot with nothing behind it renders `-` with an empty bar and no reset line, because a zeroed panel is a lie about a limit that does not exist |
| `sm` / `wm` | Pill overrides — `"Spark"`, `"Overall"` — for a panel showing one model's slice rather than the account's |
| `ws` | Weekly scoped-model limits, `[{"n":"Fable","p":75}, ...]`, which the Weekly card flips between (from the merged fable branch) |

The daemon sends **every** provider it can read in one payload rather than the
device requesting one. The device holds a slot per provider and picks by mode,
so switching is instant instead of waiting for the next poll.

**Where the merge goes.** It belongs in `poll_active()`, not
`poll_active_payload()`. The latter reads like the entry point and is not one
— `run()` calls `poll_active()` directly because it needs the all-dead flag.
Every unit test passed while the device never received an `x` key; only the
live daemon log caught it. `test_poll_active_itself_merges_codex` guards this.

### Codex panel mapping

A Codex Pro account has no account 5-hour window, so the top panel cannot
mirror Claude's "Current". Both panels are weekly:

- **top** — the model's weekly quota, pill `"Spark"`
- **bottom** — the account weekly quota, pill `"Overall"`

Both must be shown. Spark can sit at 0% while the account weekly is nearly
spent, and it is the account limit that actually stops you. An earlier
version sourced both panels from Spark for consistency and produced an
accurate, useless screen: `0% / 0%` at the moment the binding limit was at
84%.

---

## 3. Themes

`firmware/src/theme.h` is a table of `Theme` structs behind a `theme()`
accessor. ui.cpp already routed everything through `COL_*` macros, so **no
call site changed**.

Colors are stored as plain hex, not `lv_color_t`: `lv_color_hex()` is a
function, so a table of calls would run at static-init time, before LVGL is
up. Call sites convert at point of use.

| Field | Claude | Codex |
|-------|--------|-------|
| palette | warm greys, terra-cotta accent | neutral greys, ChatGPT green |
| `sans_only` | false — serif display face | true |
| `progress` | green | off-white — usage is information, not good news |
| `quiet_status` | false | true — no rotating gerunds |
| `scoped` | `0x4a7dea` | same — marks a model's slice, not a brand |

**Fonts.** Only three slots are serif (`title_font`, `ent_pct_font`,
`bt_title_font`), so the family swap runs as a post-pass after
`compute_layout()` picks sizes. `sans_for()` maps Tiempos 56 → Styrene 48 and
34 → 28, one nominal step down, because a grotesque's larger x-height reads as
the same optical size.

OpenAI Sans is **not** used: proprietary and undistributed. Styrene B stands
in, which is Anthropic's own face — a placeholder, not a finished answer.

**Status line.** Codex mode drops Claude Code's spinner glyphs and rotating
gerunds ("Elucidating…") — that is Anthropic's product voice on the wrong
provider. The glyph still animates, because a frozen indicator on a desk
display reads as a hung device; what is dropped is the trailing `…` claiming
work is in progress.

**Thresholds** are 75% warning / 90% critical, applied to both modes. The
previous 50% fired halfway through a session, which trains you to ignore it.

---

## 4. Art sets

Everything that differs between mascots travels in one `ArtSet` record in
`splash.cpp`:

| Field | Why it cannot be global |
|-------|-------------------------|
| `anims` / `count` | different tables |
| `stage_w/h` | Clawd's crops are authored on 55×37, Codey's on 29×34 |
| `groups` | rate-driven picks, matched by name — Codey has none of Clawd's names |
| `acts` | corner-mascot acts; Codey shares only `"waving"` with Clawd's list |
| `walk` | gait-step schedule (below) |
| `peek` | the pose shown large at the far edge mid-trip |

### Gait

`WALK_CRAB` and `WALK_FRONT` are **foot-locked schedules measured from
Clawd's own frames** — which frame plants a foot, how far the body moves.
Applied to another character they lock travel to feet that are not there.
Codey uses `WALK_EVEN`: one cell per gait frame.

### The trip

Both mascots now run the same journey: walk off stage left, appear large at
the bottom right, walk the whole width back to the corner slot. The return
leg is what crosses the screen.

The peek matches **on-screen height**, not cell size. Clawd's `lurking` is a
cropped lean-in pose (13×17 cells → 104×136px); Codey's borrowed `waving` is
a full 24×30 figure, so at the same cell it rendered 192×240 — half the panel.
Both anchor their feet to the same 4/5-screen line rather than deriving it
from their own stage offsets.

A short peek loops until `PEEK_MIN_MS`. Clawd's is 24 authored frames and
dwells naturally; Codey's waving is 4 frames totalling 600ms, which flashes a
large sprite on and off before it reads as anything.

### Buffers

`mas_buf` and the peek buffer are allocated **once** at create time, and the
mode can change at any point afterwards — so both are sized from the largest
frame across **every** set, not the active one. Sizing to the active set
overflowed `mas_buf` on the first mode switch.

### Rate gating

Animations are surfaced by usage rate, four bands of four. Clawd's seventeen
tolerate a three-per-band spread; Codey's nine did not — at a low rate only
`idle / happy / waving` ever played and the laptop pose sat in a band you
rarely idle in. Every band now carries four, with distinctive poses in more
than one.

---

## 5. Pet sprite pipeline

`tools/convert_pet_spritesheet.py` reads the sheet format the ChatGPT pets
docs specify for custom pets — transparent PNG/WebP at 1536×1872, an 8×9 grid
of 192×208 frames — and emits `pet_animations.h` in the same
`splash_anim_def_t` layout the Clawd art uses.

```bash
python3 tools/convert_pet_spritesheet.py research/codex-pets/codey-spritesheet.webp --stage-h 34
```

Written in Python, unlike the other `tools/`: Pillow is already a project
dependency (`daemon/icon_assets.py`) and reads WebP natively, whereas the Node
path needs `pngjs`, which is not vendored, plus a `sips` pre-step.

**Four things that only showed up once art was on a screen:**

1. **Palette index 0 is the background slot.** The engine renders code 0
   transparent and indexes `palette[code]` directly, so cell codes start at 1
   and colors must shift up one. Emitting from index 0 rendered every frame
   one color off, with the most common color becoming the background — a blue
   screen with a navy character.
2. **Quantize before counting.** Counting the palette in RGB888 is the
   obvious move and wrong here: soft shading means the top 15 source colors
   collapse onto **3** distinct RGB565 values, wasting twelve slots and
   flattening the shading.
3. **`loop_end` is the last valid index**, not `frame_count`. One past the end
   means the gait never wraps — the walk plays once and stops, about 12px of
   travel.
4. **Not pixel art.** The Clawd converter recovers cells by sampling centers
   after fitting the grid phase. These sprites are anti-aliased; a run-length
   scan over the opaque pixels returns gcd 1. Cells are area-downsampled and
   then quantized, with a coverage floor so the outline does not smear into a
   halo one ring wider than the character.

Every frame is cropped against **one global content bounding box**, not
per-frame boxes — otherwise the character hops when switching animations.

Swapping the sheet for a commissioned one at the same dimensions is a drop-in
change; the converter reads the grid, not the character. See
[`research/codex-pets/CLAUDE.md`](../research/codex-pets/CLAUDE.md) for
provenance and the licensing position.

---

## 6. Controls

**Right button (SECONDARY).** Tap flips the provider now; hold cycles the
auto-flip cadence `30s → 1m → 2m → 3m → off`, stepping again while held so one
hold reaches any cadence. The hold fires on the threshold, not on release, so
the gesture confirms itself under your thumb, and flashes its new label on the
status line — enabling auto-flip has no other visible effect for minutes.

This **retires HID Shift+Tab**. No supported board has a third button to move
it to.

`MODE_HOLD_MS` 600, `SECONDARY_DEBOUNCE_MS` 30, `MIN_FLIP_GAP_MS` 250. The
last is a guard, not a root-cause fix: two flips once landed in the same
millisecond with the timer measurably correct, so a repeat within 250ms is now
ignored at the one place every flip goes through.

**Only the 2.16 has a second button** (`button_count == 2`). On the 1.8, 2.06
and C6 boards the toggle is unreachable by button; use auto-flip or serial.

Mode and cadence both persist to NVS in the `"clawdmeter"` namespace.
`theme_init()` must run **before** `ui_init()` and `splash_init()` — the mode
picks the palette, the font family via `compute_layout()`, and the art set,
all read while those build their widgets.

---

## 7. QA

Beyond upstream's `screenshot` and `buzz`:

```bash
printf 'usage\n'  > /dev/cu.usbmodem101   # switch to the usage screen
printf 'splash\n' > /dev/cu.usbmodem101   # back to the splash
printf 'mode\n'   > /dev/cu.usbmodem101   # cycle provider
printf 'auto\n'   > /dev/cu.usbmodem101   # cycle auto-flip cadence
```

Then `./screenshot.sh out.png`. This exists because the corner mascot only
lives on the usage screen, a fresh flash boots to the splash, and **no button
leaves it** — PWR cycles animations there and only a touch toggles. Every
check used to be a request to the person holding the device.

The simulator has no serial console, so there the old workaround still
applies: temporarily change the default boot screen, iterate, revert.

**Things the simulator cannot catch**, learned the hard way: a payload fed by
hand hides a daemon that never sends it, and a glyph missing from a subset
font renders as tofu only on the panel. Both were found by reading the live
daemon log and pulling a screenshot off real hardware.

---

## 8. Fonts are subset to ASCII

The Styrene faces carry **U+0020..U+007E only**. The mono face adds exactly
seven non-ASCII glyphs: five spinner asterisks, an ellipsis, and U+00B7.

Anything typographic — em dash, bullet, `●` — renders as tofu. Check the cmap
before reaching for a character. The "no quota" placeholder is a plain ASCII
hyphen and the steady status dot is U+00B7 for exactly this reason.

---

## 9. Not done

- **Fonts.** Two reasons to regenerate: OpenAI Sans (or an OFL stand-in like
  Inter) for Codex mode, and **tabular figures** — Styrene's digits are
  proportional, `1` is 342 units against `0` at 531, so a percentage visibly
  jitters as it changes.
- **Licensing.** Upstream is deliberately unlicensed because it bundles
  proprietary fonts and mascot art; this fork adds a second vendor to that
  rather than resolving it. See the root README's warning.
- **Codex without Claude.** Codex only rides along when Claude also produced a
  payload, since the device's top-level fields are Claude's. A dead Claude
  token currently hides working Codex data.
- **The bash daemon** (Linux) has no Codex support. Both Python daemons do.
- **One-button boards** cannot reach the provider toggle by button.
