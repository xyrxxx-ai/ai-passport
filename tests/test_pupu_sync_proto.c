#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pupu_clock.h"
#include "pupu_config.h"
#include "pupu_model.h"
#include "pupu_save_codec.h"
#include "pupu_sync_proto.h"

#define T0 1798761600u  // 2027-01-01 00:00:00 UTC,和 tests/pupu_sync_vectors.txt 一致
#define HOUR_S 3600u

static pupu_model_t s_model;
static pupu_clock_t s_clock;
static pupu_sync_t s_sync;
static uint64_t s_snap_token = 0x0123456789ABCDEFull;

static void snapshot(pupu_save_t *out) {
    memset(out, 0, sizeof(*out));
    out->model = s_model;
    out->acct_epoch = s_clock.acct_now;
    out->rtc_us = 123456789ull;
    out->token = s_snap_token;
}

static const pupu_sync_env_t ENV = {&s_model, &s_clock, snapshot};

// 向量文件开头写的那个状态
static void vector_state(void) {
    pupu_model_init(&s_model);
    const bool hatched = pupu_model_hatch(&s_model);
    assert(hatched);
    s_model.full = 84;
    s_model.happy = 75;
    s_model.bond = 42;
    s_model.candy = 3;
    s_model.life_min = 1000;
    s_model.life_sec = 0;
    s_model.day_s = 3600;
    s_model.has_fed = true;
    s_model.fed_life_min = 910;
    s_model.fed_acct = 0;
    pupu_clock_init(&s_clock, 1, T0);
    pupu_sync_init(&s_sync);
    pupu_sync_begin_window(&s_sync);
    s_snap_token = 0x0123456789ABCDEFull;
}

static pupu_sync_result_t send(const uint8_t *msg, size_t len) {
    return pupu_sync_handle(&s_sync, &ENV, msg, len);
}

static void time_msg(uint8_t *b, uint8_t seq, uint32_t epoch) {
    b[0] = PUPU_MSG_TIME;
    b[1] = seq;
    for (int i = 0; i < 4; i++) b[2 + i] = (uint8_t)(epoch >> (8 * i));
    b[6] = 0xE0;  // +480 分钟(东八区),v1 不用
    b[7] = 0x01;
}

static pupu_sync_result_t send_time(uint8_t seq, uint32_t epoch) {
    uint8_t b[8];
    time_msg(b, seq, epoch);
    return send(b, sizeof(b));
}

static void assert_err(const pupu_sync_result_t *r, uint8_t seq, uint8_t type, uint8_t code) {
    assert(r->reply_len == 4);
    assert(r->reply[0] == PUPU_MSG_ERR && r->reply[1] == seq);
    assert(r->reply[2] == type && r->reply[3] == code);
    assert(!r->time_accepted && !r->done);
}

