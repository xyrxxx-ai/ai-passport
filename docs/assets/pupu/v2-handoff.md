<p align="right">
  <a href="v2-handoff.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu v2 Development Handoff: Pat, Work, Sheep Gate

Status: design final · 2026-10-25 · Target device: FoloToy AI Passport (240 × 320 portrait screen, three physical buttons ↑ ↓ OK, no touch, ESP32-C3, no PSRAM)

> **In one sentence**: add three things to Pupu. Pat gets a hand animation and "too many pats make it angry"; a new "work" action earns pocket money, feeding now costs money, and too much work makes it emo; the mini-game "Catch the Stars" is replaced by "Sheep Gate", where you pick formula cards across three lanes, with Normal and Scholar levels. The WeChat mini program is **not** in scope this time; every feature in this round must be fully playable without connecting to a phone.

This document is for another development thread. It has only two parts: sections 1 to 7 are the **final product rules** (every number is settled; do not change them), and sections 8 to 13 are the **implementation checklist** (files, fields, tests, order, acceptance). Before starting, read `AGENTS.md` at the repository root and use the `passport-develop` skill; the existing rules are in [`design.md`](design.md), and progress is in [`progress.md`](progress.md).

---

## 0. Why the design looks like this

Three review agents ran simulations on the first draft (a week of income and spending was worked out for a player who checks in three times a day and for one who checks in once a day, and the mini-game was simulated for 2000 rounds). They found three problems, all fixed in the final version:

1. **The existing decay is too fast, so costs are invisible.** Fullness −10 per hour and happiness −8 per hour mean that after 8 hours away you usually pick it up at 10 / 10 (even from 100 / 100 before leaving, only 20 / 36 is left). A −5 happiness cost for work or for the 5th pat would then hardly be felt by the player. So decay is slowed to "one night's sleep costs about one rice ball".
2. **Without a cap, working while emo prints unlimited money.** Once happiness is floored at 0, every press is pure income. So work is capped at 5 times per day.
3. **A wolf pack set at 70% of the optimal path sounds lenient but really means "only one mistake in 6 waves".** Multiplication cards and addition cards are mixed in the same wave, and after 6 waves of compounding the gap is tens of times. So card generation now has a "best and second-best differ by less than 20%" constraint, the threshold is 60%, and it is measured by **cumulative performance** rather than the absolute path.

Two more principles: **bond only goes up** stays unchanged (it is growth progress, and going backwards feels like punishment), so the bond cost of work is implemented as "using up part of the day's allowance"; and **star candy still comes only from the mini-game**, because it is the only shortcut out of emo, which is what makes it valuable.

---

## 1. Resources and decay

Pupu now has five numbers. All times are **pet time** (real seconds × scale factor, see `pupu_clock.h`).

| Resource | Range | Change | Compared with v1 |
| --- | --- | --- | --- |
| Fullness | 0–100 | **−1 every 15 minutes (−4 per hour)**. At most 8 hours of catch-up when away = −32, exactly one rice ball | v1 was −1 every 6 minutes |
| Happiness | 0–100 | **−1 every 20 minutes (−3 per hour); −2 every 20 minutes when very hungry (fullness < 15)** | v1 was −2 / −4 every 15 minutes |
| Bond | Only goes up; grows up at 100 | Daily allowance of 20 per pet day, shared by all sources; **work uses up allowance** (see section 4) | Unchanged |
| Star candy | 0–99 | Only from winning the mini-game, at most 3 per day | Unchanged |
| **Pocket money (new)** | **0–99** | **Only from work. A newly hatched Pupu has 30 coins; migrated v1 saves are also topped up to 30 coins** | New |

The catch-up rule is unchanged: values catch up at most 8 hours, with a floor of min(10, the value before catch-up); the day count follows the full time away.

**A day's ledger (a player who checks in three times a day; for understanding, not a rule)**: work twice, +20 coins; three rice balls, −15 coins; the remaining 5 coins buy one cookie. Bond allowance 20 − 2 × 3 = 14, exactly used up by pats 9 + mini-game win 3 + first sync 2, so it grows up in about 7 days. With no work at all it can grow up in 5 days, but the 30 starting coins only cover two days of food.

---

## 2. Feeding costs money

| Food | Price | Effect |
| --- | --- | --- |
| Rice ball | 5 coins | Fullness +35; when hungry (fullness < 40) bond +3 (unchanged) |
| Cookie | 3 coins | Fullness +10, happiness +5 (within a 60-minute window the first 3 cookies add happiness; from the 4th it is "bored of them" but money is still deducted) (effect unchanged) |
| Star candy | Free, uses 1 candy | Happiness +25, bond +2 (unchanged); **during emo the "fullness ≥ 90 refuses food" rule does not apply**; otherwise as before |

Check order (`pupu_model_feed()` returns in this order; once a step matches, the rest are skipped):

1. Egg stage → `PUPU_NOT_HATCHED`.
2. Star candy with zero stock → `PUPU_NO_CANDY`.
3. **A cookie while emo → `PUPU_SULKING` (new)**: head shake, bubble "No appetite", no money deducted, not counted, no window opened.
4. Fullness ≥ 90, and it is not "star candy while emo" → `PUPU_REFUSED`: head shake "I'm too full", no money deducted.
5. **A rice ball or cookie with `coin` below the price → `PUPU_NO_COIN` (new)**: head shake, bubble "No money, go to work", nothing changes.
6. Take effect and deduct money. Star candy is free and skips the money check; feeding star candy sets `emo_min = 0`.

Prices are per serving: a rice ball at fullness 89 only brings it to 100 but still costs 5 coins; a cookie it is "bored of" still costs 3 coins.

