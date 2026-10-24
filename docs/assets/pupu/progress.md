<p align="right">
  <a href="progress.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu v1 Development Progress

Update this document at the end of every milestone. The product design is in [design.md](design.md); milestone details are in section 10 of the development plan (outside the repository).

## Current state

- **Branch**: `feature/pupu-v1`, created from `006c227` on `origin/main`. It has no upstream tracking branch and is never pushed.
- **Done**: M0 preparation, M1 pet rule core, M2 first screen, M3 care actions, sound effects, save data. Most of the M3 on-device acceptance passes; the two remaining checks need the normal build, and you chose to commit first and run them at the first M4 flash (see the M3 section).
- **Next step**: M4 screen off, deep sleep, and continuous clock.
- **Commit state**: M0 (`2c77a38`), M1 (`dcf9342`), and M2 (`2ad5410`), and M3 are committed to the local `feature/pupu-v1`. The M3 commit hash is recorded in the M4 section (a commit cannot contain its own hash). Nothing is pushed.

## Milestones

| Milestone | Scope | Status |
| --- | --- | --- |
| M0 | Preparation: branch, first build, complete-gate baseline, documents in the repository | Done |
| M1 | Pet rule core | Done |
| M2 | First screen: see Pupu | Done |
| M3 | Care actions, sound effects, save data | Done (two device checks moved to M4) |
| M4 | Screen off, deep sleep, and continuous clock | Not started |
| M5 | Mini-game "Catch the Stars" | Not started |
| M6 | Bluetooth sync and Mac web page | Not started |
| M7 | WeChat mini program | Not started |
| M8 | Full-device acceptance and wrap-up | Not started |

## M0 preparation (2026-10-23)

### What was done

- Created the `feature/pupu-v1` branch.
- Confirmed that the 5 passport skills appear in Claude Code's available-skills list.
- First build of the unmodified main on this Mac: `idf.py set-target esp32c3 && idf.py build` passed, and it runs inside the sandbox.
- Ran the complete gate `./tools/validate.sh` outside the sandbox as the baseline.
- Moved the product design into the repository as `design.md` and `design.zh_CN.md` in this directory; created this progress document; created `README.md` and `README.zh_CN.md` at the repository root.

### Baseline build identity

The firmware source is identical to `006c227` on `origin/main`; M0 changed no firmware code.

| Item | Value |
| --- | --- |
| Firmware version (app descriptor) | `006c227`, ESP-IDF v5.5.3, no `-dirty` suffix |
| Local incremental image `build/FoloToy-AI-Passport.bin` | SHA-256 `596f24bc26f60865116f1effbd17978fb98e9f5d1c0fa69867bcaf2f6e03a2b7` |
| Complete-gate image `build/FoloToy-AI-Passport-full.bin` | SHA-256 `5831fc0bb324eac7f41430b2a77ca5a59ffadf87b1a3bb591fea3653f94aa70e` (1593152 bytes, flashed at `0x0`) |
| Matching ELF | SHA-256 `28aa1bdef91d4ccad6fe920507b2db57c51bacbf77cfde5ace336bf23bfa320f` |
| Debug bundle | `build/firmware/5831fc0bb324eac7f41430b2a77ca5a59ffadf87b1a3bb591fea3653f94aa70e/`, `archive_firmware.py verify` passed |
| Application size | 1527616 / 8323072 bytes (about 18% of the factory partition) |
| Image flashed to the device | None; M0 did not flash |

### Results

```text
Build: PASS (idf.py build passed inside the sandbox; validate.sh firmware build, merged image, partition layout, and bundle checks passed)
Host tests: PASS (validate.sh repository checks, actionlint, and all host tests passed; requires the two macOS workarounds below)
Device tests: NOT RUN (no device connected; M0 did not flash)
Unverified: the baseline firmware has not run on the device; validate.sh only completes on this Mac with the two workarounds below
```

### Notes

- The sandbox does not let git write `.git/config`, so the branch was created with `git switch --no-track -c` and has no upstream tracking. This does not affect development and also guards against an accidental upload.
- `tools/install_passport_skills.py --check` only checks `.agents/skills`, which Codex uses, so it always reports the skills as missing for Claude Code. Do not use it to decide whether the skills are installed.

### Two pitfalls when running validate.sh on this Mac (repository scripts unchanged)

The repository CI runs on Linux, so upstream has not hit these two problems. M0 did not change the repository scripts; it only worked around them outside the repository:

1. **actionlint checksum step fails**: the `/sbin/sha256sum` that ships with macOS 26 does not accept `--check --status`, so `tools/install-actionlint.sh` stops with `usage: sha256sum` right after the download.
   - Workaround: check `/tmp/actionlint-1.7.12/actionlint_1.7.12_darwin_arm64.tar.gz` by hand with `shasum -a 256`; the result must equal `aba9ced2dee8d27fecca3dc7feb1a7f9a52caefa1eb46f3271ea66b6e0e6953f` from the script, and only then extract `actionlint`. The script then uses the extracted binary directly. `/tmp` is cleared on reboot, so repeat this afterwards.
