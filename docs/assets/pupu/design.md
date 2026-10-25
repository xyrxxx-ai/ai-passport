<p align="right">
  <a href="design.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu · Design (v2)

Status: final · v1 2026-10-23, v2 2026-10-25 · Target device: FoloToy AI Passport (240 × 320 portrait screen, ↑ ↓ OK buttons, iPhone user)

> **In one sentence**: a little lamb spirit that lives in the passport and hatched from an ink egg. You feed it, pat it, and play "Sheep Gate" with it; it works for pocket money to buy its food, and too much work makes it emo; it slowly grows up, and when it connects to the phone it learns how long you were away and syncs its status to the phone.

v2 (2026-10-25) changed these things: slower decay, new pocket money and work, feeding costs money, six pat levels, emo, and the mini-game replaced by "Sheep Gate". The reasoning behind the numbers and the implementation checklist are in [v2-handoff.md](v2-handoff.md).

---

## 1. The pet

| Item | Setting |
| --- | --- |
| Name | **Pupu** |
| Origin | A little lamb spirit that hatched from the passport's "ink egg". When the shell cracked, it got stamp ink all over its face, so its face and four little hooves are black |
| Look | Based on a black-faced sheep: a fluffy, curly white fleece body, black face, black ears, black hooves, and two small curled horns. It wears a small piece of eggshell on its head as a "hat", the sign that it hatched from an egg. 48 × 48 pixel art, shown at 3× scale on screen |
| Signature trait | **The fleece color follows its mood**: fluffy cream when happy, slowly turning dull gray when hungry or bored. You can tell how it is doing from the fleece, without reading numbers |
| Personality | Curious, a bit greedy, and likes company. When happy it gives a little "puh" snort through its nose, which is where its name comes from |
| Growth | Egg → baby → adult, 3 stages in total |
| Difficulty | **Easy companion**: it never dies and never gets sick. Neglect only makes it unhappy and slows its growth; feeding it and playing with it brings it back |

- **Avoid lookalikes**: there are already well-known animated black-faced white sheep (for example Shaun the Sheep). The pixel art must stand clearly apart from them through the eggshell hat, the curled horns, and the round, puffy body shape.

## 2. State: five numbers

All times are **pet time** (real seconds × a scale factor; the development build can be accelerated).

| State | Range | How it changes | How it shows on screen |
| --- | --- | --- | --- |
| Fullness | 0–100 | −1 every 15 minutes (−4 per hour). At most 8 hours are caught up after being away = −32, exactly one rice ball | Below 40 a "hungry" bubble appears; below 15 it lies down and stops moving |
| Happiness | 0–100 | −1 every 20 minutes (−3 per hour); −2 every 20 minutes when very hungry (fullness < 15) | Shown through body color and facial expression |
| Bond | Only goes up | Increases through care; all sources combined add **at most +20 per day**; **work uses up part of the day's allowance** | Decides when it grows up. Not shown on the home screen; during the baby stage the status page shows it as "growth progress x/100", and the phone shows it too |
| Star candy | 0–99 | Only won through the mini-game, at most 3 per day | Count shown on the status and feed pages |
| Pocket money | 0–99 | Only earned by working. A newly hatched Pupu has 30; an upgraded old save also gets 30 | Shown on the status and feed pages |

- The home screen **shows no numbers**, only expression and color, plus at most one need bubble. Exact numbers are on the "Status" page.
- **How it grows**: the egg must be warmed by holding OK for 3 seconds before it hatches (no repeated pressing, because the button driver merges rapid presses into a single double-click). A baby that collects 100 bond points grows into an adult; with normal care and no work this takes about 5 days at the fastest, about 7 days when it works twice a day (time when the device was switched off with the power button and never synced does not count toward days).
- All numbers are provisional and will be tuned after play testing.

## 3. Time rules (the most unusual part of this device)

This device **has no clock that keeps running**. After it is switched off with the power button, it does not know how much time passed outside.

