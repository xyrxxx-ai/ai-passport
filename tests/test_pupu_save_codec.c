#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pupu_config.h"
#include "pupu_save_codec.h"

#define V1_RECORD_SIZE (PUPU_SAVE_HEADER_SIZE + PUPU_SAVE_PAYLOAD_SIZE_V1 + 4)  // 67

// v1 存档的逐字节标准答案,用 Python struct.pack + zlib.crc32 独立生成。
// 这一份不能删:它证明 v1 固件写的旧存档还能被新固件读出来(并补成 30 元零花钱)。
static const uint8_t k_golden_v1[V1_RECORD_SIZE] = {
    0x50, 0x55, 0x50, 0x55, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x33, 0x00,
    0x01, 0x2a, 0x37, 0x02, 0x01, 0x07, 0x04, 0x03, 0x02, 0x01, 0x3b, 0x45,
    0x23, 0x01, 0x00, 0x0d, 0x02, 0x01, 0x02, 0x2d, 0x03, 0x07, 0x01, 0x00,
    0x00, 0x02, 0x01, 0x00, 0xd2, 0x49, 0x6b, 0x10, 0xe0, 0x49, 0x6b, 0x07,
    0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x72, 0x9e, 0xe1, 0xb1,
};
#define GOLDEN_V1_SEQ 0x105u

// v2 存档的逐字节标准答案:同样的宠物状态,后面接 coin 77、work_today 4、emo_min 0x0123。
static const uint8_t k_golden_v2[PUPU_SAVE_RECORD_SIZE] = {
    0x50, 0x55, 0x50, 0x55, 0x02, 0x00, 0x06, 0x02, 0x00, 0x00, 0x37, 0x00,
    0x01, 0x2a, 0x37, 0x02, 0x01, 0x07, 0x04, 0x03, 0x02, 0x01, 0x3b, 0x45,
    0x23, 0x01, 0x00, 0x0d, 0x02, 0x01, 0x02, 0x2d, 0x03, 0x07, 0x01, 0x00,
    0x00, 0x02, 0x01, 0x00, 0xd2, 0x49, 0x6b, 0x10, 0xe0, 0x49, 0x6b, 0x07,
    0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x4d, 0x04, 0x23, 0x01, 0xe8, 0xb8, 0x09, 0xda,
};
#define GOLDEN_V2_SEQ 0x206u

// 两份样例共用的 v1 字段。
static pupu_save_t golden_v1_values(void) {
    pupu_save_t s;
    memset(&s, 0, sizeof(s));
    pupu_model_t *m = &s.model;
    m->stage = PUPU_STAGE_CHILD;
    m->full = 42;
    m->happy = 55;
    m->bond = 258;
    m->candy = 7;
    m->life_min = 0x01020304u;
    m->life_sec = 59;
    m->day_s = 74565;
    m->bond_today = 13;
    m->candy_today = 2;
    m->synced_today = true;
    m->cookie_count = 2;
    m->cookie_win_min = 45;
    m->pat_count = 3;
    m->pat_win_min = 7;
    m->has_fed = true;
    m->fed_life_min = 0x01020000u;
    m->fed_acct = 1800000000u;
    s.acct_epoch = 1800003600u;
    s.rtc_us = 0x0001020304050607ull;
    s.token = 0x8877665544332211ull;
    // v1 存档没有这三个字段,读出来时按新孵化的噗噗补上
    m->coin = PUPU_COIN_START;
    m->work_today = 0;
    m->emo_min = 0;
    return s;
}

static pupu_save_t golden_v2_values(void) {
    pupu_save_t s = golden_v1_values();
    s.model.coin = 77;
    s.model.work_today = 4;
    s.model.emo_min = 0x0123;
    return s;
}

static bool same_save(const pupu_save_t *a, const pupu_save_t *b) {
    const pupu_model_t *x = &a->model;
    const pupu_model_t *y = &b->model;
    return x->stage == y->stage && x->full == y->full && x->happy == y->happy
        && x->bond == y->bond && x->candy == y->candy
        && x->life_min == y->life_min && x->life_sec == y->life_sec && x->day_s == y->day_s
        && x->bond_today == y->bond_today && x->candy_today == y->candy_today
        && x->synced_today == y->synced_today
        && x->cookie_count == y->cookie_count && x->cookie_win_min == y->cookie_win_min
        && x->pat_count == y->pat_count && x->pat_win_min == y->pat_win_min
        && x->has_fed == y->has_fed && x->fed_life_min == y->fed_life_min
        && x->fed_acct == y->fed_acct
        && x->coin == y->coin && x->work_today == y->work_today && x->emo_min == y->emo_min
        && a->acct_epoch == b->acct_epoch && a->rtc_us == b->rtc_us && a->token == b->token;
}

// 改了记录里的字节以后重新算 CRC,用来造"CRC 正确但内容不对"的记录。
static void refresh_crc(uint8_t *buf, size_t len) {
    uint32_t crc = pupu_crc32(buf, len - 4);
    for (int i = 0; i < 4; i++) buf[len - 4 + i] = (uint8_t)(crc >> (8 * i));
}