2. **Four demo runtime tests fail to link**: `validate.sh` passes the GNU linker flag `-Wl,--gc-sections`, and Apple's linker reports `unknown options: --gc-sections`.
   - Workaround: place a small script outside the repository that replaces that flag with Apple's equivalent `-Wl,-dead_strip` and passes every other argument to `/usr/bin/cc`, then run `CC=<script path> ./tools/validate.sh`. With the replacement, all four tests pass.
- Fixing both scripts in the repository so they also run on macOS is a possible later change. It edits upstream tool files, so it needs your approval first.

## M1 pet rule core (2026-10-23)

### What was done

- Added 4 pure-logic modules that use only standard C headers, with no ESP-IDF or LVGL includes:
  - `main/pupu_config.h`: every rule value in one place, to be tuned here after playtesting on the device.
  - `main/pupu_model.{c,h}`: pet state and rules. It covers per-minute decay, away catch-up (8-hour cap, floor of 10), the pet day and daily caps, the three foods, head pats, the snack and pat windows, mini-game settlement, the first-sync bonus, hatching, and growing up. Every action returns what actually changed, so a result screen can show it to the player directly.
  - `main/pupu_save_codec.{c,h}`: field-by-field save encoding. Fixed-width little-endian fields with a magic value, schema version, sequence number, length, and CRC32; one record is 67 bytes.
  - `main/pupu_clock.{c,h}`: real time to pet time (scale factor plus the sub-second remainder), reset-reason classification, the RTC continuity check, phone time sync, and `acct_now` accounting.
- Added 3 host tests, `tests/test_pupu_model.c`, `tests/test_pupu_save_codec.c`, and `tests/test_pupu_clock.c`, registered by hand in `tools/validate.sh`.
- Added the 3 new `.c` files to SRCS in `main/CMakeLists.txt`; the device's RISC-V compiler also builds them. Nothing in the firmware calls them yet, so **nothing changes on the device**.

### What the tests cover

Every item listed for M1 in the development plan has a matching test:

- Hourly decay (fullness −10, happiness −8); happiness −16 when very hungry, including the boundary at fullness 15 versus 14;
- The 8-hour catch-up cap; the catch-up floor of 10; a value already below 10 stays unchanged by catch-up;
- Crossing a pet day during catch-up resets the daily counters; after 3 days away, the values catch up only 8 hours while the day count advances 3 days;
- The 5 time scenarios from section 4.3, run on a "simulated device" through the full save, power loss or deep sleep, boot, and sync flow (the save really goes through encode and decode); plus 3 more scenarios: a second restart after power loss before the first save, deep-sleep wake on a device that has never synced, and time away under the time-scale factor;
- Daily caps: bond 20 (all sources combined), star candy 3, first sync +2;
- The 60-minute snack window (minutes 59 and 60 tested) and the 10-minute pat window (a dodge neither counts nor extends it);
- Refusing food when full (all three foods, nothing changes); a rice ball while hungry adds bond;
- Hatching; growing into an adult at bond 100 (triggered by feeding, pats, the mini-game, or sync, and only once);
- Mini-game settlement: quitting midway, fewer than 8 stars, a win, fullness clamped at 0, a fourth win in a day, and a full candy stock of 99;
- Save data: encode/decode round trip, detection of any single corrupted byte, wrong length, wrong magic, version migration, and rejection of future versions;
- CRC32 of "123456789" is `0xCBF43926`, matching Python `zlib.crc32`;
- Scale conversion and remainder carry; reset-reason classification (including USB and TASK_WDT); phone times before 2026 are rejected; syncing with a wrong time is corrected by the next sync.

A "mutation check" was also run: in temporary copies, 12 rules were deliberately broken (for example removing the floor of 10, removing the 8-hour cap, treating a USB restart as power loss, and checking the version before the CRC), and the tests failed every time. No repository files were changed.

### Save-format version migration

v1 is the first save version, so there is no older version to migrate yet. The tests therefore do the following:

- A byte-for-byte v1 save sample was generated **independently** with Python (`struct` + `zlib`) and embedded in the test. The firmware must read it back and also write exactly the same bytes. When the save format changes later, this sample is the proof that old saves still load, so it must not be deleted.
- A save from a future version reports FUTURE and leaves the output untouched, so the caller knows not to overwrite it.
- Version 0 is rejected; a corrupted record counts only as corrupted even if its version looks newer, so it cannot block overwriting.
- A real "upgrade an old version" test is written when v2 appears. At that point, all of the following are required together: bump the version, add a v1 branch in `decode_payload()`, and keep the v1 sample above with a test that it upgrades.

### Rule interpretations made while coding (you can overrule them)

The product design does not go into this much detail, so these follow the "relaxed companion" approach:

1. During the egg stage Pupu cannot be fed, patted, or played with, and gets no first-sync bonus (because "values do not change during the egg stage"). The first sync on the day it hatches still earns the bonus.
2. After becoming an adult, bond keeps increasing ("only goes up"), still at most +20 per day.
3. A cookie eaten while tired of cookies (the 4th or later in the window) still adds fullness and still counts; only refused cookies do not count.
4. Choosing star candy with none in stock shows "no star candy" first, whether or not Pupu is full.
5. Winning the mini-game when the candy stock is already 99: no candy is given, and it does not use up one of the day's 3.
6. Windows are timed in whole minutes: if the first cookie is eaten partway through a minute, the window is up to 1 minute shorter than 60 minutes.

### Interface notes for later milestones

- **Reset reason**: the values of `pupu_reset_reason_t` match `esp_reset_reason_t` in ESP-IDF 5.5.3 exactly. M4 device code must confirm this with `_Static_assert` before converting.
- **Session token**: a new token is generated only when the continuity check fails; when it passes, the token from the save is kept. `test_restart_before_first_save` covers a second restart after power loss before anything was saved.
- **Call order during sync**: `pupu_clock_sync()` → `pupu_model_catch_up(pupu_clock_to_pet(away_seconds))` → `pupu_model_apply_sync()`.
- **Left for M3**: choosing between the two save slots (newer sequence number and valid checksum; never overwrite a FUTURE record) belongs in `pupu_store`.
- **Left for M4 and M6**: `away_min` in STATUS and the "most recent time away" used by "Welcome back" are kept by the caller from the away seconds that `pupu_clock_boot()` and `pupu_clock_sync()` return; M1 does not store them.

### Build identity

| Item | Value |
| --- | --- |
| Source | `2c77a38` plus the uncommitted M1 changes |
| Firmware version (app descriptor) | `2c77a38-dirty`, ESP-IDF v5.5.3 (`-dirty` means there are uncommitted changes) |
| Local incremental image `build/FoloToy-AI-Passport.bin` | SHA-256 `58002a38bf65b00b3ea53b967cdbca11e2d63f41b691d967578f41c4e512c752` |
| Complete-gate image `build/FoloToy-AI-Passport-full.bin` | SHA-256 `64def7419d2643ba6fa902eb42ca392ad08b9379f394f3a697edda94a32eef9e` (1593152 bytes, flashed at `0x0`) |
| Matching ELF | SHA-256 `5cb61a2e5b1df1073980612d12d8b2e9b3bef152b2e64fe092cc92dd6e731632` |
| Debug bundle | `build/firmware/64def7419d2643ba6fa902eb42ca392ad08b9379f394f3a697edda94a32eef9e/`, `archive_firmware.py verify` passed |
| Application size | 1527616 / 8323072 bytes, the same as M0: nothing calls the new modules yet, so the linker drops them |
| Image flashed to the device | None; M1 did not flash |

### Results

```text
Build: PASS (idf.py build passed inside the sandbox, and the device compiler built the 3 new files without warnings; validate.sh firmware build, merged image, partition layout, and bundle checks passed)
Host tests: PASS (validate.sh repository checks, actionlint, and all host tests passed, including the 3 new tests; still requires the two macOS workarounds recorded under M0)
Device tests: NOT RUN (M1 is pure logic with no visible change on the device; nothing was flashed)
Unverified: the new rules are not wired into the firmware yet, so their effect on the device appears only in M2-M4; whether the RTC really stays continuous across deep sleep and the various restarts must be confirmed on the device in M4
```

## M2 first screen: see Pupu (2026-10-24)

### First task: on-device experiment

A temporary page compared two ways of drawing the same 48 × 48 test image (the experiment page has since been deleted):

| Comparison | A: 144 × 144 I4 canvas drawn at 1:1 | B: 48 × 48 RGB565A8 canvas stretched 3× |
| --- | --- | --- |
| Pixel accuracy | Exact: the red-green checkerboard edges are sharp, no visible difference from B | Exact (antialiasing turned off) |
| Redraw time per frame (whole 144 × 144) | **about 26 ms** | about 71 ms |
| Changing wool colour | Only palette slots 8–10 change; the black face stays black | The whole image is converted again |
| Transparent background | Background shows through correctly | Correct |

**Decision: use A (the I4 canvas)**, matching decision D7 in the plan. In the same experiment, the Noto font generated with Node 22 and lv_font_conv 1.5.3 rendered correctly on the device, and the battery icon rendered through the Montserrat 14 fallback. You confirmed that the ↑ key sits above the ↓ key and that the key numbering matches their positions (↑ is `BSP_BTN_UP`).

### What was done

