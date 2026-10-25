// Host test for tools/pupu-sync-web/pupu_protocol.js (run: node tests/test_pupu_sync_web.mjs).
//
// Checks the page's encoder and decoder against the golden vectors the firmware test also
// replays (tests/pupu_sync_vectors.txt), then the retry rules and one whole sync against a
// fake device that answers with those same vector bytes. Node standard library only.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import {
  PupuLink, ProtocolError, checkSave, crc32, decode, encodeDone, encodeGetStatus, encodeSaveBegin,
  encodeSaveGet, encodeTime, formatMinutes, runSync,
} from '../tools/pupu-sync-web/pupu_protocol.js';

const vectorPath = process.argv[2] || fileURLToPath(new URL('pupu_sync_vectors.txt', import.meta.url));

const fromHex = (text) => Uint8Array.from(text.trim().split(/\s+/).filter(Boolean), (h) => parseInt(h, 16));

function loadVectors(path) {
  const out = [];
  for (const raw of readFileSync(path, 'utf8').split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const [head, tail = ''] = line.split('|');
    const [dir, name, ...hexParts] = head.trim().split(/\s+/);
    const fields = {};
    for (const pair of tail.trim().split(/\s+/).filter(Boolean)) {
      const [k, v] = pair.split('=');
      fields[k] = v;
    }
    out.push({ dir, name, bytes: fromHex(hexParts.join(' ')), fields });
  }
  return out;
}

const vectors = loadVectors(vectorPath);
assert.equal(vectors.filter((v) => v.dir === 'req').length, 18);
assert.equal(vectors.filter((v) => v.dir === 'resp').length, 18);
const SAVE_CHUNKS = [0, 1, 2, 3, 4]; // a 71-byte v2 save record in 17 + 17 + 17 + 17 + 3
const byName = Object.fromEntries(vectors.map((v) => [v.name, v]));
const num = (v) => (String(v).startsWith('0x') ? parseInt(v, 16) : Number(v));

function testEncoders() {
  let checked = 0;
  for (const v of vectors.filter((x) => x.dir === 'req')) {
    const f = v.fields;
    const seq = num(f.seq);
    let got;
    switch (f.kind) {
      case 'time': got = encodeTime(seq, num(f.epoch), num(f.tz_min)); break;
      case 'get_status': got = encodeGetStatus(seq); break;
      case 'save_begin': got = encodeSaveBegin(seq); break;
      case 'save_get': got = encodeSaveGet(seq, num(f.idx)); break;
      case 'done': got = encodeDone(seq); break;
      case 'raw': continue; // malformed on purpose: the page never sends these
      default: throw new Error(`unknown kind in ${v.name}`);
    }
    assert.deepEqual(got, v.bytes, v.name);
    checked++;
  }
  assert.equal(checked, 15);
  // negative time zones and the whole u32 range
  assert.deepEqual(encodeTime(0, 0xffffffff, -300), Uint8Array.of(1, 0, 0xff, 0xff, 0xff, 0xff, 0xd4, 0xfe));
}

function testDecoders() {
  for (const v of vectors.filter((x) => x.dir === 'resp')) {
    const msg = decode(v.bytes);
    const f = v.fields;
    assert.equal(msg.kind, f.kind, v.name);
    for (const [k, want] of Object.entries(f)) {
      if (k === 'kind') continue;
      if (k === 'data') {
        assert.deepEqual(msg.data, fromHex(want.replace(/(..)/g, '$1 ')), `${v.name}.data`);
      } else {
        assert.equal(msg[k], num(want), `${v.name}.${k}`);
      }
    }
  }
  // wrong lengths and unknown types are rejected, not misread
  assert.throws(() => decode(Uint8Array.of(0x81, 1, 1)), ProtocolError);
  assert.throws(() => decode(Uint8Array.of(0x83, 1)), ProtocolError);
  assert.throws(() => decode(Uint8Array.of(0x84, 1, 0)), ProtocolError); // chunk with no data
  assert.throws(() => decode(new Uint8Array(21).fill(0x84)), ProtocolError);
  assert.throws(() => decode(Uint8Array.of(0x85, 1, 0)), ProtocolError);
  assert.throws(() => decode(Uint8Array.of(0x01, 1)), ProtocolError); // a request, not a reply
  assert.throws(() => decode(Uint8Array.of(0x81)), ProtocolError);
}

function savedBlob() {
  const chunks = SAVE_CHUNKS.map((i) => decode(byName[`save_chunk_${i}`].bytes).data);
  const blob = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
  let at = 0;
  for (const c of chunks) {
    blob.set(c, at);
    at += c.length;
  }
  return blob;
}

function testSaveCheck() {
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
  const info = decode(byName.save_info.bytes);
  const blob = savedBlob();
  assert.equal(blob.length, 71);
  assert.equal(crc32(blob), 0x2144df1c); // whole record incl. its CRC: always this constant
  assert.deepEqual(checkSave(blob, info), { ok: true, lengthOk: true, crcOk: true, trailerOk: true, magicOk: true });
  // any single flipped byte is caught
  for (let i = 0; i < blob.length; i++) {
    const bad = blob.slice();
    bad[i] ^= 0x01;
    assert.equal(checkSave(bad, info).ok, false, `flip byte ${i}`);
  }
  assert.equal(checkSave(blob.subarray(0, 70), info).ok, false);
}

function testFormat() {
  assert.equal(formatMinutes(0), '0 分钟');
  assert.equal(formatMinutes(90), '1 小时 30 分钟');
  assert.equal(formatMinutes(180), '3 小时');
  assert.equal(formatMinutes(1500), '1 天 1 小时');
  assert.equal(formatMinutes(0xfffe), '45 天以上');
}