At the top of the feed page, "Star candy ×2" becomes "**Pocket money 15  Candy ×2**" (two short labels, right-aligned). When the selected food is unaffordable, the text in the bottom capsule is drawn faint (`PUPU_COLOR_MUTED`).

---

## 3. Pat (formerly "head pat", renamed)

Change "head pat" to "**pat**" everywhere: menu item, bubbles, and documents. The 10-minute window is unchanged: the 1st pat opens the window, and the count resets when the window expires. By count within the window:

| Pat number | Happiness | Bond | Pupu | Picture and sound |
| --- | --- | --- | --- | --- |
| 1, 2, 3 | +5 | +1 | Happy hop | A small hand drops from above the pet onto its head, pats once, and lifts; "slap" sound; bubble "Happiness +5  Bond +1" |
| 4 | 0 | 0 | **Annoyed** (new pose: frown, head tilted), **held until the window ends** | Hand drops, bubble "That's enough", "hmph" sound |
| 5 | **−5** | 0 | **Fierce protest** (new pose: angry jump away) | Hand drops and is knocked away (flies off to the upper right), bubble "Stop patting me!  Happiness −5", three falling notes |
| 6 and later | 0 | 0 | Turns away (reuses the dodge pose) | **No hand, no bubble** |

Counting rule (different from v1): **every press within the window counts, including the 4th and later**, saturating at 5 (from the 6th press the count no longer increases and the window is not extended). During emo, a pat just dodges, bubble "Leave me alone", with no count and no window opened. Bond still goes through the daily allowance of 20.

---

## 4. Work (new)

On the play page, the menu becomes three items: **Pat / Work / Sheep Gate**. Counted per pet day and reset when the pet day passes (cleared in `advance_day()`, the same as `candy_today`).

| Work number today | Pocket money | Happiness | Today's bond allowance | Pupu | Picture and sound |
| --- | --- | --- | --- | --- | --- |
| 1, 2 | +10 | −5 | **−3** (`bond_today += 3`; bond itself unchanged) | Carrying a spreadsheet (new pose: hugging a sheet, sweating) | An Excel spreadsheet drops from the top of the screen and Pupu catches it, "ding-ding" (coins); bubble "Pocket money +10  Happiness −5  Today's bond allowance −3" |
| 3 | +10 | −5 | −3 | Knocked dizzy (new pose: on the floor, a dizzy swirl over its head) | The spreadsheet hits its head, "thud"; then it enters emo, bubble "Worn out... need to rest 4 hours" |
| 4, 5 (while emo) | **+3** | **−10** | 0 | Knocked dizzy | "thud", bubble "Got the sheet wrong...  Pocket money +3  Happiness −10" |
| 6 and later | 0 | 0 | 0 | Head shake | Bubble "Can't work anymore", "NO" sound |

Which work it is depends only on `work_today` before the work (add 1, then compare), **not on `emo_min`**: if star candy cleared emo early, the day's 4th and 5th works are still +3 / −10; after the pet day rolls over `work_today` is cleared, so even if emo has not ended, the new day's 1st and 2nd works are +10 / −5. The 3rd work unconditionally sets `emo_min` to 240 (reset even if already emo).

Allowance rule: `bond_today` goes up to at most 20; the bubble shows the amount actually used (from 18 → 20 it shows −2; when already full it shows 0, but work still pays as usual). Pocket money is capped at 99 (at 95 coins, work only adds +4, but it still counts as one work, still costs happiness, and still uses allowance). Work is not possible in the egg stage (`PUPU_NOT_HATCHED`).

---

## 5. Emo

- **Entering**: at the end of the 3rd work of the day. `emo_min = 240` (4 hours of pet time).
- **Countdown**: −1 per minute in `step_minute()`, the same as `cookie_win_min`; catch-up runs at most 8 hours, so after a deep sleep or a power-off of more than 4 hours it is always already cleared on wake; crossing a day or a deep sleep needs no special case.
- **Pose**: when `emo_min > 0` the idle animation switches to emo (head down, a small dark cloud overhead), shown on the home, feed, and play pages. Idle pose priority: **very hungry lying down > emo > annoyed (section 3) > normal idle**; bubble priority likewise ("Hungry" > "Worn out").
- **During emo**: pats only dodge ("Leave me alone"); cookies are refused (head shake "No appetite", no money deducted); rice balls are eaten as usual; for work see rows 4 and 5 of section 4; the mini-game plays as usual and a win gives the usual rewards but **does not clear emo**.
- **Clearing early**: only by feeding one star candy (immediately `emo_min = 0`, plus the usual happiness +25 and bond +2).
- The hours in the bubble are shown as `(emo_min + 59) / 60`.

---

## 6. Mini-game "Sheep Gate" (replaces "Catch the Stars")

**One round**: choose the level at the start (↑↓ picks "Normal / Scholar", OK starts) → 3-2-1 countdown → 6 waves → wolf-pack settlement → result card. ↑↓ switch lanes, OK pauses, long-press OK quits, screen-off while paused voids the round, and quitting midway does not count as a round; all carried over from Catch the Stars.

**Picture**: keep the three 80 px lanes from `catch_layout.h`, with Pupu on the left at x 8–56. Each wave is three **static** cards, one per lane, at x 140–220, 64 px tall, rounded corners, paper-colored fill with an ink-colored border; the cards do not fly. The top bar shows "Sheep N" on the left and "Wave k/6" on the right, and below it a countdown bar drains from full to empty. When time is up, the card in Pupu's lane slides to Pupu in 0.3 seconds and takes effect while the other two disappear; then the next wave appears. After the 6th wave the wolf-pack icon slides in from the right carrying the number W, it is compared against N, and the result card appears.