- **Asset pipeline**: the palette `assets/images/pupu/pupu.gpl` (16 colours, each slot with a fixed job); `tools/make_placeholder_art.py` draws placeholder art from simple shapes (egg idle, egg wobble, hatch, young idle, young lying down hungry, adult idle: 6 animations, 13 frames); `tools/gen_pupu_assets.py` turns the PNG strips into `main/pupu_assets.{c,h}` with an enum name for every animation and frame, and writes the preview `build/pupu-preview.png`; `tests/test_pupu_assets_fresh.py` catches art that changed without regenerating. See `assets/images/pupu/README.md`.
- **Font**: Noto Sans SC Regular static OTF (8,331,336 bytes, SHA-256 `faa6c9df…5ea9`, OFL 1.1) downloaded to `assets/fonts/pupu/src/` (in `.gitignore`, not committed); the character list `assets/fonts/pupu/charset.txt`; the generated `main/fonts/pupu_font_16.c` (16 px, 4 bpp, uncompressed, 134 glyphs); the license `assets/fonts/pupu/OFL.txt`. `tests/test_pupu_charset.py` checks that every Chinese character in source strings is in the character list and the font, with a deliberate missing-character negative case; at boot `pupu_fonts_selfcheck()` checks again on the device. See `assets/fonts/pupu/README.md`.
- **Pure logic**: `pupu_sprite_math` (8 wool tint steps, palette blending, 3× enlarging copy) and `pupu_input` (key mapping, the egg's 3-second hold timer with a 150 ms bounce grace, phantom-key filtering), each with a host test.
- **Device code**: `pupu_app` (50 ms main loop, 16-entry key queue, clock wiring, page switching, memory logs), `pupu_pet_view` (pet canvas), `pupu_ui_egg` (egg page), `pupu_ui_home` (home page), `pupu_ui` (shared colours and helpers), `pupu_fonts` (font with fallback and self-check), and `pupu_text.h` (all on-screen text).
- **`main.c`**: boots straight into Pupu; the demo menu is no longer compiled. `demo_*.c` and `ui_pixel*.c` were removed from `main/CMakeLists.txt` but the files are kept; `demo_radio.c` is still compiled and initializes NVS (never erasing on failure). `on_key` still calls `key_guard_consume(` before checking `!s_input_ready`.
- **Configuration**: `main/Kconfig.projbuild` adds `PUPU_TIME_SCALE` (1–120, default 1); `sdkconfig.dev-fast` sets 60; `CONFIG_LV_MEM_SIZE_KILOBYTES` in `sdkconfig.defaults` went from 24 to 48. The local `sdkconfig` was deleted afterward and grep after the rebuild confirmed 48 / scale 1 for the normal build and 48 / scale 60 for the fast build.
- All four new tests are registered in `tools/validate.sh`.

### Decisions made while coding (you can overrule them)

1. The egg page shows warming progress as a **bar**, not the "progress ring" in the plan. An arc needs masked drawing; the bar is two plain rectangles and uses less memory.
2. OK on the home page currently shows a "not ready yet" bubble for 2 seconds: feed, play, and status arrive in M3, sync in M6. A long OK press does nothing yet; screen-off arrives in M4.
3. The bottom bar shows the previous item, the current item (dark pill), and the next item, with four small dots for position; ↑/↓ stop at the ends instead of wrapping.
4. Phantom keys: a press of a different key while another key is held, or within 120 ms of its release, is treated as a misread and ignored. Repeated presses of the same key are unaffected.
5. All on-screen text lives in `main/pupu_text.h`. Chinese text in serial logs does not need glyphs, so the character check skips strings inside `ESP_LOG…(…)`; every other Chinese string is checked as if it could reach the screen.
6. The character list already includes the characters M4 needs for the "welcome back, away for X hours" message.
7. The battery level is read every 30 seconds in the main loop (one I2C read, about 1 ms); no number is shown when it cannot be read.
8. Every boot starts with a new egg: save data arrives in M3.

### On-device acceptance (2026-10-24, time-scaled build, scale 60)

| Check | Result |
| --- | --- |
| Boots straight to the egg; the demo menu can no longer be reached | Pass (observed by you; the demo menu code is no longer compiled) |
| Hold OK 3 s to hatch; releasing early cancels | Pass: the egg wobbles and a progress bar fills while held; releasing cancels; after 3 s the hatch animation plays and the home page opens |
| Home page | Pass: battery level at top right; ↑/↓ move the bottom bar; OK shows the bubble |
| Wool greys with the time scale on | Pass: about 4 minutes after hatching the hungry bubble appears, about 6.5 minutes after hatching Pupu lies down, and the wool is clearly grey while the face stays black |
| Chinese text renders | Pass: the font self-check found glyphs for all 29 non-ASCII characters; no boxes on screen |
| Memory logs | Normal, see the table below; no errors, warnings, or restarts |

The pet status logged every minute (real seconds since boot): at 60 s still an egg at 80/80 (step 1); after hatching, 120 s 70/72 (step 2), 180 s 60/64 (step 3), 300 s 40/48 (step 4), 360 s 30/40 (step 5), 420 s 20/32 (step 6). This is exactly −10 fullness and −8 happiness per pet hour.

| Memory (fast build on the device) | LVGL pool used | Pool peak | Largest free pool block | Free system heap | Largest internal DMA block |
| --- | --- | --- | --- | --- | --- |
| Boot | 13% | 6,280 B | 41,300 B | 205,704 B | 114,688 B |
| Egg page | 15% | 7,108 B | 40,232 B | 205,704 B | 114,688 B |
| Home page | 19% | 9,556 B | 37,428 B | 209,172 B | 114,688 B |

The 48 KB pool currently peaks under 10 KB. The "20 page round trips without the minimum dropping" check waits until M3 adds more pages.

### Interface notes for later milestones

- **Adding a page**: declare create / action / update in `pupu_ui.h` and wire them into `page_t` and `show_page()` in `pupu_app.c`. Only one screen exists at a time; a page with timers or animations must stop them before its screen is deleted.
- **The pet canvas** has a single static buffer: every page calls `pupu_pet_view_create()` again. Frame durations live in `ANIMS` at the top of `tools/gen_pupu_assets.py`; the eat, happy, dodge, refuse, and wave frames for M3 are added as more strips in the same format, then regenerated.
- **Adding Chinese text**: update `charset.txt` and regenerate the font first (command in `assets/fonts/pupu/README.md`), then add the string to `PUPU_TEXT_LIST` in `pupu_text.h`.
- **Keys**: `pupu_input` has an egg mode and a normal-page mode. The M5 mini-game needs a third mode (OK press pauses immediately; OK is ignored while ↑/↓ is held).
- **Clock**: every 50 ms the main loop passes the real time difference to `pupu_clock_tick()` and then calls `pupu_model_advance()`; boot catch-up and sync are not wired yet (M4, M6).
- **The 26 ms measured in the experiment** is a full 144 × 144 redraw at the debug optimization level (`-Og`). Pupu in the mini-game is drawn at 1× and is not limited by it.

### Build identity

| Item | Value |
| --- | --- |
| Source | `dcf9342` (M1) plus the uncommitted M2 changes |
| Firmware version (app descriptor) | `dcf9342-dirty`, ESP-IDF v5.5.3 |
| Local incremental image `build/FoloToy-AI-Passport.bin` (scale 1) | SHA-256 `6f799d144da684881460f1d5aa8e7238f497913596ff68ad3b9a3139faa27c12` (653,120 bytes) |
| Fast build `build-fast/FoloToy-AI-Passport.bin` (scale 60) | SHA-256 `5f7b74537ed1550d6ff3acef8652da7159d987feca514732e3e9b3c30c0b31a0` (653,120 bytes) |
| `build/FoloToy-AI-Passport-full.bin` from the complete gate | SHA-256 `ab34fb38ff0baf31169f48e799ac68e839f24f9e649465d756d422aa6fab9504` (718,656 bytes, flash from `0x0`) |
| Matching ELF | SHA-256 `985d9ee4a352bdb73faebb986746b13310b0758e73cf33a262c5b3b071db8b11` |
| Debug archive | `build/firmware/ab34fb38ff0baf31169f48e799ac68e839f24f9e649465d756d422aa6fab9504/`, `archive_firmware.py verify` passes |
| Application size | 653,120 / 8,323,072 bytes (about 8% of the factory partition). Smaller than M1 because the demo menu and the Wi-Fi and Bluetooth demos are no longer compiled |
| Images flashed to the device | 1) the experiment page (deleted), app SHA-256 `20b0a38ea94fbf486290db153eb59410d62691300067a1f665e8b94bf72851d3`; 2) **the device now runs the fast build** `5f7b7453…31a0`. Both were segmented esptool writes of the bootloader at `0x0`, the partition table at `0x8000`, and the app at `0x10000`, leaving NVS (`0x9000`) and PHY calibration (`0xF000`) untouched |

