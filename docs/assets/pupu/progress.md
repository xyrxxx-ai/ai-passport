<p align="right">
  <a href="progress.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu v1 Development Progress

Update this document at the end of every milestone. The product design is in [design.md](design.md); milestone details are in section 10 of the development plan (outside the repository).

## Current state

- **Branch**: `feature/pupu-v1`, created from `006c227` on `origin/main`. It has no upstream tracking branch and is never pushed.
- **Done**: M0 preparation, M1 pet rule core.
- **Next milestone**: M2 first screen: see Pupu (needs the device).
- **Commit state**: M0 (`2c77a38`) and M1 are both committed to the local `feature/pupu-v1`. The M1 commit hash will be recorded in the next update (a commit cannot contain its own hash). Nothing is pushed.

## Milestones

| Milestone | Scope | Status |
| --- | --- | --- |
| M0 | Preparation: branch, first build, complete-gate baseline, documents in the repository | Done |
| M1 | Pet rule core | Done |
| M2 | First screen: see Pupu | Not started |
| M3 | Care actions, sound effects, save data | Not started |
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

## Next step

M2 first screen: see Pupu. The first task is an on-device experiment (I4 canvas recoloring, RGB565A8 scaling, and one line of Chinese text), which **requires your approval to flash first**. Before starting M2, please prepare:

- A data-capable USB-C cable, with the device plugged into the computer before opening the new conversation;
- A check of whether the ↑ key sits above the ↓ key on the device (the mini-game direction depends on it);
- Approval to download the font files when asked (a few MB, needs the network).

Running the complete gate still needs the two macOS workarounds recorded under M0 above.