**Numbers**:
- The flock N starts at 10, range 1–999 (results are rounded down; below 1 counts as 1, above 999 as 999).
- Each wave first picks three **result values** from the current N, best > second > third, checked by cross-multiplication: `second × 100 >= best × 80`, `third × 100 <= best × 60`, all three distinct and within 1–999. Then each value gets a **notation** (see the card pool). This way the card whose "number looks biggest" is not necessarily the best; you really have to calculate.
- Performance ratio `R` (per mille, `uint16_t`, starts at 1000): each wave `R = R × chosen / best` (integer arithmetic with a `uint32_t` intermediate; a result below 1 counts as 1). **Win condition: `R >= 600` after 6 waves**. One second-best pick (×0.8) wins, two (0.64) win, three (0.51) lose; after picking the worst card once (≤ 0.6), any further mistake loses.
- The wolf count is only for display: `W = (N × 600 + R − 1) / R` (rounded up, `uint32_t`), so "N ≥ W" and "R ≥ 600" always agree; W is capped at 9999 when shown in the top bar and on the result card.
- Feedback right after each wave's card takes effect: picking best shows the bubble "Best!" (green border flashes once), second "Not bad", third "Poor"; with two short sound effects.
- Random numbers use seeded xorshift32 (carried over from `catch_model.c`); the same seed and the same key presses always give the same round.

**Card pool (notations)**. Each card has at most two lines; the width rule is the "cells" rule after the table. Operations are applied left to right, with no parentheses.

| Notation family | Examples | Value | Only appears when |
| --- | --- | --- | --- |
| Add / subtract | `+36`, `−9` | Literal | Addition constants from 0.6N–1.2N, subtraction constants from 0.2N–0.5N |
| Multiply / divide | `×2`, `×3`, `÷2`, `÷3` | Literal | ÷2 only when N is even, ÷3 only when N is divisible by 3 |
| Two-step | `×2 −15`, `+9 ×2`, `+40%`, `−25%`, `×1.5` | Applied in order | +40% only when N is a multiple of 5, −25% only when a multiple of 4, ×1.5 only when even |
| Square root | `+√144` | 12 | Value is a perfect square |
| Power | `+6²`, `+3³`, `+2⁴` | 36, 27, 16 | Exponent only 2, 3, 4 (the font has no ⁵ and above) |
| Factorial | `+3!`, `+4!` | 6, 24 | |
| Integral | Line 1 `+∫2x dx`, line 2 `0→6` | 36 (= b²) | Value is a perfect square; the font has no subscripts, so the bounds go on the second line |
| Derivative | Line 1 `+(x³)′`, line 2 `x=3` | 27 (= 3a²) | Value is 3a² |
| Sum | Line 1 `+∑k`, line 2 `k=1→8` | 36 (= n(n+1)/2) | Value is a triangular number |

Generation is **list the candidates first, then pick three**: the candidate set = every (notation, value) the wave's allowed families can express at the current N. For all additive families (add / subtract, square root, power, factorial, integral, derivative, sum) the addend k is taken from `1 ≤ k ≤ max(⌊1.2N⌋, 6)`, the subtrahend k from `⌈0.2N⌉ ≤ k ≤ ⌊0.5N⌋` (no subtraction when N < 5), and multiply / divide follow the divisibility conditions in the table. Look for a triple that satisfies the constraints in this order and stop at the first hit: ① the wave's families, with best's addend ≥ 0.6N; ② the wave's families, best unrestricted; ③ all families; ④ degrade to three cards `+kmax`, `+⌈0.8 × kmax⌉`, `+1` (kmax = max(⌊1.2N⌋, 6)); ④ satisfies the constraints for any N ≥ 1.

Card text width is measured in "cells", provided by the rule layer as `gate_text_cells()`: ASCII 1 cell, `%` 2 cells, `× ÷ √ ∫ ∑ → −` 2 cells, `² ³ ⁴ ′` 1 cell, a Chinese character 2 cells; **at most 9 cells per line** (9 × 8 px = 72 px, the 80 px card minus 4 px padding on each side). The minus sign on cards is always `−` (U+2212).

**Levels**:

| Level | Waves 1–2 | Waves 3–4 | Waves 5–6 | Seconds per wave |
| --- | --- | --- | --- | --- |
| Normal | Add / subtract / multiply / divide | Two-step | Square root, power, factorial | 4, 4, 6, 6, 6, 6 |
| Scholar | Two-step | Integral, derivative | Sum, power, ×1.5 | 6, 6, 8, 8, 8, 8 |

A Scholar round lasts at most 44 seconds + 3 seconds of countdown + about 3 seconds of settlement. Multiplication cards (×2, ×3, ×1.5, +40%) appear in at most 2 waves per round, so N rarely reaches 999.

**Rewards** (unchanged): completing a round costs fullness −5; a win gives happiness +15, bond +3, star candy +1 (at most 3 per day, stock capped at 99). The result card keeps the Catch the Stars format.

---

## 7. Unchanged rules

The sync protocol (bytes on the wire unchanged, `proto_ver` stays 1), bond +2 on the first sync of each day, egg hatching, growing up, screen off, deep sleep, save timing, and key filtering are all untouched. The four home-screen entries are unchanged. The detail rules in section 12 of `design.md` remain in force except where this document changes them.

---

## 8. Implementation checklist: rule core (pure logic; build and test it first)

### 8.1 `main/pupu_config.h`

