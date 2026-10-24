// Pupu sync protocol for the Mac Chrome page (and a reference for the WeChat mini program).
//
// Mirrors main/pupu_sync_proto.h. One message is one ATT write or notification of at most
// 20 bytes: [type u8][seq u8][payload], little-endian. The device echoes the request's seq.
// The phone owns retries: no reply within 1 s -> resend the same bytes (same seq), at most
// 3 times. Golden byte vectors shared with the C tests: tests/pupu_sync_vectors.txt.
//
// No browser APIs in here, so tests/test_pupu_sync_web.mjs can run it under Node.

export const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // phone writes
export const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // device notifies
export const NAME_PREFIX = 'Pupu-';
export const PROTO_VER = 1;
export const MSG_MAX = 20;
export const CHUNK_MAX = 17;
export const NEVER_FED = 0xffff;

export const MSG = Object.freeze({
  TIME: 0x01,
  GET_STATUS: 0x02,
  SAVE_BEGIN: 0x03,
  SAVE_GET: 0x04,
  DONE: 0x05,
  STATUS: 0x81,
  SAVE_INFO: 0x83,
  SAVE_CHUNK: 0x84,
  DONE_ACK: 0x85,
  ERR: 0xee,
});

export const ERR_TEXT = Object.freeze({
  1: '长度不对',
  2: '设备不认识这条消息',
  3: '手机时间不对（要在 2026 年到 2100 年之间）',
  4: '存档块序号越界',
  5: '还没有存档快照',
});

export const STAGE_TEXT = Object.freeze({ 0: '蛋', 1: '幼年', 2: '成年' });

// ---- encoding (phone -> device) ----

export function encodeTime(seq, epochS, tzMin) {
  const b = new Uint8Array(8);
  const v = new DataView(b.buffer);
  b[0] = MSG.TIME;
  b[1] = seq & 0xff;
  v.setUint32(2, epochS >>> 0, true);
  v.setInt16(6, tzMin, true);
  return b;
}

export const encodeGetStatus = (seq) => Uint8Array.of(MSG.GET_STATUS, seq & 0xff);
export const encodeSaveBegin = (seq) => Uint8Array.of(MSG.SAVE_BEGIN, seq & 0xff);
export const encodeSaveGet = (seq, idx) => Uint8Array.of(MSG.SAVE_GET, seq & 0xff, idx & 0xff);
export const encodeDone = (seq) => Uint8Array.of(MSG.DONE, seq & 0xff);

// ---- decoding (device -> phone) ----

export class ProtocolError extends Error {}

function need(bytes, len, what) {
  if (bytes.length !== len) {
    throw new ProtocolError(`${what}: expected ${len} bytes, got ${bytes.length}`);
  }
}

// Returns {kind, seq, ...fields}. Field names match tests/pupu_sync_vectors.txt.
export function decode(bytes) {
  if (bytes.length < 2) throw new ProtocolError(`message too short: ${bytes.length} bytes`);
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const seq = bytes[1];
  switch (bytes[0]) {
    case MSG.STATUS:
      need(bytes, 14, 'STATUS');
      return {
        kind: 'status',
        seq,
        proto_ver: bytes[2],
        stage: bytes[3],
        full: bytes[4],
        happy: bytes[5],
        bond: v.getUint16(6, true),
        candy: bytes[8],
        fed_age_min: v.getUint16(9, true),
        away_min: v.getUint16(11, true),
        flags: bytes[13],
      };
    case MSG.SAVE_INFO:
      need(bytes, 10, 'SAVE_INFO');
      return {
        kind: 'save_info',
        seq,
        save_ver: bytes[2],
        total_len: v.getUint16(3, true),
        chunk_count: bytes[5],
        crc32: v.getUint32(6, true),
      };
    case MSG.SAVE_CHUNK:
      if (bytes.length < 4 || bytes.length > MSG_MAX) {
        throw new ProtocolError(`SAVE_CHUNK: bad length ${bytes.length}`);
      }
      return { kind: 'save_chunk', seq, idx: bytes[2], data: bytes.slice(3) };
    case MSG.DONE_ACK:
      need(bytes, 2, 'DONE_ACK');
      return { kind: 'done_ack', seq };
    case MSG.ERR:
      need(bytes, 4, 'ERR');
      return { kind: 'err', seq, req_type: bytes[2], code: bytes[3] };
    default:
      throw new ProtocolError(`unknown message type 0x${bytes[0].toString(16)}`);
  }
}

// ---- save backup ----

// CRC-32/IEEE (reflected 0xEDB88320, init and xorout 0xFFFFFFFF), same as zlib.crc32.
export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const b of bytes) {
    crc ^= b;
    for (let k = 0; k < 8; k++) crc = crc & 1 ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1;
  }
  return (crc ^ 0xffffffff) >>> 0;
}

