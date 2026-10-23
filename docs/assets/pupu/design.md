<p align="right">
  <a href="design.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu · Version 1 Design (v1)

Status: final · 2026-10-23 · Target device: FoloToy AI Passport (240 × 320 portrait screen, ↑ ↓ OK buttons, iPhone user)

> **In one sentence**: a little lamb spirit that lives in the passport and hatched from an ink egg. You feed it and play with it, and it slowly grows up; when it connects to the phone, it learns how long you were away and syncs its status to the phone.

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

## 2. State: only 3 numbers

| State | Range | How it changes | How it shows on screen |
| --- | --- | --- | --- |
| Fullness | 0–100 | −10 per hour | Below 40 a "hungry" bubble appears; below 15 it lies down and stops moving |
| Happiness | 0–100 | −8 per hour; −16 when very hungry | Shown through body color and facial expression |
| Bond | Only goes up | Increases through care; all sources combined add **at most +20 per day** | Decides when it grows up. Not shown on the home screen; during the baby stage the status page shows it as "growth progress x/100", and the phone shows it too |

- The home screen **shows no numbers**, only expression and color, plus at most one need bubble. Exact numbers are on the "Status" page.
- **How it grows**: the egg must be warmed by holding OK for 3 seconds before it hatches (no repeated pressing, because the button driver merges rapid presses into a single double-click). A baby that collects 100 bond points grows into an adult; with normal care this takes about 5 days at the fastest (time when the device was switched off with the power button and never synced does not count toward days).
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

## 4. Feeding

| Food | Effect | How to get it |
| --- | --- | --- |
| Rice ball (staple) | Fullness +35 | Free, always available |
| Cookie (snack) | Fullness +10, happiness +5; from the 4th cookie within 1 hour it gets bored of them and happiness no longer increases | Free |
| Star candy (rare) | Happiness +25, bond +2 | Only won through the mini-game |

- Feeding a rice ball while it is hungry (fullness below 40) gives bond +3.
- When it is already very full (fullness 90 or more) it **shakes its head and refuses**. Nothing is deducted; it is just a fun reaction.
- Eating plays a chewing animation of about 2 seconds with a short sound effect.

## 5. Play

**1. Head pat (over in a few seconds)**
Happiness +5, bond +1. From the 4th pat within 10 minutes, it **dodges**: "Stop patting me!"

**2. Mini-game "Catch the Stars" (25 seconds per round)**
- Pupu is on the left side of the screen with 3 lanes stacked vertically; stars fly in from the right, and ↑ / ↓ switch lanes to catch them.
- Small rain clouds occasionally fly in; touching one loses a star.
- Catching 8 or more: earn 1 star candy (**at most 3 per day**), happiness +15, bond +3.
- Playing makes it a little hungry: fullness −5.

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
- **Development order**: first write a small web page in Chrome on the Mac to verify Bluetooth send and receive (no account registration needed), then build the WeChat mini program once that works. The mini program needs a registered account, which individuals can create; for personal use the "trial version" is enough and it does not need to be published.

## 7. Buttons

| Page | ↑ / ↓ | OK short press | OK long press |
| --- | --- | --- | --- |
| Egg | — | — | Hold 3 seconds to hatch (the screen does not turn off during this) |
| Home | Switch the bottom function (Feed / Play / Status / Sync) | Enter | Screen off |
| Feed, Play | Choose a food or activity | Confirm | Back |
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

## 9. Not in version 1

Sickness, death, poop and cleaning, sleeping, coins and a shop, multiple pets, gifts, co-parenting by two people, AI conversation, Wi-Fi networking. These are left for later versions.

## 10. Definition of done (acceptance)

1. **Fully playable without a phone**: feeding, head pats, and the mini-game all work, the state changes over time, and the save survives a power-off and power-on.
2. After 3 hours powered off, power on and sync: the pet's state correctly reflects how long you were away, and the caught-up part did not go below 10.
3. The values shown in the mini program exactly match those on the device.
4. The development build has a "time acceleration" switch (1 minute counts as 1 hour), so testing does not require waiting several real hours.

## 11. Decisions made (2026-10-23)

1. **Name and look**: Pupu, based on a black-faced sheep, hatched from an egg.
2. **Gameplay**: everything in sections 2–10 is confirmed.
3. **On-screen text**: follow the proposal for now, **mainly icons plus a little Chinese text** (a font is generated only for the few dozen characters actually used). If an English name is needed later, note that "Pupu" can remind English speakers of "poop"; "Puff" is an option.
4. **Pacing**: designed for checking in 3–4 times a day, to be tuned after play testing on the device.