| Situation | What happens to the pet |
| --- | --- |
| Powered on, screen on | Time runs normally and the state slowly drops |
| Auto-dim, screen off | The screen dims after 20 seconds without a button press and turns off after 45 seconds. The chip stays on and time keeps running |
| Deep sleep 5 minutes after the screen turns off | The chip's internal timer keeps running (stated in the official ESP-IDF documentation, **still to be confirmed on the device**). When any button wakes it, the "time away" is caught up using the same rules as a sync |
| Switched off with the power button | The pet is "frozen" and keeps the state it had before power-off |
| Synced with the phone | The phone tells it the current time, and it catches up on the time you were away. **At most 8 hours are caught up**, and the caught-up part only lowers a value down to 10, never to 0. So even after a week-long trip, it is only hungry and missing you |

## 4. Feeding (costs pocket money)

| Food | Price | Effect | How to get it |
| --- | --- | --- | --- |
| Rice ball (staple) | 5 coins | Fullness +35 | Buy it whenever there is money |
| Cookie (snack) | 3 coins | Fullness +10, happiness +5; from the 4th cookie within 1 hour it gets bored of them and happiness no longer increases, but the price is still charged | Buy it whenever there is money |
| Star candy (rare) | Free, uses 1 candy | Happiness +25, bond +2; **eaten even when too full while emo**, as the only way to end emo early | Only won through the mini-game |

- Feeding a rice ball while it is hungry (fullness below 40) gives bond +3.
- When it is already very full (fullness 90 or more) it **shakes its head and refuses**. Nothing is charged or deducted; it is just a fun reaction.
- Without enough money it shakes its head with the bubble "No money, go to work" (in Chinese) and nothing changes.
- While emo it refuses cookies ("no appetite", nothing charged); rice balls are eaten as usual.
- Prices are per serving: a rice ball at fullness 89 only fills to 100 and still costs 5.
- Eating plays a chewing animation of about 2 seconds with a short sound effect. The top-right corner of the feed page shows "pocket money N  candy ×N", and the selected food's label dims when it cannot be afforded.

## 5. Play

**1. Pat (over in a few seconds)**

A 10-minute window: the first pat opens it, and the count resets when it expires. Within the window, by pat number:

| Pat | Happiness | Bond | Pupu | Picture and sound |
| --- | --- | --- | --- | --- |
| 1, 2, 3 | +5 | +1 | Happy hop | A hand drops from above onto its head, pats once and lifts; "pat" |
| 4 | 0 | 0 | Annoyed (frown, head tilted), kept until the window ends | The hand drops; bubble "Enough"; "humph" |
| 5 | −5 | 0 | Angry (hops away) | The hand drops and is bounced away; bubble "Stop patting me!  happiness −5"; three falling notes |
| 6 onward | 0 | 0 | Turns away | No hand, no bubble |

Every press within the window counts, saturating at 5. While emo, pats are only dodged ("not talking to you"); they are not counted and do not open the window.

**2. Work (new)**

Counted per pet day and reset when the day rolls over:

| Job of the day | Pocket money | Happiness | Day's bond allowance | Pupu | Picture and sound |
| --- | --- | --- | --- | --- | --- |
| 1, 2 | +10 | −5 | 3 points used (bond itself unchanged) | Holding a sheet, sweating | An Excel sheet falls from the top of the screen and Pupu catches it; "ding ding" |
| 3 | +10 | −5 | 3 points used | Knocked out (lying down, stars circling) | The sheet hits its head, "thud"; then it goes emo with the bubble "Exhausted… resting 4 hours" |
| 4, 5 | +3 | −10 | 0 | Knocked out | "Thud", bubble "Got the sheet wrong…  pocket money +3  happiness −10" |
| 6 onward | 0 | 0 | 0 | Shakes its head | Bubble "Can't work anymore" |

