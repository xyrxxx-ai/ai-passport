#include "pupu_save_codec.h"

#include <string.h>

#include "pupu_config.h"

#define PUPU_SAVE_MAGIC 0x55505550u  // 小端写出就是 "PUPU"

uint32_t pupu_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint8_t *put_u8(uint8_t *p, uint8_t v) {
    *p = v;
    return p + 1;
}

static uint8_t *put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return p + 2;
}

static uint8_t *put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
    return p + 4;
}

static uint8_t *put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
    return p + 8;
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

// v1 payload 的字段顺序(51 字节)。v2 的前 51 字节和它完全一样。
// 改动这里就是改了存档格式,必须同时升版本号。
static void decode_v1(const uint8_t *p, pupu_save_t *out) {
    pupu_model_t *m = &out->model;
    memset(out, 0, sizeof(*out));
    m->stage = p[0];
    m->full = p[1];
    m->happy = p[2];
    m->bond = get_u16(p + 3);
    m->candy = p[5];
    m->life_min = get_u32(p + 6);
    m->life_sec = p[10];
    m->day_s = get_u32(p + 11);
    m->bond_today = p[15];
    m->candy_today = p[16];
    m->synced_today = p[17] != 0;
    m->cookie_count = p[18];
    m->cookie_win_min = p[19];
    m->pat_count = p[20];
    m->pat_win_min = p[21];
    m->has_fed = p[22] != 0;
    m->fed_life_min = get_u32(p + 23);
    m->fed_acct = get_u32(p + 27);
    out->acct_epoch = get_u32(p + 31);
    out->rtc_us = get_u64(p + 35);
    out->token = get_u64(p + 43);
}

// v2 在 v1 后面接的 4 字节:coin u8、work_today u8、emo_min u16。
static void decode_v2_tail(const uint8_t *p, pupu_save_t *out) {
    pupu_model_t *m = &out->model;
    m->coin = p[PUPU_SAVE_PAYLOAD_SIZE_V1];
    m->work_today = p[PUPU_SAVE_PAYLOAD_SIZE_V1 + 1];
    m->emo_min = get_u16(p + PUPU_SAVE_PAYLOAD_SIZE_V1 + 2);
}

// 按存档里的版本号读 payload,并迁移到当前版本。
static pupu_save_status_t decode_payload(uint16_t ver, const uint8_t *p, size_t n, pupu_save_t *out) {
    switch (ver) {
    case 1:
        if (n != PUPU_SAVE_PAYLOAD_SIZE_V1) return PUPU_SAVE_BAD_LENGTH;
        decode_v1(p, out);
        // v1 → v2 迁移:v1 没有零花钱、打工和 emo,按新孵化的噗噗补上
        out->model.coin = PUPU_COIN_START;
        out->model.work_today = 0;
        out->model.emo_min = 0;
        return PUPU_SAVE_OK;
    case 2:
        if (n != PUPU_SAVE_PAYLOAD_SIZE_V2) return PUPU_SAVE_BAD_LENGTH;
        decode_v1(p, out);
        decode_v2_tail(p, out);
        return PUPU_SAVE_OK;
    default:
        return PUPU_SAVE_BAD_VERSION;
    }
}

size_t pupu_save_encode(const pupu_save_t *s, uint32_t seq, uint8_t *buf, size_t cap) {
    if (cap < PUPU_SAVE_RECORD_SIZE) return 0;
    const pupu_model_t *m = &s->model;
    uint8_t *p = buf;
    p = put_u32(p, PUPU_SAVE_MAGIC);
    p = put_u16(p, PUPU_SAVE_VERSION);
    p = put_u32(p, seq);
    p = put_u16(p, PUPU_SAVE_PAYLOAD_SIZE);

    // v1 部分
    p = put_u8(p, m->stage);
    p = put_u8(p, m->full);
    p = put_u8(p, m->happy);
    p = put_u16(p, m->bond);
    p = put_u8(p, m->candy);
    p = put_u32(p, m->life_min);
    p = put_u8(p, m->life_sec);
    p = put_u32(p, m->day_s);
    p = put_u8(p, m->bond_today);
    p = put_u8(p, m->candy_today);
    p = put_u8(p, m->synced_today ? 1 : 0);
    p = put_u8(p, m->cookie_count);
    p = put_u8(p, m->cookie_win_min);
    p = put_u8(p, m->pat_count);
    p = put_u8(p, m->pat_win_min);
    p = put_u8(p, m->has_fed ? 1 : 0);
    p = put_u32(p, m->fed_life_min);
    p = put_u32(p, m->fed_acct);
    p = put_u32(p, s->acct_epoch);
    p = put_u64(p, s->rtc_us);
    p = put_u64(p, s->token);
    // v2 新增
    p = put_u8(p, m->coin);
    p = put_u8(p, m->work_today);
    p = put_u16(p, m->emo_min);

    put_u32(p, pupu_crc32(buf, (size_t)(p - buf)));
    return PUPU_SAVE_RECORD_SIZE;
}

pupu_save_status_t pupu_save_decode(const uint8_t *buf, size_t len, pupu_save_t *out, uint32_t *seq) {
    if (len < PUPU_SAVE_HEADER_SIZE + 4) return PUPU_SAVE_BAD_LENGTH;
    if (get_u32(buf) != PUPU_SAVE_MAGIC) return PUPU_SAVE_BAD_MAGIC;
    size_t payload_len = get_u16(buf + 10);
    if (len != PUPU_SAVE_HEADER_SIZE + payload_len + 4) return PUPU_SAVE_BAD_LENGTH;
    if (get_u32(buf + len - 4) != pupu_crc32(buf, len - 4)) return PUPU_SAVE_BAD_CRC;

    // 先验 CRC 再看版本号:损坏的记录不能被误认成"未来版本",否则它会永远挡住覆盖。
    uint16_t ver = get_u16(buf + 4);
    if (ver > PUPU_SAVE_VERSION) return PUPU_SAVE_FUTURE;
    pupu_save_status_t status = decode_payload(ver, buf + PUPU_SAVE_HEADER_SIZE, payload_len, out);
    if (status == PUPU_SAVE_OK) *seq = get_u32(buf + 6);
    return status;
}