### Results

```text
Build: PASS (normal and fast builds compile in the sandbox without warnings; validate.sh firmware build, merged image, partition layout, and archive verification pass)
Host tests: PASS (validate.sh repository checks, actionlint, and all host tests pass, including the 4 new tests; still needs the two macOS workarounds recorded under M0)
Device tests: PASS (fast build: boot to the egg, 3 s hold to hatch, release cancels, home battery and bottom bar, wool greying, hungry bubble, lying down, Chinese text, and memory logs all confirmed by you on the device)
Unverified: the normal build (scale 1) has not been flashed; the battery percentage was not compared with the real charge; the memory trend over repeated page changes waits for M3; save data, screen-off, and deep sleep are not implemented yet
```

### Notes

- The first complete-gate run failed once: the character check found three Chinese memory-log labels in `pupu_app.c` that are not in the font. They never reach the screen, but the check cannot tell, so they became English labels. This shows the check works.
- Flashing and reading the serial port open `/dev/cu.usbmodem101`, which the sandbox blocks, so those commands run outside the sandbox (you are asked to approve). Opening the serial port restarts the device once.

## M3 care actions, sound effects, save data (2026-10-24, done, two device checks moved to M4)

### What was done

- **Pages** (all newly designed; keys follow section 7 of the design: Up/Down select, OK confirms, holding OK returns home):
  - OK on the home page opens Feed, Play, or Status; Sync still shows the "not ready yet" bubble (M6). Coming back from another page, the bottom bar stays on the item chosen last.
  - **Feed page**: Pupu in the middle, rice ball, cookie, and star candy in the bottom bar, and the star candy stock ("star candy x N") in the top-right corner. Confirming plays a 2-second chewing animation and sound, and the bubble shows what actually changed, for example "Fullness +35  Bond +3". From the fourth cookie on it says "bored of these  Fullness +10"; when too full Pupu shakes its head ("can't eat any more"); with no candy it says "no star candy".
  - **Play page**: pat the head (a happy hop and a snort, "Happiness +5  Bond +1"; from the fourth pat on Pupu dodges, "stop patting!") and Catch the Stars ("not ready yet", built in M5).
  - **Status page**: fullness and happiness (number plus bar) and star candy; a young Pupu also shows "growth x/100".
  - When bond reaches 100 the bubble says "all grown up!" with a growth sound, and the adult Pupu appears.