static uint16_t u16_at(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

// ---- 共享向量:按顺序回放,每条回复必须逐字节一样 ----

static size_t parse_hex(const char *s, uint8_t *out, size_t cap) {
    size_t n = 0;
    while (*s && *s != '|' && *s != '\n') {
        if (*s == ' ' || *s == '\t' || *s == '\r') {
            s++;
            continue;
        }
        unsigned v;
        const int got = sscanf(s, "%2x", &v);
        assert(got == 1);
        assert(n < cap);
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static void test_shared_vectors(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s (run from the repository root)\n", path);
        exit(1);
    }
    vector_state();
    char line[512];
    char kind[8], name[64];
    uint8_t req[32], want[32];
    size_t req_len = 0;
    bool have_req = false;
    int pairs = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int used = 0;
        const int fields = sscanf(line, "%7s %63s %n", kind, name, &used);
        assert(fields == 2);
        if (strcmp(kind, "req") == 0) {
            assert(!have_req);
            req_len = parse_hex(line + used, req, sizeof(req));
            have_req = true;
            continue;
        }
        assert(strcmp(kind, "resp") == 0 && have_req);
        const size_t want_len = parse_hex(line + used, want, sizeof(want));
        const pupu_sync_result_t r = send(req, req_len);
        if (r.reply_len != want_len || memcmp(r.reply, want, want_len) != 0) {
            fprintf(stderr, "vector %s: reply differs\n  got ", name);
            for (int i = 0; i < r.reply_len; i++) fprintf(stderr, " %02x", r.reply[i]);
            fprintf(stderr, "\n");
            exit(1);
        }
        have_req = false;
        pairs++;
    }
    fclose(f);
    assert(!have_req);
    assert(pairs == 17);  // 改了向量文件就同时改这里,防止文件被截断还"通过"
}

// ---- 单项 ----

static void test_crc_and_chunks(void) {
    assert(pupu_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    assert(pupu_sync_chunk_count(0) == 0);
    assert(pupu_sync_chunk_count(1) == 1);
    assert(pupu_sync_chunk_count(17) == 1);
    assert(pupu_sync_chunk_count(18) == 2);
    assert(pupu_sync_chunk_count(34) == 2);
    assert(pupu_sync_chunk_count(35) == 3);
    assert(pupu_sync_chunk_count(PUPU_SAVE_RECORD_SIZE) == 4);
    assert(PUPU_SAVE_RECORD_SIZE == 67);
}

static void test_malformed(void) {
    vector_state();
    const pupu_model_t before = s_model;
    pupu_sync_result_t r = send(NULL, 0);
    assert_err(&r, 0, 0, PUPU_SYNC_ERR_LENGTH);  // 连 type 都没有
    const uint8_t one[1] = {PUPU_MSG_GET_STATUS};
    r = send(one, 1);
    assert_err(&r, 0, PUPU_MSG_GET_STATUS, PUPU_SYNC_ERR_LENGTH);  // 没有 seq,按 0 回

    // 每种请求长度差 1 都不行(包括被截断的 TIME 和 21 字节的写入)
    uint8_t big[21] = {0};
    const struct { uint8_t type; size_t len; } ok_len[] = {
        {PUPU_MSG_TIME, 8}, {PUPU_MSG_GET_STATUS, 2}, {PUPU_MSG_SAVE_BEGIN, 2},
        {PUPU_MSG_SAVE_GET, 3}, {PUPU_MSG_DONE, 2},
    };
    for (size_t i = 0; i < sizeof(ok_len) / sizeof(ok_len[0]); i++) {
        big[0] = ok_len[i].type;
        big[1] = 0x42;
        r = send(big, ok_len[i].len - 1);
        assert_err(&r, ok_len[i].len - 1 >= 2 ? 0x42 : 0, ok_len[i].type, PUPU_SYNC_ERR_LENGTH);
        r = send(big, ok_len[i].len + 1);
        assert_err(&r, 0x42, ok_len[i].type, PUPU_SYNC_ERR_LENGTH);
    }
    big[0] = PUPU_MSG_TIME;
    r = send(big, sizeof(big));
    assert_err(&r, 0x42, PUPU_MSG_TIME, PUPU_SYNC_ERR_LENGTH);
    // 设备发的类型、0x00 和 0xFF 都是"不认识",不管长度
    const uint8_t unknown[] = {0x00, 0x06, 0x81, 0x85, 0xEE, 0xFF};
    for (size_t i = 0; i < sizeof(unknown); i++) {
        big[0] = unknown[i];
        r = send(big, 2);
        assert_err(&r, 0x42, unknown[i], PUPU_SYNC_ERR_UNKNOWN);
        r = send(big, 1);
        assert_err(&r, 0, unknown[i], PUPU_SYNC_ERR_UNKNOWN);
    }
    // 格式不对的请求什么都不改
    assert(memcmp(&before, &s_model, sizeof(before)) == 0);
    assert(s_clock.acct_now == T0);
}

static void test_seq_echo(void) {
    vector_state();
    uint8_t m[8];
    time_msg(m, 0xFF, T0 + 60);
    pupu_sync_result_t r = send(m, 8);
    assert(r.reply[0] == PUPU_MSG_STATUS && r.reply[1] == 0xFF && r.reply_len == 14);
    const uint8_t gs[2] = {PUPU_MSG_GET_STATUS, 0x00};
    r = send(gs, 2);
    assert(r.reply[0] == PUPU_MSG_STATUS && r.reply[1] == 0x00);
    const uint8_t sb[2] = {PUPU_MSG_SAVE_BEGIN, 0x80};
    r = send(sb, 2);
    assert(r.reply[0] == PUPU_MSG_SAVE_INFO && r.reply[1] == 0x80 && r.reply_len == 10);
    const uint8_t sg[3] = {PUPU_MSG_SAVE_GET, 0x7F, 3};
    r = send(sg, 3);
    assert(r.reply[0] == PUPU_MSG_SAVE_CHUNK && r.reply[1] == 0x7F && r.reply[2] == 3);
    assert(r.reply_len == 3 + 16);  // 最后一块 67 − 3 × 17 = 16 字节
    const uint8_t dn[2] = {PUPU_MSG_DONE, 0xA5};
    r = send(dn, 2);
    assert(r.reply[0] == PUPU_MSG_DONE_ACK && r.reply[1] == 0xA5 && r.reply_len == 2 && r.done);
}

static void test_snapshot_is_stable(void) {
    vector_state();
    const uint8_t sb1[2] = {PUPU_MSG_SAVE_BEGIN, 1};
    pupu_sync_result_t r = send(sb1, 2);
    uint8_t info1[10];
    memcpy(info1, r.reply, 10);
    // 拍完快照以后宠物状态变了:块里还是快照时的样子
    s_model.full = 1;
    s_snap_token = 0x1111;
    const uint8_t sg[3] = {PUPU_MSG_SAVE_GET, 2, 0};
    r = send(sg, 3);
    assert(r.reply[3 + 13] == 84);  // payload 第 2 个字节是饱腹:快照时是 84
    // 同一个 seq 重发 SAVE_BEGIN:沿用同一份快照
    r = send(sb1, 2);
    assert(memcmp(r.reply, info1, 10) == 0);
    // 新的 seq:重新拍一份(饱腹 1、令牌变了,CRC 也变了)
    const uint8_t sb2[2] = {PUPU_MSG_SAVE_BEGIN, 2};
    r = send(sb2, 2);
    assert(r.reply[1] == 2 && memcmp(r.reply + 6, info1 + 6, 4) != 0);
    r = send(sg, 3);
    assert(r.reply[3 + 13] == 1);

    // 整份快照能解码回来,两层 CRC 都对
    uint8_t all[PUPU_SAVE_RECORD_SIZE];
    for (uint8_t i = 0; i < 4; i++) {
        const uint8_t q[3] = {PUPU_MSG_SAVE_GET, (uint8_t)(10 + i), i};
        r = send(q, 3);
        memcpy(all + i * 17, r.reply + 3, r.reply_len - 3u);
    }
    assert(pupu_crc32(all, sizeof(all) - 4) == s_sync.snap_crc);
    assert(pupu_crc32(all, sizeof(all)) == 0x2144DF1Cu);  // 连自带的 CRC 一起算:永远是这个常数
    const uint32_t trailer = (uint32_t)all[63] | ((uint32_t)all[64] << 8) |
                             ((uint32_t)all[65] << 16) | ((uint32_t)all[66] << 24);
    assert(trailer == s_sync.snap_crc);
    pupu_save_t back;
    uint32_t seq = 99;
    assert(pupu_save_decode(all, sizeof(all), &back, &seq) == PUPU_SAVE_OK);
    assert(seq == 0 && back.model.full == 1 && back.token == 0x1111 && back.acct_epoch == T0);

    // 手机重新连上:上一次连接的快照不要了(随机起点的 seq 碰巧一样也不会拿到旧快照)
    pupu_sync_new_link(&s_sync);
    r = send(sb2, 2);
    assert(r.reply[0] == PUPU_MSG_SAVE_INFO);
    s_model.full = 7;
    pupu_sync_new_link(&s_sync);
    r = send(sb2, 2);
    r = send(sg, 3);
    assert(r.reply[3 + 13] == 7);

    // 新窗口:快照和 TIME 记录都丢掉,离开分钟保留
    s_sync.away_min = 77;
    pupu_sync_begin_window(&s_sync);
    r = send(sg, 3);
    assert_err(&r, 2, PUPU_MSG_SAVE_GET, PUPU_SYNC_ERR_NO_SNAPSHOT);
    assert(s_sync.away_min == 77 && !s_sync.has_time);
}

static void test_first_sync_ever(void) {
    pupu_model_init(&s_model);
    const bool hatched = pupu_model_hatch(&s_model);
    assert(hatched);
    const pupu_model_t before = s_model;
    pupu_clock_init(&s_clock, 1, 0);
    pupu_sync_init(&s_sync);
    pupu_sync_result_t r = send_time(1, T0);
    // 第一次同步:不补算,只记下时间,领首次同步奖励
    assert(r.time_accepted && r.away_pet_s == 0 && r.bonus.bond == PUPU_SYNC_BOND);
    assert(s_clock.acct_now == T0);
    assert(s_model.full == before.full && s_model.happy == before.happy);
    assert(s_model.bond == before.bond + PUPU_SYNC_BOND);
    assert(u16_at(r.reply + 11) == 0);  // away_min:没补算过
    // 同一天第二次(新的窗口、新的时间):补算,但不再加亲密
    pupu_sync_begin_window(&s_sync);
    r = send_time(2, T0 + 2 * HOUR_S);
    assert(r.time_accepted && r.away_pet_s == 2 * HOUR_S && r.bonus.bond == 0);
    assert(s_model.bond == before.bond + PUPU_SYNC_BOND);
    assert(u16_at(r.reply + 11) == 120);
}

static void test_egg_sync(void) {
    pupu_model_init(&s_model);
    pupu_clock_init(&s_clock, 1, T0);
    pupu_sync_init(&s_sync);
    pupu_sync_result_t r = send_time(1, T0 + 5 * HOUR_S);
    // 蛋:时间照样对上,数值不变,也不领奖励
    assert(r.time_accepted && r.bonus.result == PUPU_NOT_HATCHED && r.bonus.bond == 0);
    assert(s_model.full == PUPU_EGG_FULL && s_model.happy == PUPU_EGG_HAPPY && s_model.bond == 0);
    assert(r.reply[3] == PUPU_STAGE_EGG);
    assert(u16_at(r.reply + 9) == PUPU_SYNC_NEVER_FED);
}

static void test_cap_and_floor(void) {
    vector_state();
    s_model.full = 30;
    s_model.happy = 12;
    // 离开 26 小时:数值只按 8 小时补,补算部分最低到 10;屏幕和手机写原始的 26 小时
    pupu_sync_result_t r = send_time(1, T0 + 26 * HOUR_S);
    assert(r.time_accepted && r.away_pet_s == 26ull * HOUR_S);
    assert(s_model.full == PUPU_CATCH_UP_FLOOR && s_model.happy == PUPU_CATCH_UP_FLOOR);
    assert(u16_at(r.reply + 11) == 26 * 60);
    assert(r.reply[4] == PUPU_CATCH_UP_FLOOR && r.reply[5] == PUPU_CATCH_UP_FLOOR);
    // 跨过了一个宠物日,所以首次同步奖励还能领
    assert(r.bonus.bond == PUPU_SYNC_BOND);

    // 时间加速 60 倍:真实 10 分钟 = 宠物 10 小时,补 8 小时;away_min 是宠物分钟
    vector_state();
    s_clock.scale = 60;
    r = send_time(1, T0 + 600);
    assert(r.away_pet_s == 10ull * HOUR_S && u16_at(r.reply + 11) == 600);
    // 8 小时:饱腹 84 − 80、开心 75 − 64 以上,都停在最低值 10
    assert(s_model.full == PUPU_CATCH_UP_FLOOR && s_model.happy == PUPU_CATCH_UP_FLOOR);
    assert(s_clock.acct_now == T0 + 600);  // acct_now 一直是真实秒

    // 加速版里深睡醒来补算了 10 小时;对时时手机只快 1 秒(= 60 宠物秒):"最近离开"不能被改成 1 分钟
    vector_state();
    s_clock.scale = 60;
    pupu_sync_note_away(&s_sync, 10ull * HOUR_S);
    r = send_time(1, T0 + 1);
    assert(r.time_accepted && r.away_pet_s == 60 && u16_at(r.reply + 11) == 600);
    // 真实离开 1 分钟就算数
    vector_state();
    s_clock.scale = 60;
    pupu_sync_note_away(&s_sync, 10ull * HOUR_S);
    r = send_time(1, T0 + 60);
    assert(u16_at(r.reply + 11) == 60);
}

static void test_retry_and_wrong_time(void) {
    vector_state();
    pupu_sync_result_t r = send_time(9, T0 + HOUR_S);
    assert(r.time_accepted && s_model.bond == 44);
    // 手机没收到回复,过了 1 秒用同一个 seq 重发:当作同一条,不存档、不改 acct_now
    (void)pupu_clock_tick(&s_clock, 1000000);
    assert(s_clock.acct_now == T0 + HOUR_S + 1);
    r = send_time(9, T0 + HOUR_S);
    assert(!r.time_accepted && r.reply[0] == PUPU_MSG_STATUS && s_clock.acct_now == T0 + HOUR_S + 1);
    // 同一个 seq 但时间不同:不是重发,照常处理(早于 acct_now,不补算)
    r = send_time(9, T0 + HOUR_S - 30);
    assert(r.time_accepted && r.away_pet_s == 0 && s_clock.acct_now == T0 + HOUR_S - 30);
    // 2100 年的错误时间被接受,补算封顶 8 小时;下一次正确的时间把 acct_now 拉回来,不再补算
    r = send_time(10, 4102444800u);
    assert(r.time_accepted && s_clock.acct_now == 4102444800u);
    const uint8_t full_after_bad = s_model.full;
    r = send_time(11, T0 + 2 * HOUR_S);
    assert(r.time_accepted && r.away_pet_s == 0 && s_clock.acct_now == T0 + 2 * HOUR_S);
    assert(s_model.full == full_after_bad);
    // 早于 2026 的时间拒绝,什么都不改
    const pupu_model_t before = s_model;
    r = send_time(12, PUPU_EPOCH_MIN - 1);
    assert_err(&r, 12, PUPU_MSG_TIME, PUPU_SYNC_ERR_TIME);
    assert(memcmp(&before, &s_model, sizeof(before)) == 0 && s_clock.acct_now == T0 + 2 * HOUR_S);
    r = send_time(13, PUPU_EPOCH_MIN);
    assert(r.time_accepted);
    // 晚于 2100-01-01 也拒绝(acct_now 是 u32,不能让它走到回绕)
    r = send_time(14, PUPU_EPOCH_MAX + 1);
    assert_err(&r, 14, PUPU_MSG_TIME, PUPU_SYNC_ERR_TIME);
    r = send_time(15, 0xFFFFFFFFu);
    assert_err(&r, 15, PUPU_MSG_TIME, PUPU_SYNC_ERR_TIME);
    r = send_time(16, PUPU_EPOCH_MAX);
    assert(r.time_accepted && s_clock.acct_now == PUPU_EPOCH_MAX);
}

static void test_one_catch_up_per_window(void) {
    // 窗口里有人连发"一天以后"的时间:只有第一条补算、领奖励,后面的只校正时间
    vector_state();
    pupu_sync_result_t r = send_time(1, T0 + HOUR_S);
    assert(r.time_accepted && r.away_pet_s == HOUR_S && r.bonus.bond == PUPU_SYNC_BOND);
    const pupu_model_t after_first = s_model;
    for (uint32_t day = 1; day <= 50; day++) {
        r = send_time((uint8_t)(1 + day), T0 + HOUR_S + day * 86400u);
        assert(r.time_accepted && r.away_pet_s == 0 && r.bonus.bond == 0);
        assert(s_clock.acct_now == T0 + HOUR_S + day * 86400u);
    }
    assert(memcmp(&after_first, &s_model, sizeof(after_first)) == 0);
    assert(s_model.stage == PUPU_STAGE_CHILD && s_model.bond == 44);
    // 手机断开又连上(同一个窗口):还是只校正时间
    pupu_sync_new_link(&s_sync);
    r = send_time(99, T0 + 60u * 86400u);
    assert(r.time_accepted && r.away_pet_s == 0 && r.bonus.bond == 0);
    // 在设备上重新打开窗口:按上一次校正后的 acct_now 补算。被跳过的 60 天没有算进宠物日,
    // 所以还是同一个宠物日,奖励不会再给
    pupu_sync_begin_window(&s_sync);
    r = send_time(1, T0 + 60u * 86400u + 2 * HOUR_S);
    assert(r.time_accepted && r.away_pet_s == 2 * HOUR_S && r.bonus.bond == 0);
    assert(s_model.bond == 44);
}

static void test_bonus_after_day_rollover(void) {
    // 昨天已经领过首次同步奖励,离开期间跨进了新的一天:先补算(新的一天清零),再领今天的奖励
    vector_state();
    s_model.synced_today = true;
    s_model.bond_today = 2;
    s_model.day_s = PUPU_DAY_S - 600;
    const pupu_sync_result_t r = send_time(1, T0 + HOUR_S);
    assert(r.time_accepted && r.bonus.bond == PUPU_SYNC_BOND);
    assert(s_model.bond == 42 + PUPU_SYNC_BOND && s_model.synced_today);
}

static void test_fed_age(void) {
    pupu_model_t m;
    pupu_model_init(&m);
    assert(pupu_sync_fed_age_min(&m, T0) == PUPU_SYNC_NEVER_FED);
    m.has_fed = true;
    m.life_min = 500;
    m.fed_life_min = 380;
    m.fed_acct = 0;
    assert(pupu_sync_fed_age_min(&m, 0) == 120);   // 没同步过:宠物分钟
    assert(pupu_sync_fed_age_min(&m, T0) == 120);  // 喂食时还没同步过:也只能用宠物分钟
    m.fed_acct = T0;
    assert(pupu_sync_fed_age_min(&m, T0 + 3 * HOUR_S + 59) == 180);  // 真实分钟,向下取整
    assert(pupu_sync_fed_age_min(&m, T0 - 600) == 0);                // 时间被纠正得更早:0
    assert(pupu_sync_fed_age_min(&m, T0 + 100u * 24u * HOUR_S) == PUPU_SYNC_AGE_MAX);
    m.fed_acct = 0;
    m.life_min = 1u << 20;
    m.fed_life_min = 0;
    assert(pupu_sync_fed_age_min(&m, 0) == PUPU_SYNC_AGE_MAX);
}

static void test_note_away(void) {
    pupu_sync_t s;
    pupu_sync_init(&s);
    pupu_sync_note_away(&s, 5 * HOUR_S + 59);
    assert(s.away_min == 300);
    pupu_sync_note_away(&s, 0);  // 没离开:不覆盖上一次
    assert(s.away_min == 300);
    pupu_sync_note_away(&s, 59);  // 两边时钟差几秒:也不覆盖
    assert(s.away_min == 300);
    pupu_sync_note_away(&s, 60);
    assert(s.away_min == 1);
    pupu_sync_note_away(&s, 365ull * 24u * HOUR_S);
    assert(s.away_min == PUPU_SYNC_AGE_MAX);
}

// ---- 同步窗口 ----

static void test_window_no_phone(void) {
    pupu_sync_window_t w;
    pupu_sync_window_open(&w, 1000);
    assert(pupu_sync_window_wait_left_s(&w, 1000) == 60);
    assert(pupu_sync_window_wait_left_s(&w, 1000 + 59001) == 1);
    assert(pupu_sync_window_check(&w, 1000 + PUPU_SYNC_WAIT_MS - 1) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, 1000 + PUPU_SYNC_WAIT_MS) == PUPU_WIN_CLOSE_NO_PHONE);
    assert(pupu_sync_window_wait_left_s(&w, 1000 + PUPU_SYNC_WAIT_MS) == 0);
    // 收到请求但从没连上(不会发生,但不能让窗口一直开着)
    pupu_sync_window_open(&w, 0);
    pupu_sync_window_request(&w, 59000);
    assert(pupu_sync_window_check(&w, PUPU_SYNC_WAIT_MS) == PUPU_WIN_CLOSE_NO_PHONE);
}

static void test_window_idle(void) {
    pupu_sync_window_t w;
    pupu_sync_window_open(&w, 0);
    pupu_sync_window_link(&w, true, 50000);  // 第 50 秒才连上:不受 60 秒限制
    assert(pupu_sync_window_wait_left_s(&w, 50000) == 0);
    assert(pupu_sync_window_check(&w, 69999) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, 70000) == PUPU_WIN_CLOSE_IDLE);
    pupu_sync_window_request(&w, 65000);  // 有请求就重新数 20 秒
    assert(pupu_sync_window_check(&w, 84999) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, 85000) == PUPU_WIN_CLOSE_IDLE);
    // 断开不重新计时;重新连上算一次活动
    pupu_sync_window_link(&w, false, 80000);
    assert(pupu_sync_window_check(&w, 85000) == PUPU_WIN_CLOSE_IDLE);
    pupu_sync_window_link(&w, true, 84000);
    assert(pupu_sync_window_check(&w, 103999) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, 104000) == PUPU_WIN_CLOSE_IDLE);
}