| Constant | Value | Notes |
| --- | --- | --- |
| `PUPU_FULL_DECAY_EVERY_MIN` | 6 → **15** | |
| `PUPU_HAPPY_DECAY_EVERY_MIN` | 15 → **20** | |
| `PUPU_HAPPY_DECAY` | 2 → **1** | |
| `PUPU_HAPPY_DECAY_STARVING` | 4 → **2** | |
| `PUPU_COIN_MAX` | 99 | New |
| `PUPU_COIN_START` | 30 | New: newly hatched and v1 migration |
| `PUPU_RICE_PRICE` / `PUPU_COOKIE_PRICE` | 5 / 3 | New |
| `PUPU_PAT_HAPPY` / `PUPU_PAT_BOND` | 5 / 1 | Unchanged; only for pats 1–3 |
| `PUPU_PAT_LIMIT` | 3 | Unchanged: nothing is added from the 4th |
| `PUPU_PAT_ANGRY_AT` | 5 | New: this pat costs happiness −`PUPU_PAT_ANGRY_HAPPY` (5) |
| `PUPU_PAT_COUNT_MAX` | 5 | New: count saturation value |
| `PUPU_WORK_COIN` / `PUPU_WORK_HAPPY_COST` / `PUPU_WORK_BOND_BUDGET` | 10 / 5 / 3 | New |
| `PUPU_WORK_EMO_AT` | 3 | New: enters emo after the 3rd |
| `PUPU_WORK_EMO_COIN` / `PUPU_WORK_EMO_HAPPY_COST` | 3 / 10 | New |
| `PUPU_WORK_PER_DAY` | 5 | New: refused from the 6th |
| `PUPU_EMO_MIN` | 240 | New: pet minutes |

Mini-game settlement constants: delete `PUPU_CATCH_WIN_STARS` (8) and rename `PUPU_CATCH_FULL_COST` / `PUPU_CATCH_HAPPY` / `PUPU_CATCH_BOND` to `PUPU_GATE_FULL_COST` (5), `PUPU_GATE_HAPPY` (15), `PUPU_GATE_BOND` (3); `PUPU_CANDY_PER_DAY` is unchanged. **`PUPU_CATCH_UP_MAX_S` and `PUPU_CATCH_UP_FLOOR` belong to the away-time catch-up and must not be touched.** These deletions and renames happen in M7c (see section 10).

### 8.2 `main/pupu_model.h` / `.c`

- New fields in `pupu_model_t`: `uint8_t coin;`, `uint8_t work_today;`, `uint16_t emo_min;`. Put them at the end of the struct.
- New values in `pupu_result_t`: `PUPU_NO_COIN` (not enough money), `PUPU_ANNOYED` (4th pat), `PUPU_ANGRY` (5th pat), `PUPU_SULKING` (pat or cookie while emo), `PUPU_WENT_EMO` (3rd work, successful and entering emo), `PUPU_OVERWORKED` (4th and 5th work, successful, +3 / −10), `PUPU_EXHAUSTED` (6th and later, nothing changes).
- New fields in `pupu_outcome_t`: `int8_t coin;` (−5…+10), `int8_t bond_budget;` (−3 on work, for the bubble), `bool emo_cleared;` (feeding star candy cleared emo).
- `pupu_model_init()`: `coin = PUPU_COIN_START`.
- `step_minute()`: `if (m->emo_min > 0) m->emo_min--;` next to the window countdowns; it also runs in the egg stage (an egg never goes emo, so no effect).
- `advance_day()`: clear `work_today`.
- `pupu_model_feed()`: rewrite in the six-step order of section 2; feeding star candy sets `emo_min = 0` and sets `emo_cleared`.
- `pupu_model_pat()`: rewrite the counting per section 3; returns `PUPU_SULKING` while emo.
- New function `pupu_outcome_t pupu_model_work(pupu_model_t *m);`, per section 4: the 1st and 2nd return `PUPU_OK`, the 3rd `PUPU_WENT_EMO`, the 4th and 5th `PUPU_OVERWORKED`, the 6th and later `PUPU_EXHAUSTED`.
- Add `pupu_model_apply_gate_result(pupu_model_t *m, bool completed, bool won);` with the same logic as `pupu_model_apply_catch_result()` (win = `won`). Keep the old function for now and delete it in M7c when the mini-game is replaced (see section 10).
- In the header comments, change "head pat" to "pat" and add notes on emo and pocket money.
- `tests/test_pupu_model.c` must add at least these cases: every tier of pats 1–6 (including the 4th staying annoyed, the 5th −5, the 6th not counted, reset when the window expires); a pat while emo → `PUPU_SULKING` with no state change; every tier of works 1–6, `emo_min == 240` after the 3rd, reset by a 3rd work while already emo; `emo_min` counting down to 0 through `advance` and `catch_up`; a pet-day rollover clears `work_today` but not `emo_min`; emo + fullness 95 + cookie → `PUPU_SULKING`; emo + 0 coins + cookie → `PUPU_SULKING`; emo + fullness 95 + star candy → `PUPU_OK`, `emo_cleared`, `emo_min == 0`; rice ball with no money → `PUPU_NO_COIN`; at fullness ≥ 90 a rice ball is `PUPU_REFUSED` first with no money deducted; the new hourly decay values; work using allowance up to the cap of 20. The existing `test_pat_window` (around line 273) expects "dodged pats are not counted" and must be changed.

### 8.3 Save data `main/pupu_save_codec.h` / `.c`