static void test_crc32(void) {
    const uint8_t check[] = "123456789";
    assert(pupu_crc32(check, 9) == 0xCBF43926u);
    assert(pupu_crc32(check, 0) == 0);
}

static void test_sizes(void) {
    assert(PUPU_SAVE_VERSION == 2);
    assert(PUPU_SAVE_PAYLOAD_SIZE_V1 == 51 && PUPU_SAVE_PAYLOAD_SIZE_V2 == 55);
    assert(PUPU_SAVE_PAYLOAD_SIZE == PUPU_SAVE_PAYLOAD_SIZE_V2);
    assert(PUPU_SAVE_RECORD_SIZE == 71 && V1_RECORD_SIZE == 67);
}

// 旧固件写的 v1 存档:能读,宠物状态原样,零花钱补成 30、没在打工、没在 emo。
static void test_golden_v1_migrates(void) {
    pupu_save_t expected = golden_v1_values();
    pupu_save_t out;
    memset(&out, 0xA5, sizeof(out));  // 先填满垃圾,确认每个字段都被写了
    uint32_t seq = 0;
    assert(pupu_save_decode(k_golden_v1, sizeof(k_golden_v1), &out, &seq) == PUPU_SAVE_OK);
    assert(seq == GOLDEN_V1_SEQ);
    assert(same_save(&out, &expected));
    assert(out.model.coin == 30 && out.model.work_today == 0 && out.model.emo_min == 0);

    // 读出来再写,就是一条 v2 记录(71 字节,版本号 2),再读回来和迁移结果一样。
    uint8_t buf[PUPU_SAVE_RECORD_SIZE];
    assert(pupu_save_encode(&out, GOLDEN_V1_SEQ, buf, sizeof(buf)) == PUPU_SAVE_RECORD_SIZE);
    assert(buf[4] == 2 && buf[5] == 0 && buf[10] == PUPU_SAVE_PAYLOAD_SIZE_V2);
    assert(memcmp(buf + 12, k_golden_v1 + 12, PUPU_SAVE_PAYLOAD_SIZE_V1) == 0);  // v1 部分逐字节一样
    assert(buf[12 + 51] == 30 && buf[12 + 52] == 0 && buf[12 + 53] == 0 && buf[12 + 54] == 0);
    pupu_save_t again;
    assert(pupu_save_decode(buf, sizeof(buf), &again, &seq) == PUPU_SAVE_OK);
    assert(same_save(&again, &expected));
}

static void test_golden_v2(void) {
    pupu_save_t expected = golden_v2_values();
    pupu_save_t out;
    uint32_t seq = 0;
    assert(pupu_save_decode(k_golden_v2, sizeof(k_golden_v2), &out, &seq) == PUPU_SAVE_OK);
    assert(seq == GOLDEN_V2_SEQ);
    assert(same_save(&out, &expected));

    uint8_t buf[PUPU_SAVE_RECORD_SIZE];
    assert(pupu_save_encode(&expected, GOLDEN_V2_SEQ, buf, sizeof(buf)) == sizeof(buf));
    assert(memcmp(buf, k_golden_v2, sizeof(buf)) == 0);
}

static void test_round_trip(void) {
    pupu_save_t s;
    memset(&s, 0, sizeof(s));
    pupu_model_init(&s.model);
    uint8_t buf[PUPU_SAVE_RECORD_SIZE + 8];
    assert(pupu_save_encode(&s, 1, buf, sizeof(buf)) == PUPU_SAVE_RECORD_SIZE);
    pupu_save_t out;
    uint32_t seq = 0;
    assert(pupu_save_decode(buf, PUPU_SAVE_RECORD_SIZE, &out, &seq) == PUPU_SAVE_OK);
    assert(seq == 1 && same_save(&out, &s));
    assert(out.model.coin == PUPU_COIN_START);

    // 每个字段都取最大值,确认没有字段被截断。
    s.model.stage = PUPU_STAGE_ADULT;
    s.model.full = 100;
    s.model.happy = 100;
    s.model.bond = UINT16_MAX;
    s.model.candy = 99;
    s.model.life_min = UINT32_MAX;
    s.model.day_s = 86399;
    s.model.fed_life_min = UINT32_MAX;
    s.model.fed_acct = UINT32_MAX;
    s.model.coin = UINT8_MAX;
    s.model.work_today = UINT8_MAX;
    s.model.emo_min = UINT16_MAX;
    s.acct_epoch = UINT32_MAX;
    s.rtc_us = UINT64_MAX;
    s.token = UINT64_MAX;
    assert(pupu_save_encode(&s, UINT32_MAX, buf, sizeof(buf)) == PUPU_SAVE_RECORD_SIZE);
    assert(pupu_save_decode(buf, PUPU_SAVE_RECORD_SIZE, &out, &seq) == PUPU_SAVE_OK);
    assert(seq == UINT32_MAX && same_save(&out, &s));

    // 缓冲区不够时不写。
    assert(pupu_save_encode(&s, 1, buf, PUPU_SAVE_RECORD_SIZE - 1) == 0);
}