- **Animations**: 9 new animations with 18 frames (eat, happy, dodge, and head shake for the young and the adult Pupu, plus the adult lying down when very hungry). There are now 15 animations, 31 frames, and 35,712 bytes of pixels. The original 6 strips are byte-for-byte unchanged.
- **Sound effects, `pupu_sfx`**: the tamagezi synthesizer design (16 kHz mono, square, triangle, and noise waves with fade-in and fade-out), without its per-species pitch table. Five sounds: chew, happy, "no" (head shake, dodge, no candy), hatch, and grow. A stop handshake, `pupu_sfx_stop()`, is ready for M4 to use before deep sleep.
- **Save data, `pupu_store`**: NVS namespace `pupu`, two slots `slot_a` and `slot_b` written alternately by a background task. **NVS is never erased.**
- **Pure logic, `pupu_save_policy`**: which slot to load, which slot to write next, and when to save. It has a host test, `tests/test_pupu_save_policy.c`, registered in `tools/validate.sh`.
- **Font**: 37 new characters (including the multiplication sign `×`, U+00D7); the regenerated font has 171 glyphs (95 ASCII plus 76 Chinese characters and symbols).
- **Configuration**: `sdkconfig.defaults` adds `CONFIG_I2S_ISR_IRAM_SAFE=y`. Following the rule, the local `sdkconfig` and `build-fast/sdkconfig` were deleted before building, and grep confirmed both builds: normal 48 KB / IRAM-safe / scale 1, time-scaled 48 KB / IRAM-safe / scale 60.
- `main.c` starts the sound effects before Pupu starts; if audio fails to initialize, the game still runs, just silently.

### How saving works

- **Load**: at boot both slots are read and the valid one with the newer sequence number wins; the next save goes to the other slot. Sequence numbers are compared in a wrap-safe way. Pupu stays as it was when saved (as if the power had been cut); catching up on time away is M4.
- **When to save** (real time, not affected by time scaling):
  - about 1 second after feeding, patting, hatching, or growing up, and **only after the sound has finished** (plus 150 ms for the audio still in the DMA buffer);
  - every 5 minutes while running;
  - once when the battery first drops below 5%;
  - not when only the selection moves, and not after a refusal or a dodge. Saving before screen-off and deep sleep is M4.
- **Priorities**: sound task 5 > LVGL 4 > save task 2. The UI and keys never wait for Flash.
- **When a save cannot be read**:
  - save data exists but every slot is damaged: start from a new egg, and the egg page first shows "save data was damaged, starting over" for 4 seconds.
  - a save written by newer firmware: show "save data is too new, not saving this time" and write **nothing** during this boot; if the other slot is readable, play with it as usual.
  - a write fails (for example NVS is full): log an error and write the same slot next time, never erasing.

### What the tests cover

`test_pupu_save_policy.c`: first boot; the newer slot wins; sequence wrap-around; one damaged slot (4 kinds of damage); both damaged; future-version lock (including "a missing key does not count"); alternating writes; a simulated power cut halfway through a write, using the real encoder and decoder, which falls back to the last complete save and overwrites the damaged slot next; the 1-second debounce; waiting for sound to finish; the 5-minute timer; millisecond wrap-around; the battery dropping below 5% saves once, and again only after recharging.

Mutation check: 10 rules were deliberately broken in a temporary copy (for example no wrap-safe comparison, picking the older slot, ignoring future versions, not waiting for sound, repeated low-battery saves, overwriting the slot just loaded), and the test failed every time. The repository files were not changed.

### Decisions made while coding (you can overrule them)