- `PUPU_SAVE_VERSION` 1 → 2. **Split the length constants**: `PUPU_SAVE_PAYLOAD_SIZE_V1 = 51`, `PUPU_SAVE_PAYLOAD_SIZE_V2 = 55` (the 51 v1 bytes followed by `coin` u8, `work_today` u8, `emo_min` u16); `PUPU_SAVE_PAYLOAD_SIZE` points at V2, and the total record length is 71 bytes.
- `decode_payload()`: `case 1` uses the V1 constant, calls `decode_v1()`, then fills in `coin = PUPU_COIN_START`, `work_today = 0`, `emo_min = 0`; add `case 2`. With the current code, changing only one constant would make **every old save report BAD_LENGTH and wipe the player's Pupu**, which must be avoided.
- `tests/test_pupu_save_codec.c`: keep one byte-for-byte v1 sample proving an old save loads and yields 30 coins; a v2 round trip; wrong lengths.
- `tests/pupu_sync_vectors.txt` lines 39–51 are a 67-byte v1 save sample (`save_info` with `save_ver=1`, `total_len=67`, `chunk_count=4`). More than this block changes:
  - Once the save is 71 bytes it splits into 5 chunks (17 + 17 + 17 + 17 + 3), so add one more pair `save_get_4` / `save_chunk_4` (18 pairs in total); `save_info` becomes `save_ver=2 total_len=71 chunk_count=5`; the idx of `save_get_oob` becomes 5; the last 4 bytes of the new payload are `1e 00 00 00` (coin = 30, work_today = 0, emo_min = 0).
  - After the decay change, the fullness and happiness bytes in the three rows `status_synced` / `status_retry` / `status_earlier` and in `save_chunk_0` change too (the header comment "catch up 3 h (full −30, happy −24)" becomes −12 / −9 with the new decay).
  - The hard-coded `pairs == 17`, `chunk_count(...) == 4`, `PUPU_SAVE_RECORD_SIZE == 67`, and last chunk `3 + 16` in `tests/test_pupu_sync_proto.c` become 18 / 5 / 71 / 3; the 17 req / 17 resp in `tests/test_pupu_sync_web.mjs` change the same way.
  - The repository has no tool that generates the vectors. Suggested: add a `--dump` option to `test_pupu_sync_proto.c` that prints every reply in hex in the order of the vector file, overwrite the vector file with it, then run `test_pupu_sync_web.mjs` as a cross-check.

  The protocol itself is unchanged (`proto_ver` stays 1); the web page assembles chunks dynamically from `total_len` and needs no change.

### 8.4 Mini-game rules `main/gate_model.h` / `.c`, `main/gate_layout.h`

Replace `catch_model.*` and `catch_layout.h` (delete the old files; move the lane layout constants to `gate_layout.h`, with the `CATCH_` prefix renamed to `GATE_`). Suggested interface:

```c
typedef enum { GATE_EASY = 0, GATE_HARD } gate_level_t;
void gate_init(gate_model_t *m, uint32_t seed, gate_level_t level);
uint32_t gate_input(gate_model_t *m, gate_input_t in);   // UP/DOWN/OK_PRESS/OK_RELEASE/OK_LONG, same as catch
uint32_t gate_tick(gate_model_t *m);                      // one step every 20 ms
const gate_card_t *gate_card(const gate_model_t *m, int lane);  // the current wave's card: two lines of text + value
unsigned gate_wave_ms_left(const gate_model_t *m);        // countdown bar
int gate_text_cells(const char *utf8);                    // the cell rule from section 6, shared by tests and UI
gate_result_t gate_result(const gate_model_t *m);         // { bool completed; bool won; uint16_t sheep; uint32_t wolves; uint16_t r_permille; }
```

Event bits follow the `CATCH_EV_*` approach, adding `GATE_EV_WAVE` (a new wave appears), `GATE_EV_PICK_BEST/SECOND/THIRD`, `GATE_EV_WOLVES`. State machine: `READY_LEVEL` (choose level) → `COUNTDOWN` → `WAVE` → `APPLYING` (0.3-second slide) → … → `WOLVES` → `FINISHED` / `ABORTED`. In `READY_LEVEL`, ↑↓ switch the level and the countdown starts only on **OK release** (`GATE_IN_OK_RELEASE`), so a 0.5-second hold (`GATE_IN_OK_LONG`) quits directly without first starting a round; the last chosen level is kept in a static variable in `pupu_ui_gate.c` and is the default the next time the page opens. `gate_in_round()` is true in `READY_LEVEL` (keys use GAME mode, no periodic save), and `gate_active()` is false in `READY_LEVEL` (like pause, dimming and screen-off are allowed; screen-off voids the round and the screen comes back on the play page); the other states behave as in Catch the Stars. The rule layer generates the card text as two `char[16]` lines (UTF-8) and the UI only displays them, so the text width can be checked in host tests.

`tests/test_gate_model.c` (replaces `test_catch_model.c`; also change the compile and run lines at lines 63–65 of `tools/validate.sh`) must cover: for N from 1 to 999, both levels, and every wave position, the three generated cards satisfy the cross-multiplication constraints, are distinct, and fall within 1–999; every notation's value is an integer and matches what is displayed; `gate_text_cells()` ≤ 9 for each line; every non-ASCII character on a card is in `PUPU_TXT_GATE_GLYPHS` (section 9.4); the divisibility conditions for ÷, %, and ×1.5; the `R` update unit-tested with fixed ratios (1000 → 800 → 640 wins, → 512 loses), not asserted on random cards; with a fixed seed, picking the worst card in all 6 waves (including the extreme triple third = 1, best = 999) does not crash, `R >= 1`, and `W > N`; multiplication cards in ≤ 2 waves per round; the same seed reproduces the round; pause, quit, and void semantics identical to Catch the Stars.

---

## 9. Implementation checklist: UI, art, sound, font