// A fake device that answers from the vectors, patching in the request's seq.
function fakeDevice({ dropFirst = 0 } = {}) {
  const sent = [];
  let link;
  let toDrop = dropFirst;
  const reply = (vectorName, seq) => {
    const b = byName[vectorName].bytes.slice();
    b[1] = seq;
    return b;
  };
  const write = async (bytes) => {
    sent.push(bytes.slice());
    if (toDrop > 0) {
      toDrop--;
      return; // lost on the way: no reply
    }
    const seq = bytes[1];
    let out;
    switch (bytes[0]) {
      case 0x01: out = reply('status_synced', seq); break;
      case 0x02: out = reply('status_start', seq); break;
      case 0x03: out = reply('save_info', seq); break;
      case 0x04: out = reply(`save_chunk_${bytes[2]}`, seq); break;
      case 0x05: out = reply('done_ack', seq); break;
      default: out = Uint8Array.of(0xee, seq, bytes[0], 2);
    }
    setTimeout(() => link.onNotify(out), 1);
  };
  return { sent, write, attach: (l) => { link = l; } };
}

async function testRetries() {
  // the first two writes are lost: the same bytes (same seq) go out again
  const dev = fakeDevice({ dropFirst: 2 });
  const link = new PupuLink(dev.write, { timeoutMs: 20 });
  dev.attach(link);
  const status = await link.request((s) => encodeGetStatus(s));
  assert.equal(status.kind, 'status');
  assert.equal(dev.sent.length, 3);
  assert.deepEqual(dev.sent[0], dev.sent[2]);
  assert.equal(status.seq, dev.sent[0][1]);

  // 1 try + 3 retries, then give up
  const deaf = fakeDevice({ dropFirst: 99 });
  const link2 = new PupuLink(deaf.write, { timeoutMs: 10 });
  deaf.attach(link2);
  await assert.rejects(link2.request((s) => encodeGetStatus(s)), ProtocolError);
  assert.equal(deaf.sent.length, 4);

  // a late reply with an old seq is ignored, garbage is ignored
  const dev3 = fakeDevice();
  const link3 = new PupuLink(dev3.write, { timeoutMs: 50 });
  dev3.attach(link3);
  link3.onNotify(Uint8Array.of(0x85, (link3.seq + 200) & 0xff));
  link3.onNotify(Uint8Array.of(0x42));
  const ack = await link3.request((s) => encodeDone(s));
  assert.equal(ack.kind, 'done_ack');

  // a stale reply arriving while another request waits must not answer it
  const stale = new PupuLink(async (bytes) => {
    setTimeout(() => stale.onNotify(Uint8Array.of(0x85, (bytes[1] - 1) & 0xff)), 1);
    setTimeout(() => {
      const st = byName.status_start.bytes.slice();
      st[1] = bytes[1];
      stale.onNotify(st);
    }, 5);
  }, { timeoutMs: 50 });
  assert.equal((await stale.request((s) => encodeGetStatus(s))).kind, 'status');

  // seq wraps from 255 to 0
  link3.seq = 255;
  assert.equal(link3.nextSeq(), 0);

  // a write that throws counts as lost
  let calls = 0;
  const flaky = new PupuLink(async (bytes) => {
    calls++;
    if (calls === 1) throw new Error('GATT operation already in progress');
    setTimeout(() => flaky.onNotify(Uint8Array.of(0x85, bytes[1])), 1);
  }, { timeoutMs: 20 });
  assert.equal((await flaky.request((s) => encodeDone(s))).kind, 'done_ack');
  assert.equal(calls, 2);
}

async function testWholeSync() {
  const dev = fakeDevice({ dropFirst: 1 });
  const link = new PupuLink(dev.write, { timeoutMs: 20 });
  dev.attach(link);
  const steps = [];
  const r = await runSync(link, { epochS: 1798772400, tzMin: 480, onStep: (s) => steps.push(s) });
  assert.deepEqual(steps, ['time', 'save', 'done']);
  assert.equal(r.status.full, 72);
  assert.equal(r.status.away_min, 180);
  assert.equal(r.check.ok, true);
  assert.deepEqual(r.blob, savedBlob());
  // TIME (lost once and resent), SAVE_BEGIN, 5 × SAVE_GET, DONE
  assert.deepEqual(dev.sent.map((b) => b[0]), [1, 1, 3, 4, 4, 4, 4, 4, 5]);
  assert.deepEqual(dev.sent[1].subarray(2), encodeTime(0, 1798772400, 480).subarray(2));

  assert.equal(r.doneAcked, true);

  // DONE_ACK lost every time (the device already turned Bluetooth off): keep the results
  const deafToDone = fakeDevice();
  const link2 = new PupuLink(async (bytes) => (bytes[0] === 0x05 ? undefined : deafToDone.write(bytes)), { timeoutMs: 10 });
  deafToDone.attach(link2);
  const r2 = await runSync(link2, { epochS: 1798772400, tzMin: 480 });
  assert.equal(r2.doneAcked, false);
  assert.equal(r2.check.ok, true);
  assert.equal(r2.status.full, 72);

  // an ERR reply stops the sync with a readable message
  const errLink = new PupuLink(async (bytes) => {
    setTimeout(() => errLink.onNotify(Uint8Array.of(0xee, bytes[1], 1, 3)), 1);
  }, { timeoutMs: 20 });
  await assert.rejects(runSync(errLink, { epochS: 1, tzMin: 0 }), /2026.*2100/);
}

testEncoders();
testDecoders();
testSaveCheck();
testFormat();
await testRetries();
await testWholeSync();
console.log('test_pupu_sync_web: PASS');