static void test_window_max_and_done(void) {
    pupu_sync_window_t w;
    pupu_sync_window_open(&w, 0);
    pupu_sync_window_link(&w, true, 1000);
    for (uint32_t t = 10000; t < PUPU_SYNC_MAX_MS; t += 10000) {
        pupu_sync_window_request(&w, t);
        assert(pupu_sync_window_check(&w, t) == PUPU_WIN_OPEN);
    }
    assert(pupu_sync_window_check(&w, PUPU_SYNC_MAX_MS - 1) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, PUPU_SYNC_MAX_MS) == PUPU_WIN_CLOSE_MAX);

    // DONE:挥完手(1.5 秒)再关,重复的 DONE 不推迟
    pupu_sync_window_open(&w, 0);
    pupu_sync_window_link(&w, true, 1000);
    pupu_sync_window_done(&w, 5000);
    pupu_sync_window_done(&w, 6000);
    assert(pupu_sync_window_check(&w, 5000 + PUPU_SYNC_DONE_LINGER_MS - 1) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, 5000 + PUPU_SYNC_DONE_LINGER_MS) == PUPU_WIN_CLOSE_DONE);
    // 挥手的时候刚好到 2 分钟:还是算同步成功
    pupu_sync_window_open(&w, 0);
    pupu_sync_window_link(&w, true, 1000);
    pupu_sync_window_request(&w, 110000);
    pupu_sync_window_done(&w, PUPU_SYNC_MAX_MS - 500);
    assert(pupu_sync_window_check(&w, PUPU_SYNC_MAX_MS) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, PUPU_SYNC_MAX_MS + 1000) == PUPU_WIN_CLOSE_DONE);
    // 挥手时手机断开、没有请求:也只等挥完手
    pupu_sync_window_open(&w, 0);
    pupu_sync_window_link(&w, true, 0);
    pupu_sync_window_done(&w, 19500);
    pupu_sync_window_link(&w, false, 19600);
    assert(pupu_sync_window_check(&w, 20500) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, 21000) == PUPU_WIN_CLOSE_DONE);
}

