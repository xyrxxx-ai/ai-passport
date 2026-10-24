<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu sync page (Mac Chrome prototype)

A single web page that syncs with Pupu over Bluetooth LE from Chrome on a Mac. It sends the
current time (Pupu catches up on the time you were away), shows the status card, downloads a
backup of the save, and ends the sync. It proves the protocol before the WeChat mini program.

| File | What it is |
| --- | --- |
| `index.html` | The page. No build step and no dependencies. |
| `pupu_protocol.js` | Message encoding and decoding, CRC-32, retries, and one whole sync. No browser APIs, so Node can test it. |
| `package.json` | Only marks the `.js` files here as ES modules for Node. Nothing to install. |

The protocol itself is described in `main/pupu_sync_proto.h`. The byte-exact golden vectors
in `tests/pupu_sync_vectors.txt` are checked by both `tests/test_pupu_sync_proto.c` (firmware)
and `tests/test_pupu_sync_web.mjs` (this page).

## Run it

1. Allow Chrome to use Bluetooth once: System Settings → Privacy & Security → Bluetooth.
2. Start a local web server from the repository root and leave it running (Ctrl+C stops it):

   ```bash
   python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/pupu-sync-web
   ```

3. Open `http://localhost:8000/` in Chrome. Keep `--bind 127.0.0.1`: Web Bluetooth only works
   on a secure origin, and `localhost` counts as one.
4. On the device, choose Sync on the home page. The screen shows `Pupu-XXXX` and a countdown.
5. On the page, press the sync button and pick the same `Pupu-XXXX` in Chrome's list.

The device waits 60 seconds for a connection. Once connected, the window stays open until the
page says it is done, or until 20 seconds pass without a request, and never longer than
2 minutes. Long-press OK on the device to close the window early.

## What a sync does

1. Opens notifications on the TX characteristic first, then reads it once
   (`PUPU/<protocol>/<firmware version>`).
2. TIME: the device catches up (at most 8 hours, never below 10) and replies with STATUS.
3. SAVE_BEGIN, then one SAVE_GET per 17-byte chunk. The page checks the length, the
   CRC-32 from SAVE_INFO, and the record's own trailing CRC.
4. DONE: the device replies DONE_ACK, waves, chimes, and turns Bluetooth off.

Every request waits up to 1 second for its reply and is resent with the same sequence number
at most 3 times. The device treats a repeated request as the same request.

## Troubleshooting

- The list in Chrome is empty: the window on the device has closed. Choose Sync on the device
  again. The page only lists names starting with `Pupu-`.
- The page says the browser cannot use Bluetooth: use Chrome on the Mac and open the page from
  `http://localhost:8000/`, not from a file.
- Open `chrome://bluetooth-internals` to watch the connection. The message log at the bottom of
  the page shows every byte sent and received.
- Web Bluetooth is not available in any browser on iPhone. The phone version is the WeChat
  mini program (milestone M7).