// The backup is one save record (main/pupu_save_codec.h): "PUPU" magic first and its own
// CRC32 in the last 4 bytes. SAVE_INFO.crc32 covers every byte except those last 4 (the CRC
// of a whole record including its own CRC is always 0x2144df1c, so it would check nothing).
export function checkSave(blob, info) {
  const lengthOk = blob.length === info.total_len && blob.length > 8;
  const body = lengthOk ? blob.subarray(0, blob.length - 4) : blob;
  const crcOk = lengthOk && crc32(body) === info.crc32;
  const v = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  const trailerOk = lengthOk && v.getUint32(blob.length - 4, true) === info.crc32;
  const magicOk = blob.length >= 4 && v.getUint32(0, true) === 0x55505550; // "PUPU"
  return { ok: lengthOk && crcOk && trailerOk && magicOk, lengthOk, crcOk, trailerOk, magicOk };
}

// ---- reliable request/response over one link ----

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// write(bytes) sends one message and resolves when the write is acknowledged.
// Call onNotify(bytes) for every TX notification. One request is in flight at a time.
export class PupuLink {
  constructor(write, { timeoutMs = 1000, retries = 3, log = () => {} } = {}) {
    this.write = write;
    this.timeoutMs = timeoutMs;
    this.retries = retries;
    this.log = log;
    this.seq = Math.floor(Math.random() * 256);
    this.waiting = null; // {seq, resolve}
  }

  nextSeq() {
    this.seq = (this.seq + 1) & 0xff;
    return this.seq;
  }

  onNotify(bytes) {
    let msg;
    try {
      msg = decode(bytes);
    } catch (e) {
      this.log(`ignored a bad notification: ${e.message}`);
      return;
    }
    if (this.waiting && msg.seq === this.waiting.seq) {
      const { resolve } = this.waiting;
      this.waiting = null;
      resolve(msg);
    } else {
      this.log(`ignored a late reply (seq ${msg.seq})`);
    }
  }

  // Sends make(seq) and waits for the reply with the same seq. Resends the same bytes after
  // timeoutMs, at most `retries` times. ERR replies come back as {kind: 'err'}.
  async request(make) {
    const seq = this.nextSeq();
    const bytes = make(seq);
    for (let attempt = 0; attempt <= this.retries; attempt++) {
      const reply = new Promise((resolve) => {
        this.waiting = { seq, resolve };
      });
      if (attempt > 0) this.log(`no reply, resending seq ${seq} (${attempt}/${this.retries})`);
      try {
        // iPhone WeChat may lose a write with no callback at all, so the wait is bounded too.
        await Promise.race([this.write(bytes), sleep(this.timeoutMs)]);
      } catch (e) {
        this.log(`write failed: ${e.message}`); // Chrome may throw instead: retry the same way
      }
      const got = await Promise.race([reply, sleep(this.timeoutMs).then(() => null)]);
      if (got) return got;
    }
    this.waiting = null;
    throw new ProtocolError(`no reply to type 0x${bytes[0].toString(16)} seq ${seq}`);
  }
}

function expect(reply, kind) {
  if (reply.kind === 'err') {
    const text = ERR_TEXT[reply.code] || `error ${reply.code}`;
    throw new ProtocolError(`device said: ${text}`);
  }
  if (reply.kind !== kind) throw new ProtocolError(`expected ${kind}, got ${reply.kind}`);
  return reply;
}

// One whole sync: TIME (the device catches up and replies STATUS), download the save,
// then DONE. epochS and tzMin come from the phone's clock.
export async function runSync(link, { epochS, tzMin, onStep = () => {} }) {
  onStep('time');
  const status = expect(await link.request((s) => encodeTime(s, epochS, tzMin)), 'status');
  if (status.proto_ver !== PROTO_VER) {
    throw new ProtocolError(`device speaks protocol ${status.proto_ver}, this page speaks ${PROTO_VER}`);
  }
  onStep('save');
  const info = expect(await link.request((s) => encodeSaveBegin(s)), 'save_info');
  const blob = new Uint8Array(info.total_len);
  let filled = 0;
  for (let idx = 0; idx < info.chunk_count; idx++) {
    const chunk = expect(await link.request((s) => encodeSaveGet(s, idx)), 'save_chunk');
    if (chunk.idx !== idx || idx * CHUNK_MAX + chunk.data.length > blob.length) {
      throw new ProtocolError(`bad chunk ${chunk.idx}`);
    }
    blob.set(chunk.data, idx * CHUNK_MAX);
    filled += chunk.data.length;
  }
  if (filled !== blob.length) throw new ProtocolError(`save has ${filled} of ${blob.length} bytes`);
  onStep('done');
  // The status and the checked backup are already here. The device turns Bluetooth off
  // 1.5 s after the first DONE it sees, so a lost DONE_ACK must not throw them away.
  let doneAcked = true;
  try {
    expect(await link.request((s) => encodeDone(s)), 'done_ack');
  } catch (e) {
    doneAcked = false;
  }
  return { status, info, blob, check: checkSave(blob, info), doneAcked };
}

// ---- display helpers ----

// 90 -> "1 小时 30 分钟"; 0xFFFE is the device's cap (about 45 days).
export function formatMinutes(min) {
  if (min >= 0xfffe) return '45 天以上';
  const d = Math.floor(min / 1440);
  const h = Math.floor((min % 1440) / 60);
  const m = min % 60;
  const parts = [];
  if (d) parts.push(`${d} 天`);
  if (h) parts.push(`${h} 小时`);
  if (m || parts.length === 0) parts.push(`${m} 分钟`);
  return parts.join(' ');
}