Which job it is depends only on how many jobs were done today, not on whether it is emo; pocket money caps at 99 and the allowance can be used up to 20.

**Emo**: starts when the third job of the day ends and lasts 4 hours of pet time (the third job always resets it to 4 hours). The idle animation changes to a drooping Pupu under a little storm cloud on the home, feed, and play pages; pats are only dodged, cookies are refused, rice balls are eaten, work follows the table above, and the mini-game plays and rewards as usual but does not end emo. Only eating a star candy ends it immediately. Waking from deep sleep or power-off after more than 4 hours always finds it over.

**3. Mini-game "Sheep Gate" (at most about 50 seconds per round)**

- Pick a level first (↑↓ for "Normal / Scholar", OK to start), then a 3-2-1 countdown, then 6 waves.
- Pupu is on the left side of the screen with 3 lanes stacked vertically; each wave shows three **static** formula cards, one per lane, and a countdown bar under the top bar runs from full to empty. When time is up, the card in Pupu's lane slides over and takes effect: the flock N (starting at 10, range 1–999) becomes the card's result.
- The three cards of a wave always evaluate to "best > second > third", the second at least 80% of the best and the third at most 60%; the written form is random, so the card that "looks biggest" is not necessarily the best and you have to work it out. Each applied card gives immediate feedback: "Best! / OK / Worse".
- Normal: waves 1–2 add, subtract, multiply, divide; waves 3–4 two-step (×2 −15, +9 ×2, +40%, −25%, ×1.5); waves 5–6 square roots, powers, factorials. Scholar: waves 1–2 two-step; waves 3–4 integrals and derivatives; waves 5–6 sums, powers, ×1.5. Every wave lasts 3 seconds (shortened on 2026-10-25 after play testing from 4–6 and 6–8 seconds; the numbers live in `gate_tuning`).
- Winning is judged on **cumulative performance**: each wave multiplies the performance ratio by "chosen ÷ best", and 60% or more left after 6 waves wins (the second-best once or twice still wins, three times loses; after picking a worst card any further mistake loses). After wave 6 the wolf pack slides in from the right with its number and is compared with the sheep; as many sheep as wolves or more is a win.
- Multiplication cards (×2, ×3, ×1.5, +40%) appear in at most 2 waves per round.
- Win: 1 star candy (**at most 3 per day**), happiness +15, bond +3. Playing makes it a little hungry: fullness −5.

## 6. Phone sync (WeChat mini program)

**How to use it**: choose "Sync" on the home screen, and the device turns on Bluetooth and waits 60 seconds; open the mini program on the iPhone and tap "Sync". Bluetooth is normally off to save power.

| Direction | What is sent |
| --- | --- |
| Phone → device | The current real time, used to catch up on changes while you were away |
| Device → phone | Name, stage, fullness, happiness, bond, star candy count, last feeding time |
| Device → phone (bonus) | A full save backup. Reflashing the firmware may erase the save; with a backup it can be restored |

- **A little sync ceremony**: when the sync succeeds, Pupu waves, with a "ding" sound.
- **"Welcome back"**: whenever time away is caught up (waking from deep sleep, or syncing after a power-off), if you were away for more than 1 hour, the home screen shows "Welcome back! You were away X hours" (displayed in Chinese on the device).
- First sync of each day: bond +2.
- The mini program shows the pet's state as **large cards** on the phone; anything that does not fit on the small screen goes there.
- v2 did not change the sync protocol: pocket money and emo are not in the synced status, but they are in the save backup (save v2).
- **Development order**: first write a small web page in Chrome on the Mac to verify Bluetooth send and receive (no account registration needed), then build the WeChat mini program once that works. The mini program needs a registered account, which individuals can create; for personal use the "trial version" is enough and it does not need to be published.

## 7. Buttons