static void test_corruption(void) {
    uint8_t buf[PUPU_SAVE_RECORD_SIZE];
    pupu_save_t out;
    uint32_t seq = 0;

    // 数据里任何一个字节坏了,或者 CRC 本身坏了,都能发现(v2 和 v1 的样例都查)。
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (i < 4 || (i >= 10 && i < 12)) continue;  // magic 和 len 坏了会先报别的错
        memcpy(buf, k_golden_v2, sizeof(buf));
        buf[i] ^= 0x01;
        assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_BAD_CRC);
    }
    uint8_t v1[V1_RECORD_SIZE];
    for (size_t i = 0; i < sizeof(v1); i++) {
        if (i < 4 || (i >= 10 && i < 12)) continue;
        memcpy(v1, k_golden_v1, sizeof(v1));
        v1[i] ^= 0x01;
        assert(pupu_save_decode(v1, sizeof(v1), &out, &seq) == PUPU_SAVE_BAD_CRC);
    }

    memcpy(buf, k_golden_v2, sizeof(buf));
    buf[0] = 'X';
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_BAD_MAGIC);
}

static void test_bad_length(void) {
    uint8_t buf[PUPU_SAVE_RECORD_SIZE];
    pupu_save_t out;
    uint32_t seq = 0;
    memcpy(buf, k_golden_v2, sizeof(buf));

    assert(pupu_save_decode(buf, 0, &out, &seq) == PUPU_SAVE_BAD_LENGTH);
    assert(pupu_save_decode(buf, PUPU_SAVE_HEADER_SIZE + 3, &out, &seq) == PUPU_SAVE_BAD_LENGTH);
    assert(pupu_save_decode(buf, sizeof(buf) - 1, &out, &seq) == PUPU_SAVE_BAD_LENGTH);

    // len 字段和实际长度对不上。
    buf[10] = PUPU_SAVE_PAYLOAD_SIZE - 1;
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_BAD_LENGTH);

    // 记录本身自洽(CRC 正确),但 v2 的 payload 长度不对。
    assert(pupu_save_decode(buf, sizeof(buf) - 1, &out, &seq) == PUPU_SAVE_BAD_CRC);
    refresh_crc(buf, sizeof(buf) - 1);
    assert(pupu_save_decode(buf, sizeof(buf) - 1, &out, &seq) == PUPU_SAVE_BAD_LENGTH);

    // 版本号写 2 但只有 v1 的 51 字节:长度不对,不能当 v1 读。
    uint8_t v1[V1_RECORD_SIZE];
    memcpy(v1, k_golden_v1, sizeof(v1));
    v1[4] = 2;
    refresh_crc(v1, sizeof(v1));
    assert(pupu_save_decode(v1, sizeof(v1), &out, &seq) == PUPU_SAVE_BAD_LENGTH);
    // 版本号写 1 但有 v2 的 55 字节:同样不对。
    memcpy(buf, k_golden_v2, sizeof(buf));
    buf[4] = 1;
    refresh_crc(buf, sizeof(buf));
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_BAD_LENGTH);
}

static void test_versions(void) {
    uint8_t buf[PUPU_SAVE_RECORD_SIZE];
    pupu_save_t out;
    pupu_save_t untouched = golden_v2_values();
    uint32_t seq = 77;

    // 更新版本的固件写的存档:报 FUTURE,不改输出,调用方据此不覆盖它。
    memcpy(buf, k_golden_v2, sizeof(buf));
    buf[4] = PUPU_SAVE_VERSION + 1;
    refresh_crc(buf, sizeof(buf));
    out = untouched;
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_FUTURE);
    assert(same_save(&out, &untouched) && seq == 77);

    // 版本号的高字节也要算。
    memcpy(buf, k_golden_v2, sizeof(buf));
    buf[5] = 0x01;
    refresh_crc(buf, sizeof(buf));
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_FUTURE);

    // 损坏的记录哪怕版本号看起来更新,也只算损坏,不能挡住覆盖。
    memcpy(buf, k_golden_v2, sizeof(buf));
    buf[4] = PUPU_SAVE_VERSION + 1;
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_BAD_CRC);

    // 版本 0 不存在。
    memcpy(buf, k_golden_v2, sizeof(buf));
    buf[4] = 0;
    refresh_crc(buf, sizeof(buf));
    assert(pupu_save_decode(buf, sizeof(buf), &out, &seq) == PUPU_SAVE_BAD_VERSION);
}

int main(void) {
    test_crc32();
    test_sizes();
    test_golden_v1_migrates();
    test_golden_v2();
    test_round_trip();
    test_corruption();
    test_bad_length();
    test_versions();
    return 0;
}