## 12. Detail rules added after research (2026-10-23, adopted by default, you can veto them)

Research found a few places the original design left unclear. These default rules fill them in with the "easy companion" approach:

1. **What counts as a day**: use the pet's own "day clock": every full 24 hours is one day, with no calendar dates. The day clock has two parts: powered-on time, plus the **real** time away. Real time away only counts when the device knows it: it knows when waking from deep sleep; after a power-button shutdown, it only knows at the next sync. Value decay is still capped at 8 caught-up hours, but days follow the real duration, so the daily limits and "about 5 days to grow up" track real days closely.
2. **"Very hungry"**: fullness below 15. Happiness then drops 16 per hour; at 15 or above it drops 8 per hour.
3. **There is only one catch-up rule**: waking from deep sleep and syncing after a power-off both catch up the "time away", at most 8 hours, and the caught-up part only lowers values down to 10. If a value was already below 10 before catching up, catching up does not raise it to 10.
4. **The egg stage never gets hungry**: the state only starts dropping after hatching.
5. **Fleece color**: based on the lower of fullness and happiness, in 8 steps from cream to gray.
6. **Mini-game**: a 3-2-1 countdown before each round, not counted in the 25 seconds. Quitting mid-round with a long press does not count as a round: nothing is deducted or awarded. After the 3 daily star candies are claimed, a win still adds happiness and bond. If the screen turns off while paused, the round is void. At most 99 star candies can be stored.
7. **Sync window**: wait 60 seconds for the phone to connect. Once connected, end when the phone sends "done" or after 20 seconds with no new request, 2 minutes at most in total.
8. **Save backup**: version 1 only supports downloading a backup from device to phone. Restoring a save from the phone is planned for v1.1.
9. **Bluetooth name**: the device appears in Bluetooth as `Pupu-XXXX` (XXXX is the last 4 digits of the device address), visible only in the device list while connecting.
10. **Status page**: shows fullness, happiness, and the star candy count; during the baby stage it also shows a "growth progress x/100" line (the bond value).
11. **Starting values of a new egg**: fullness 80, happiness 80, bond 0, star candy 0. Values do not change during the egg stage; after hatching they start dropping from these values.
12. **Refusing food**: at fullness ≥ 90, it refuses rice balls, cookies, and star candy. Nothing changes on a refusal: no star candy is used and it does not count toward the snack count.
13. **Snack and pat "windows"**: the first cookie opens a 60-minute window (pet time); within the window, cookies from the 4th onward add no happiness, and counting restarts after the window ends. Head pats work the same way with a 10-minute window: from the 4th pat it dodges and nothing is added. Refused cookies and dodged pats are not counted and do not extend the window.
14. **Daily bond limit**: rice ball while hungry +3, star candy +2, head pat +1, mini-game +3, first sync +2; all combined add at most +20 per day, and any excess is not added.
15. **"First sync of the day"**: the first accepted time sync in each pet day; catch-up happens first, then bond is added. Connections that only view the status without a time sync, and repeated time syncs, do not count.
16. **Leaving the sync page**: a long press of OK on the sync page first turns Bluetooth off, then returns to the home screen. While the sync window is open, other pages cannot be opened.

---

<details>
<summary>Developer notes (for writing code)</summary>

- Rules such as state decay, the catch-up cap, and daily limits must be written as hardware-independent pure logic so they can be covered by tests on a computer (repository rule).
- The save is stored in NVS with a version number, so a future save format change does not misread old saves.
- "Daily" limits follow the pet's own clock (powered-on time plus caught-up time from syncs). A device that has never synced does not know today's date, so calendar dates cannot be used.
- Bluetooth reuses the community-proven Nordic UART channel layout (see `docs/reference/shinku-chen/ble-voice-uplink-and-pairing.md`). v1 has no pairing; connections are only possible during the 60-second sync window. Pairing will be added later together with gifts.
- ↑ / ↓ respond on press (PRESS event) on every page, so rapid presses are not lost. OK uses single click (a double click also counts as one click) and long press (500 milliseconds). The button driver merges rapid presses into a double click, and a triple click sends no event at all. See section 5 of the development plan (outside the repository) for details.
- The UI must be redesigned and must not reuse the demo menu (repository rule); `demo/tamagezi` is only a reference for logic and sound effects.

</details>