### 9.1 Pages

- `main/pupu_ui.h`: add `PUPU_POSE_ANNOYED`, `PUPU_POSE_ANGRY`, `PUPU_POSE_WORK`, `PUPU_POSE_DIZZY`, `PUPU_POSE_EMO` to `pupu_pose_t`, with `PUPU_POSE_COUNT` at the end; add five entries to each of the two tables in `pupu_ui_pet_anim()`, and change `PUPU_POSE_WAVE + 1` in the two `_Static_assert`s to `PUPU_POSE_COUNT` (without that change it does not compile). The idle pose has a single source: a new function `pupu_ui_idle_pose(m)` (in `pupu_ui.c`, shared by the home and care pages) that returns `IDLE` / `EMO` / `ANNOYED` by the priority very hungry > emo > annoyed > normal; `pupu_ui_pet_anim()` applies the "very hungry lying down" replacement to all three poses `IDLE`, `EMO`, `ANNOYED`, and not to the reaction poses (`EAT` / `HAPPY` / `DODGE` / `SHAKE` / `WORK` / `DIZZY` / `ANGRY`). `PUPU_UI_GO_CATCH` becomes `PUPU_UI_GO_GATE` (M7c).
- `main/pupu_ui_care.c`: the play page has three items, `Pat / Work / Sheep Gate`; `pat()` branches on the result to show the five reactions; new `work()`; the feed page shows pocket money at the top; the capsule text is faint when unaffordable; the hand and the spreadsheet are `lv_image` objects layered above the pet canvas; the care page's main loop is 50 ms (`LOOP_MS`), and they move one step per loop (a 0.3-second drop = 6 steps × 10 px) and are hidden when the reaction ends. The annoyed condition is `pat_win_min > 0 && pat_count >= 4`, decided by `pupu_ui_idle_pose()`.
- `main/pupu_ui_home.c`: no change needed as long as `pupu_ui_pet_anim()` handles the emo idle; bubble priority "Hungry" > "Worn out".
- `main/pupu_ui_status.c`: add a "Pocket money" row, `ROW_STEP` 56 → 44, five rows at y = 60 / 104 / 148 / 192 / 236, the last progress bar reaching 270, above the "Long-press OK to go back" hint at the bottom (from about y 287).
- `main/pupu_ui_gate.c` (replaces `pupu_ui_catch.c`): level selection → countdown → waves → settlement. LVGL object budget: 3 cards (rectangle + 2 labels), the countdown bar, 2 top-bar labels, the wolf-pack icon + label, and the result card, all created on entering the page and afterwards only shown, hidden, or moved. Keep the existing frame rate / lane-switch latency / LVGL memory log lines for device acceptance.
- `main/pupu_app.c`: `PAGE_CATCH` → `PAGE_GATE`, with the related calls renamed; everything else (20 ms main loop, deferred save, screen-off while paused voids the round) carries over.

### 9.2 Pixel art (draw placeholder versions with the script first)

Append to the end of `ANIMS` in `tools/make_placeholder_art.py` (one set each for the young and the adult Pupu, 2 frames per set, appended last so the bytes of existing frames do not change): `child_annoyed`, `child_angry`, `child_work`, `child_dizzy`, `child_emo` and the matching `adult_*`. The small dark cloud for emo uses color 15 (`PUPU_SLOT_CLOUD`); pose frames can only use the 16 palette colors.

Add `icon_hand`, `icon_sheet` (an Excel spreadsheet: green background, white cells), and `icon_wolf` to `ICONS`, suggested **48 × 48** (add a size field to `ICONS` in `gen_pupu_assets.py`; `ICON_W` is no longer the only size), 6,912 bytes each. Note that today `read_strip()` in `gen_pupu_assets.py` accepts only the 16 colors of `pupu.gpl` for icons too, and the palette has no green: change icons to read RGBA directly without the palette check (they are emitted as RGB565A8 anyway), and let `make_placeholder_art.py` draw icons with plain RGB colors. `icon_star` and `icon_cloud` are deleted in M7c; deleting the star frees slot 14, and slot 15 (cloud grey) stays for the emo cloud.

Then run `/usr/bin/python3 tools/make_placeholder_art.py && /usr/bin/python3 tools/gen_pupu_assets.py`; `tests/test_pupu_assets_fresh.py` checks the hashes; update the file tables in `assets/images/pupu/README.md` / `.zh_CN.md`. About 23 KB of new frame data + 21 KB of icons, with 7 MB of Flash headroom, so no pressure.

### 9.3 Sound effects `main/pupu_sfx.h` / `.c`

New: `PUPU_SFX_PAT` (slap), `PUPU_SFX_ANNOYED` (hmph), `PUPU_SFX_ANGRY` (three falling notes), `PUPU_SFX_COIN` (ding-ding), `PUPU_SFX_THUD` (thud: 40 ms of noise + 160 ms of 110 Hz square wave, lower and longer than CLOUD), `PUPU_SFX_SIGH` (sigh: a long downward slide, played on `PUPU_SULKING`, that is a pat or a "No appetite" cookie while emo), `PUPU_SFX_CARD_GOOD`, `PUPU_SFX_CARD_BAD`, `PUPU_SFX_HOWL` (wolf howl). `PUPU_SFX_STAR` and `PUPU_SFX_CLOUD` are deleted in M7c. WIN / LOSE / BEEP / NO are reused.

### 9.4 Text and font

New entries in `main/pupu_text.h` (the missing characters reported by `tests/test_pupu_charset.py` are the authority):

On-device strings are Chinese; the Simplified Chinese version of this file is authoritative for the exact text.