1. The Feed and Play bubbles show the actual numbers that changed. The main number (fullness for rice and cookies, happiness for candy and pats) is always shown, even when it is +0; the others only when they changed. The home page still shows no numbers.
2. While Pupu is reacting (2 seconds for eating, 1.2 seconds for happy, dodge, or head shake), OK is ignored; Up/Down and hold-to-return still work.
3. With no star candy, only the bubble and the "no" sound appear; Pupu does not shake its head, because it is not refusing.
4. The star candy stock sits in the top-right corner of the Feed page instead of the bottom bar (bar items are at most 3 characters).
5. Sub-pages show their title in the top-left corner and a "hold OK to go back" hint at the bottom.
6. The Status page fits on one screen, so Up/Down do not page. Growth progress is hidden for an adult (design section 12, rule 10).
7. Moving the selection plays no sound (the design does not ask for it). Volume is fixed at 55%, as in tamagezi.
8. An adult Pupu also lies down when very hungry. In M2 only the young Pupu did; design section 2 does not distinguish stages.
9. The session token is written as 0 for now; the RTC time at save is written as usual. M4's continuity check will treat 0 as "the power was cut", so nothing is caught up by mistake.
10. An NVS record larger than 256 bytes can only come from newer firmware, so it is treated as "too new" and never overwritten.

### On-device acceptance (2026-10-24, time-scaled build, scale 60)

Flashing: with your approval, esptool wrote the time-scaled `build-fast/FoloToy-AI-Passport.bin` (SHA-256 `4573b393…b782`, checked before flashing) in segments: bootloader at `0x0`, partition table at `0x8000`, application at `0x10000`; all three passed verification. NVS (`0x9000`) and RF calibration (`0xF000`) were not erased or written. A 35-minute serial log was recorded on the computer (you power-cycled once in the middle). Numbers were checked against the per-action records in that log.

| Check | Result |
| --- | --- |
| Loading the save at boot | Pass: the first boot found no save and started from a new egg; every later boot loaded the slot with the newer sequence number, so Pupu was not a new egg |
| Alternating slots | Pass: sequence numbers 1 to 20 were written to slot_a and slot_b in turn, with no failed write |
| When saves happen | Pass: about 1 second after each care action; none after a refusal; one every 5 minutes (at 300.5, 625.5, 925.5, 1225.6, and 1525.6 seconds) |
| Rice ball | Pass: 82 to 100 shown as "+18" (capped); fed when hungry (fullness 38): Fullness +35 and Bond +3 |
| Refusing when full | Pass: at fullness 90 or more, rice and cookies were refused and nothing changed |
| Cookie | Pass (log): the first 3 gave Fullness +10 and Happiness +5, the fourth was "bored of these" (Fullness +10, no happiness). The "can't eat any more" you saw last came from the fifth and sixth cookies, because fullness was 92 after the fourth |
| No star candy | Pass: correct message, nothing changed |
| Pat rewards | Pass: Happiness +5 and Bond +1 each time; +4 when happiness capped at 100 |
| Decay | Pass: fullness -10 and happiness -8 per real minute, stopping at 0 |
| Memory | Pass: for 26 continuous minutes after the power cycle, free heap stayed at 191,516 bytes and the lowest-ever heap at 186,128 bytes; in the 8.5 minutes before it, about 20 page switches always brought the LVGL pool back to 19% on the home page. The whole log has no error or warning |
| Fourth pat dodges | **Not seen on the device**: no dodge in either try. The first time, 5 pats were about 3.5 seconds apart and the first to fourth spanned 11.7 seconds; in the retest, 10 pats were about 3.2 seconds apart and the first to fourth spanned 10.1 seconds. The window is 10 pet minutes counted down in whole minutes, which is really 9 to 10 real seconds in the time-scaled build, too short at a normal pace. The rule is covered by the host tests; check it again on the normal build (where the window is 10 real minutes) |
| Power off and on | Pass (retest): the last save (sequence 36) had fullness 83; you saw 82 on the Status page (about 5 seconds after the save; the time-scaled build loses 1 every 6 seconds), and after power-off and on it was 83, exactly the last save. In the first try fullness went from about 74 back to 91 because that save was 2 minutes before power-off (2 pet hours in the time-scaled build) |
| Save survives a partial flash | **Not tested**: needs another flash. You chose to commit first without flashing; it moves to the first M4 flash |
| No crackle when saving during sound | Pass: in the retest, 10 pats each saved about 1 second after the sound, and you heard no noise |
| New animations and characters | Pass: you checked them and they look normal, with no box characters. You made no comment on volume |

**Lesson for time-scaled testing**: every pet-time window shrinks 60 times in the time-scaled build (the pat window is 10 seconds, the cookie window 1 minute), and the time lost when a power cut falls back to the last save grows 60 times (1 real minute = fullness -10). Press quickly for these checks, and trigger a save before powering off. In the normal build (scale 1) a power cut loses at most 5 minutes, less than 1 point of fullness, and M4 also saves when the screen dims and turns off.

### Interface notes for later milestones