static void test_window_wraps(void) {
    // 毫秒计数在窗口中间回绕
    const uint32_t t0 = 0xFFFFF000u;
    pupu_sync_window_t w;
    pupu_sync_window_open(&w, t0);
    assert(pupu_sync_window_check(&w, t0 + 30000u) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_wait_left_s(&w, t0 + 30000u) == 30);
    assert(pupu_sync_window_check(&w, t0 + PUPU_SYNC_WAIT_MS) == PUPU_WIN_CLOSE_NO_PHONE);
    pupu_sync_window_open(&w, t0);
    pupu_sync_window_link(&w, true, t0 + 1000u);
    assert(pupu_sync_window_check(&w, t0 + 20999u) == PUPU_WIN_OPEN);
    assert(pupu_sync_window_check(&w, t0 + 21000u) == PUPU_WIN_CLOSE_IDLE);
}

int main(int argc, char **argv) {
    test_shared_vectors(argc > 1 ? argv[1] : "tests/pupu_sync_vectors.txt");
    test_crc_and_chunks();
    test_malformed();
    test_seq_echo();
    test_snapshot_is_stable();
    test_first_sync_ever();
    test_egg_sync();
    test_cap_and_floor();
    test_retry_and_wrong_time();
    test_bonus_after_day_rollover();
    test_one_catch_up_per_window();
    test_fed_age();
    test_note_away();
    test_window_no_phone();
    test_window_idle();
    test_window_max_and_done();
    test_window_wraps();
    printf("test_pupu_sync_proto: PASS\n");
    return 0;
}