| Macro | Text |
| --- | --- |
| `PUPU_TXT_PAT` | "head pat" → **"pat"** |
| `PUPU_TXT_WORK` / `PUPU_TXT_GATE` | Work / Sheep Gate |
| `PUPU_TXT_COIN` / `PUPU_TXT_COIN_COUNT` | Pocket money / Pocket money %d |
| `PUPU_TXT_CANDY_SHORT` | Candy ×%d |
| `PUPU_TXT_NO_COIN` | No money, go to work |
| `PUPU_TXT_ANNOYED` / `PUPU_TXT_ANGRY` | That's enough / Stop patting me! |
| `PUPU_TXT_SULKING` / `PUPU_TXT_NO_APPETITE` | Leave me alone / No appetite |
| `PUPU_TXT_WENT_EMO` | Worn out... need to rest %u hours |
| `PUPU_TXT_EMO_WORK` / `PUPU_TXT_EXHAUSTED` | Got the sheet wrong... / Can't work anymore |
| `PUPU_TXT_BOND_BUDGET` | Today's bond allowance |
| `PUPU_TXT_LEVEL_EASY` / `PUPU_TXT_LEVEL_HARD` / `PUPU_TXT_PICK_LEVEL` | Normal / Scholar / ↑ ↓ pick a level, OK to start |
| `PUPU_TXT_SHEEP` / `PUPU_TXT_WAVE` / `PUPU_TXT_WOLVES` | Sheep %u / Wave %u/6 / Wolf pack ×%u is coming! |
| `PUPU_TXT_PICK_BEST` / `_SECOND` / `_THIRD` | Best! / Not bad / Poor |
| `PUPU_TXT_GATE_WIN` / `PUPU_TXT_GATE_LOSE` | More sheep than wolves, you win! / Not enough sheep... |
| `PUPU_TXT_GATE_GLYPHS` | `+−×÷√∫∑′²³⁴→%!()xdk=`: every non-Chinese character the cards use, added to `PUPU_TEXT_LIST`; `gate_model.c` may use only these symbols, so both `test_pupu_charset.py` and `pupu_fonts_selfcheck()` can check them. It is never shown on screen itself |

Symbols to add to `assets/fonts/pupu/charset.txt`: `÷ √ ∫ ∑ ′ ² ³ ⁴ → −` (the last one is the minus sign U+2212, used for every minus on a card; the source font Noto Sans SC has all of them; it has **no** ⁵–⁹, subscripts ₀–₉, or ∛, so do not use them). About 36 new Chinese characters; add them in one go after `tests/test_pupu_charset.py` reports them. The regeneration command is in `assets/fonts/pupu/README.md`; it needs Node 22 (this Mac already has v22.22.1); do not use Node 24. About 46 new glyphs, about 6 KB.

---

## 10. Implementation order and acceptance for each step

| Sub-milestone | What | Done when |
| --- | --- | --- |
| **M7a rule core** | 8.1–8.3: config, model, save v2, regenerated sync vectors | `./tools/validate.sh --static` all green; `test_pupu_model` covers every number in sections 1–5; the v1 save sample decodes to 30 coins |
| **M7b pat, work, emo** | The care / home / status pages from 9.1, the five poses and two icons from 9.2, the six sound effects from 9.3, the font from 9.4 | `--static` all green; `--firmware` builds with no warnings; `pupu_fonts_selfcheck()` reports no missing characters at boot |
| **M7c Sheep Gate** | 8.4, `pupu_ui_gate.c`, the wolf icon, the card sound effects, the text; **every Catch the Stars deletion and rename happens in this step** (the four constants in 8.1, the old function in 8.2, the two icons in 9.2, the two sound effects in 9.3, `PUPU_UI_GO_CATCH` / `PAGE_CATCH`, `test_catch_model.c` and its three lines in `validate.sh`) | `test_gate_model` all green; `--firmware` passes; the serial log shows minimum frames per second ≥ 30, longest frame ≤ 40 ms, lane switch to redraw ≤ 50 ms, LVGL pool headroom ≥ 4 KB (the Catch the Stars M5 acceptance line; M5 measured 36–40 fps, lane switch 33–51 ms, pool headroom about 37 KB, for comparison) |
| **M7d documents and complete check** | The documents in section 11; the full `./tools/validate.sh` run | The complete gate passes; the progress document records the build identity |

**M7a and M7b only add; they delete and rename nothing**: `pupu_model_apply_catch_result()`, the four `PUPU_CATCH_WIN_STARS` etc. constants, `PUPU_SFX_STAR` / `CLOUD`, `icon_star` / `icon_cloud`, `PUPU_UI_GO_CATCH`, and `PAGE_CATCH` all stay, and the new `pupu_model_apply_gate_result()` and `PUPU_GATE_*` coexist with them, so the firmware still builds and Catch the Stars still plays at the end of M7a and M7b.

At the end of each sub-milestone, report Build / Host tests / Device tests / Unverified separately in the `AGENTS.md` format and ask whether to flash. The four sub-milestones can each be committed to `feature/pupu-v1` (ask for approval first), with commit messages per `docs/contribution/commit-and-pr.md`.

---

## 11. Documents to change