- **Page interface**: every page's action and update return a `pupu_ui_req_t` (`PUPU_UI_CHANGED` asks for a save, `PUPU_UI_GO_*` switches pages). To add a page, extend `page_t`, `show_page()`, `handle_action()`, and `update_page()` in `pupu_app.c`. The bottom bar and the bubble live in `pupu_ui.c` and can be reused directly.
- **M4 screen off and deep sleep**:
  - Before deep sleep the save must be synchronous (wait until written). `pupu_store` only has the asynchronous `pupu_store_save()` now, so M4 needs a wait-until-written call, and the "a save is waiting to be written" busy check is also M4.
  - Before deep sleep call `pupu_sfx_stop()` first, then `bsp_audio_sleep()`. Use `pupu_sfx_busy()` for the sound-playing busy check.
  - The session token is set in `save_now()` in `pupu_app.c` and is 0 for now.
- **M5 mini-game**: while a game is running (including paused) there are no timed saves; add that to the `busy` argument of `pupu_autosave_due()`. Star candy comes from winning the game, so feeding candy is tested then.

### Build identity

| Item | Value |
| --- | --- |
| Source | `2ad5410` (M2) plus the M3 changes, which is the M3 commit. Both builds were rebuilt before committing and the image SHA-256 values matched those below, so the committed source is the tested source |
| Firmware version (app descriptor) | `2ad5410-dirty`, ESP-IDF v5.5.3 (the images were built before the commit) |
| Local incremental image `build/FoloToy-AI-Passport.bin` (scale 1) | SHA-256 `4f5ec2f321543a3ed9219a8a546ad2553742cb5936165f0a5a3717540269a231` (757,088 bytes) |
| Time-scaled `build-fast/FoloToy-AI-Passport.bin` (scale 60) | SHA-256 `4573b3934d16f02e6fe1f4daaf3a391c25c6cbdbbba09fb3a87bd9a0adb4b782` (757,088 bytes) |
| Complete-gate `build/FoloToy-AI-Passport-full.bin` | SHA-256 `b0ca950858be250ebd9eb32b5762fde8213a4ea7ca0b331b7a211f0e9c1f290d` (822,624 bytes, flash at `0x0`) |
| Matching ELF | SHA-256 `c2c70ce693232b2a165ab6230e2e728873f9a78eef5b7a6a2bb7c9a6e160d1cf` |
| Debug archive | `build/firmware/b0ca950858be250ebd9eb32b5762fde8213a4ea7ca0b331b7a211f0e9c1f290d/`, `archive_firmware.py verify` passes |
| Application size | 757,088 / 8,323,072 bytes (about 9% of the factory partition), 103,968 bytes more than M2: audio driver, NVS writes, new animations, and new characters |
| Image flashed to the device | **Time-scaled build** `4573b393…b782` (ELF SHA-256 `fdb0fe583c8bda8e8b254ae3fb72d783a206ad5e3b2197af6152564c82fae1bf`), written by esptool in segments at `0x0`, `0x8000`, and `0x10000`, without touching NVS or RF calibration. The normal build (scale 1) was not flashed |

### Results

```text
Build: PASS (normal and time-scaled builds pass in the sandbox with no warnings; validate.sh firmware build, merged image, partition layout, and archive checks pass)
Host tests: PASS (validate.sh repository checks, actionlint, and all host tests pass, including the new test_pupu_save_policy; still needs the two macOS workarounds from M0)
Device tests: PASS (only the items marked Pass above: loading the save at boot, alternating slots, save timing, rice ball, refusal, cookies, no star candy, pat rewards, decay, 26 minutes without memory loss, power off and on, no crackle while saving, and the new animations and characters; numbers checked against the serial log, sound and visuals confirmed by you)
Unverified: fourth pat dodges (the window is only 9 to 10 seconds in the time-scaled build and was not reached); save survives a partial flash; the normal build (scale 1) has not been on the device. These three move to the first M4 flash. Feeding star candy waits for M5
```

### Notes

- The first build missed one header and failed to compile; fixed.
- Self-review found one hazard: if the sound task failed to start, "sound busy" would stay true forever and saves would never happen. It now falls back to "no sound" when the task cannot start. The complete gate and both local images in the table were built after this fix.
- When building inside the sandbox, the component manager tries to reach `components-file.espressif.com` to check for updates and is blocked. The build is not affected; this can be ignored.
- The serial log was recorded by a small time-limited reader script, not the interactive monitor. Opening the port restarts the device once (`USB_UART_CHIP_RESET` in the log); the reset reason is USB and the save is not affected.

## Next step

1. Do **M4 screen off, deep sleep, and continuous clock** (section 10 of the plan). Record the M3 commit hash in the M4 section first.
2. **Finish the last two M3 checks at the first M4 flash**: flash the normal build (scale 1) in segments (needs your approval); after booting, the save should still be there. Then pat 4 times within 10 minutes; the fourth should dodge with "stop patting!".

Running the complete gate still needs the two macOS workarounds recorded under M0 above.