| Page | ↑ / ↓ | OK short press | OK long press |
| --- | --- | --- | --- |
| Egg | — | — | Hold 3 seconds to hatch (the screen does not turn off during this) |
| Home | Switch the bottom function (Feed / Play / Status / Sync) | Enter | Screen off |
| Feed, Play | Choose a food or activity (Pat / Work / Sheep Gate) | Confirm | Back |
| Mini-game level select | Choose Normal / Scholar | Release to start | Quit |
| Mini-game | Move up / down a lane | Pause | Quit |
| Status, Sync | Change page | — | Back |

## 8. Home screen layout

```text
┌──────────────────────────┐  240 × 320
│ Battery 80%    Sync icon │  Top bar: small and faint
│                          │
│        (Hungry)          │  Need bubble: at most 1
│       ┌──────────┐       │
│       │   Pupu   │       │  Pet: about 144 × 144
│       │ animated │       │  Color follows mood
│       └──────────┘       │
│                          │
│  [Feed]  (Play) [Status] │  Bottom bar: the middle one is selected
└──────────────────────────┘
```

## 9. Still **not** included

Sickness, death, poop and cleaning, sleeping, a shop, multiple pets, gifts, co-parenting by two people, AI conversation, Wi-Fi networking. These are left for later versions.

## 10. Definition of done (acceptance)

1. **Fully playable without a phone**: feeding, pats, work, and Sheep Gate all work, the state changes over time, and the save survives a power-off and power-on.
2. After 3 hours powered off, power on and sync: the pet's state correctly reflects how long you were away, and the caught-up part did not go below 10.
3. The values shown in the mini program exactly match those on the device.
4. The development build has a "time acceleration" switch (1 minute counts as 1 hour), so testing does not require waiting several real hours.

## 11. Decisions made

1. **Name and look** (2026-10-23): Pupu, based on a black-faced sheep, hatched from an egg.
2. **Gameplay** (2026-10-23): everything in sections 2–10 is confirmed.
3. **On-screen text** (2026-10-23): follow the proposal for now, **mainly icons plus a little Chinese text** (a font is generated only for the few dozen characters actually used). If an English name is needed later, note that "Pupu" can remind English speakers of "poop"; "Puff" is an option.
4. **Pacing** (2026-10-23): designed for checking in 3–4 times a day, to be tuned after play testing on the device.
5. **v2** (2026-10-25): slower decay, pocket money and work, paid feeding, six pat levels, emo, and Sheep Gate, with every number fixed as in sections 1–6 of [v2-handoff.md](v2-handoff.md).

## 12. Detail rules added after research (adopted by default on 2026-10-23, revised for v2 on 2026-10-25, you can veto them)

Research found a few places the original design left unclear. These default rules fill them in with the "easy companion" approach:

1. **What counts as a day**: use the pet's own "day clock": every full 24 hours is one day, with no calendar dates. The day clock has two parts: powered-on time, plus the **real** time away. Real time away only counts when the device knows it: it knows when waking from deep sleep; after a power-button shutdown, it only knows at the next sync. Value decay is still capped at 8 caught-up hours, but days follow the real duration, so the daily limits and "about 5 days to grow up" track real days closely.
2. **"Very hungry"**: fullness below 15. Happiness then drops 2 every 20 minutes; at 15 or above it drops 1 every 20 minutes.
3. **There is only one catch-up rule**: waking from deep sleep and syncing after a power-off both catch up the "time away", at most 8 hours, and the caught-up part only lowers values down to 10. If a value was already below 10 before catching up, catching up does not raise it to 10. The emo countdown also runs during catch-up.
4. **The egg stage never gets hungry**: the state only starts dropping after hatching. An egg cannot be patted or sent to work either.
5. **Fleece color**: based on the lower of fullness and happiness, in 8 steps from cream to gray.
6. **Mini-game**: the level selection and the 3-2-1 countdown are not counted in the round's time. Quitting mid-round with a long press does not count as a round: nothing is deducted or awarded; buttons are ignored during the 2-second wolf settlement. After the 3 daily star candies are claimed, a win still adds happiness and bond. If the screen turns off during level selection or while paused, the round is void. At most 99 star candies can be stored. While emo the game plays and rewards as usual, but a win does not end emo.
7. **Sync window**: wait 60 seconds for the phone to connect. Once connected, end when the phone sends "done" or after 20 seconds with no new request, 2 minutes at most in total.
8. **Save backup**: for now only downloading a backup from device to phone. Restoring a save from the phone is planned for later.
9. **Bluetooth name**: the device appears in Bluetooth as `Pupu-XXXX` (XXXX is the last 4 digits of the device address), visible only in the device list while connecting.
10. **Status page**: shows fullness, happiness, pocket money, and the star candy count; during the baby stage it also shows a "growth progress x/100" line (the bond value).
11. **Starting values of a new egg**: fullness 80, happiness 80, bond 0, star candy 0, pocket money 30. Values do not change during the egg stage; after hatching they start dropping from these values. An old v1 save also gets 30 pocket money when read.
12. **Refusing food**: at fullness ≥ 90, it refuses rice balls, cookies, and star candy, and nothing changes: no money or star candy is used and it does not count toward the snack count. The exception is star candy while emo, eaten even when too full. Without enough money, rice balls and cookies are refused too and nothing changes. While emo, cookies are refused and nothing is charged. Order of checks: egg → no candy → cookie while emo → too full → not enough money → takes effect.
13. **Snack and pat "windows"**: the first cookie opens a 60-minute window (pet time); within the window, cookies from the 4th onward add no happiness but are still charged, and counting restarts after the window ends. The pat window is 10 minutes: the first pat opens it, every press within it counts, saturating at 5; the 4th is annoyed, the 5th costs 5 happiness, from the 6th it only turns away; the count resets when the window ends. Refused cookies are not counted and do not extend the window; pats while emo are not counted and do not open the window.
14. **Daily bond limit**: rice ball while hungry +3, star candy +2, pat +1, mini-game +3, first sync +2; all combined add at most +20 per day, and any excess is not added. Each job uses 3 points of the allowance (bond itself unchanged), up to the 20-point cap.
15. **"First sync of the day"**: the first accepted time sync in each pet day; catch-up happens first, then bond is added. Connections that only view the status without a time sync, and repeated time syncs, do not count.
16. **Leaving the sync page**: a long press of OK on the sync page first turns Bluetooth off, then returns to the home screen. While the sync window is open, other pages cannot be opened.

---

<details>
<summary>Developer notes (for writing code)</summary>

- Rules such as state decay, the catch-up cap, daily limits, work and emo, and Sheep Gate's card generation must be written as hardware-independent pure logic so they can be covered by tests on a computer (repository rule).
- The save is stored in NVS with a version number, so a future save format change does not misread old saves. The v2 save is 71 bytes (pocket money, today's job count and the remaining emo minutes follow the 51-byte v1 payload); old saves migrate automatically when read.
- "Daily" limits follow the pet's own clock (powered-on time plus caught-up time from syncs). A device that has never synced does not know today's date, so calendar dates cannot be used.
- Bluetooth reuses the community-proven Nordic UART channel layout (see `docs/reference/shinku-chen/ble-voice-uplink-and-pairing.md`). There is no pairing; connections are only possible during the 60-second sync window. Pairing will be added later together with gifts.
- ↑ / ↓ respond on press (PRESS event) on every page, so rapid presses are not lost. OK uses single click (a double click also counts as one click) and long press (500 milliseconds). The button driver merges rapid presses into a double click, and a triple click sends no event at all. See section 5 of the development plan (outside the repository) for details.
- The UI must be redesigned and must not reuse the demo menu (repository rule); `demo/tamagezi` is only a reference for logic and sound effects.
- Every number that play testing may change lives in `main/pupu_config.h` and in `gate_tuning` at the top of `main/gate_model.c`.

</details>