- `docs/assets/pupu/design.md` / `.zh_CN.md`: section 2 (five numbers, new decay), section 4 (prices), section 5 (pat, work, Sheep Gate), section 7 (level selection in the button table), section 9 (remove "coins and a shop" from "not in the first version"), section 12 (items 2, 6, 10, 11, 12, 13, 14 per this document). Change the title version to v2 and the date to 2026-10-25.
- `docs/assets/pupu/progress.md` / `.zh_CN.md`: insert **M7 Pupu v2 (pat, work, Sheep Gate)** in the milestone table; the old M7 WeChat mini program becomes **M8**, and the old M8 full-device acceptance becomes **M9**; update "Current state" and "Next step" to match; add the M7a–M7d records.
- `assets/images/pupu/README.md` / `.zh_CN.md`: add the new animations and icons to the file tables; remove the star and the cloud.
- This file: when done, just add one line "Implemented in commit xxxx" at the top; do not delete it.
- The two language versions must be aligned in the same change (`tools/check_repo.py` checks this).

---

## 12. Device acceptance checklist (M7d)

Flash the time-scaled build first (scale factor 60): emo's 4 hours take only 4 minutes, and one pet day takes only 24 minutes.

| Item | How to test | Expected |
| --- | --- | --- |
| Old save migration | On a device with an existing v1 save, flash the v2 firmware partition by partition without erasing NVS | Pupu's state is kept; the status page shows pocket money 30 |
| Six pat tiers | Pat 6 times in a row on the play page | 1–3: hand pats the head + happy hop; 4th: frown stays + "That's enough"; 5th: jumps away, hand knocked off, "Happiness −5"; 6th: no hand, no bubble |
| Window reset | Wait 10 minutes of pet time | The frown disappears; the next pat counts as the 1st again |
| Five work tiers | Work 6 times in a row | 1–2: catches the spreadsheet, +10 coins; 3rd: hit on the head + emo bubble; 4–5: hit on the head, +3 coins; 6th: "Can't work anymore" |
| Emo pose and priority | Go to the home page while emo; then wait for fullness to fall below 15 (a few minutes on the time-scaled build) and compare | Emo idle on the home page; lying down when very hungry |
| Emo cleared | Feed a star candy (also try at fullness ≥ 90) | Recovers immediately; at fullness ≥ 90 the star candy is still eaten |
| Emo clears naturally | Wait 4 minutes on the time-scaled build, or wake after deep sleep | Back to normal idle |
| No money | Spend all the money, then feed a rice ball | Head shake + "No money, go to work"; money unchanged |
| Sheep Gate Normal / Scholar | Play two rounds of each | Both card lines stay inside the card, not truncated; the card slides over when the countdown bar runs out; every wave shows "Best / Not bad / Poor"; the wolf-pack number slides in; the result card is correct |
| Performance log | Watch the serial output | Minimum frames per second ≥ 30, longest frame ≤ 40 ms, lane switch to redraw ≤ 50 ms, LVGL pool headroom ≥ 4 KB (Catch the Stars measured about 37 KB) |
| Font | Watch the serial boot log | `pupu_fonts_selfcheck` reports 0 missing characters |
| Sound effects | Listen | Slap, hmph, three falling notes, ding-ding, thud, sigh, and wolf howl are all distinguishable |

---

## 13. Later (not in this round)

- **M8 WeChat mini program**: the protocol is unchanged; implement it directly from `tools/pupu-sync-web/pupu_protocol.js`. Decide then whether to put pocket money and emo into STATUS (bit0 of `flags` is reserved for emo, `proto_ver` goes to 2). Nothing in this round depends on the phone.
- Replace the placeholder pixel art with final art (Piskel; the process is in `assets/images/pupu/README.md`).
- The numbers that may need tuning after playing on the device are all in `pupu_config.h` and the tuning struct at the top of `gate_model.c`.

---

<details>
<summary>Appendix: review simulation summary (why these numbers)</summary>

- **Decay**: v1 fullness −1 every 6 minutes is −80 over 8 hours, plus happiness −16 per hour when very hungry, so unless it was close to full before you left (even 100 / 100 leaves only 20 / 36), you usually pick it up the next day at 10 / 10. With −1 every 15 minutes, a session of one rice ball + three pats leaves about fullness 65–70 / happiness 25–45 after 8 hours.
- **Work cap**: without a cap, pressing 30 times after entering emo = +90 coins, hitting the maximum the next day. Capped at 5 per day, two works while emo = 6 coins = one rice ball, so a player at 0 coins who is both emo and hungry can still recover; there is no dead end. "−3 coins for working while emo" was rejected because a player at 0 coins could then only wait out the day.
- **Work uses allowance**: 0 / 1 / 2 / 3 works a day → daily bond cap 20 / 17 / 14 / 11 → about 5 / 6 / 7 / 9 days to grow up. Money and growth genuinely compete.
- **Pat bond stays +1**: three sessions of pats give 9 points + at most 9 from rice balls while hungry = 18 < 20, so the mini-game and sync still matter for growth; with +2 / +1 / +1 it would be 21 ≥ 20 and every other source would be pointless.
- **Sheep Gate threshold**: 2000 rounds simulated with the draft card pool: at "70% of the optimal path", one wrong wave wins 71% of the time and two wrong waves only 26%, and a mistake in the early waves and one in the late waves give opposite results. With the cumulative performance ratio `R ≥ 600` and the "second-best ≥ 80%" constraint, the difficulty is constant at "at most two second-best picks".
- **Static cards**: at the Catch the Stars speed of 131 px/s, an 80 px card is fully on screen for only 1.2 seconds, not enough to read three cards; static cards + a countdown bar give 4–8 seconds.
- **Emo uses a countdown, not "until the next day"**: the pet-day boundary ticks every 24 hours from the moment of hatching and is invisible in the UI, so the same three works would cost anywhere between 0 and 8 bond points at random.
- **Star candy bypasses the refusal**: the common order is feed first, then work; at fullness ≥ 90 the only shortcut out of emo would be blocked by "I'm too full" for an hour.

</details>
