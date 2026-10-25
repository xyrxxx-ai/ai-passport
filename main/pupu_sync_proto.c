#include "pupu_sync_proto.h"

#include "pupu_config.h"

#include <string.h>

// 每种请求的固定长度
#define LEN_TIME 8
#define LEN_PLAIN 2
#define LEN_SAVE_GET 3

#define LEN_STATUS 14
#define LEN_SAVE_INFO 10
#define LEN_ERR 4

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t clamp_age(uint64_t min) {
    return min > PUPU_SYNC_AGE_MAX ? (uint16_t)PUPU_SYNC_AGE_MAX : (uint16_t)min;
}

void pupu_sync_init(pupu_sync_t *s) {
    memset(s, 0, sizeof(*s));
}

void pupu_sync_begin_window(pupu_sync_t *s) {
    const uint16_t away = s->away_min;
    pupu_sync_init(s);
    s->away_min = away;
}

void pupu_sync_new_link(pupu_sync_t *s) {
    s->snap_len = 0;
}

void pupu_sync_note_away(pupu_sync_t *s, uint64_t away_pet_s) {
    if (away_pet_s >= 60u) s->away_min = clamp_age(away_pet_s / 60u);
}

unsigned pupu_sync_chunk_count(size_t total_len) {
    return (unsigned)((total_len + PUPU_SYNC_CHUNK_MAX - 1) / PUPU_SYNC_CHUNK_MAX);
}

uint16_t pupu_sync_fed_age_min(const pupu_model_t *m, uint32_t acct_now) {
    if (!m->has_fed) return PUPU_SYNC_NEVER_FED;
    uint64_t min = 0;
    if (acct_now != 0 && m->fed_acct != 0) {
        // 手机时间被纠正得比喂食时还早时算 0,不回绕
        if (acct_now > m->fed_acct) min = (acct_now - m->fed_acct) / 60u;
    } else if (m->life_min > m->fed_life_min) {
        min = m->life_min - m->fed_life_min;
    }
    return clamp_age(min);
}

static uint8_t reply_err(pupu_sync_result_t *r, uint8_t seq, uint8_t req_type, pupu_sync_err_t code) {
    r->reply[0] = PUPU_MSG_ERR;
    r->reply[1] = seq;
    r->reply[2] = req_type;
    r->reply[3] = (uint8_t)code;
    return LEN_ERR;
}

static uint8_t reply_status(pupu_sync_result_t *r, uint8_t seq, const pupu_sync_t *s,
                            const pupu_sync_env_t *env) {
    const pupu_model_t *m = env->model;
    uint8_t *p = r->reply;
    p[0] = PUPU_MSG_STATUS;
    p[1] = seq;
    p[2] = PUPU_SYNC_PROTO_VER;
    p[3] = m->stage;
    p[4] = m->full;
    p[5] = m->happy;
    put_u16(p + 6, m->bond);
    p[8] = m->candy;
    put_u16(p + 9, pupu_sync_fed_age_min(m, env->clock->acct_now));
    put_u16(p + 11, s->away_min);
    p[13] = 0;  // flags:v1 固定为 0
    return LEN_STATUS;
}

static uint8_t reply_save_info(pupu_sync_result_t *r, uint8_t seq, const pupu_sync_t *s) {
    uint8_t *p = r->reply;
    p[0] = PUPU_MSG_SAVE_INFO;
    p[1] = seq;
    p[2] = PUPU_SAVE_VERSION;
    put_u16(p + 3, s->snap_len);
    p[5] = (uint8_t)pupu_sync_chunk_count(s->snap_len);
    put_u32(p + 6, s->snap_crc);
    return LEN_SAVE_INFO;
}

static void handle_time(pupu_sync_t *s, const pupu_sync_env_t *env, const uint8_t *msg,
                        pupu_sync_result_t *r) {
    const uint8_t seq = msg[1];
    const uint32_t epoch = get_u32(msg + 2);
    // msg[6..7] 是时区(分钟):宠物日不按日历算,v1 不用它。
    if (s->has_time && seq == s->time_seq && epoch == s->time_epoch) {
        // 手机没收到回复又重发了同一条:只回状态,什么都不改
        r->reply_len = reply_status(r, seq, s, env);
        return;
    }
    uint64_t away_real_s = 0;
    if (!pupu_clock_sync(env->clock, epoch, &away_real_s)) {
        r->reply_len = reply_err(r, seq, PUPU_MSG_TIME, PUPU_SYNC_ERR_TIME);
        return;
    }
    if (s->has_time) {
        // 这个窗口已经补算过:只校正时间(acct_now 已经设好),不再补算、不再领奖励
        s->time_seq = seq;
        s->time_epoch = epoch;
        r->time_accepted = true;
        r->reply_len = reply_status(r, seq, s, env);
        return;
    }
    // 顺序见 pupu_clock.h:先补算,再领首次同步奖励
    const uint64_t away_pet_s = pupu_clock_to_pet(env->clock, away_real_s);
    if (away_pet_s > 0) (void)pupu_model_catch_up(env->model, away_pet_s);
    r->bonus = pupu_model_apply_sync(env->model);
    // 两边时钟差几秒很正常:不到 1 分钟真实时间的补算不覆盖"最近离开"。按真实秒判断,
    // 加速版里 1 秒就是 60 宠物秒,按宠物秒判断挡不住。
    if (away_real_s >= 60u) pupu_sync_note_away(s, away_pet_s);
    s->has_time = true;
    s->time_seq = seq;
    s->time_epoch = epoch;
    r->time_accepted = true;
    r->away_pet_s = away_pet_s;
    r->reply_len = reply_status(r, seq, s, env);
}

static void handle_save_begin(pupu_sync_t *s, const pupu_sync_env_t *env, uint8_t seq,
                              pupu_sync_result_t *r) {
    // 同一个 seq 是重发:沿用同一份快照,手机先后收到的两条 SAVE_INFO 才对得上
    if (s->snap_len == 0 || seq != s->snap_seq) {
        pupu_save_t save;
        env->snapshot(&save);
        // 备份里的序号写 0:以后从手机恢复(v1.1)时由设备重新编号
        s->snap_len = (uint8_t)pupu_save_encode(&save, 0, s->snap, sizeof(s->snap));
        // 不包括记录末尾自带的 CRC:连它一起算的话,结果永远是常数 0x2144DF1C
        s->snap_crc = pupu_crc32(s->snap, s->snap_len - 4u);
        s->snap_seq = seq;
    }
    r->reply_len = reply_save_info(r, seq, s);
}

static void handle_save_get(const pupu_sync_t *s, uint8_t seq, uint8_t idx, pupu_sync_result_t *r) {
    if (s->snap_len == 0) {
        r->reply_len = reply_err(r, seq, PUPU_MSG_SAVE_GET, PUPU_SYNC_ERR_NO_SNAPSHOT);
        return;
    }
    if (idx >= pupu_sync_chunk_count(s->snap_len)) {
        r->reply_len = reply_err(r, seq, PUPU_MSG_SAVE_GET, PUPU_SYNC_ERR_INDEX);
        return;
    }
    const size_t off = (size_t)idx * PUPU_SYNC_CHUNK_MAX;
    const size_t n = s->snap_len - off < PUPU_SYNC_CHUNK_MAX ? s->snap_len - off : PUPU_SYNC_CHUNK_MAX;
    r->reply[0] = PUPU_MSG_SAVE_CHUNK;
    r->reply[1] = seq;
    r->reply[2] = idx;
    memcpy(r->reply + 3, s->snap + off, n);
    r->reply_len = (uint8_t)(3 + n);
}

static size_t expected_len(uint8_t type) {
    switch (type) {
    case PUPU_MSG_TIME: return LEN_TIME;
    case PUPU_MSG_GET_STATUS:
    case PUPU_MSG_SAVE_BEGIN:
    case PUPU_MSG_DONE: return LEN_PLAIN;
    case PUPU_MSG_SAVE_GET: return LEN_SAVE_GET;
    default: return 0;  // 不认识
    }
}

pupu_sync_result_t pupu_sync_handle(pupu_sync_t *s, const pupu_sync_env_t *env,
                                    const uint8_t *msg, size_t len) {
    pupu_sync_result_t r;
    memset(&r, 0, sizeof(r));
    // 空消息连 type 都没有;只有 1 字节时没有 seq,都按 seq 0 回
    const uint8_t type = len >= 1 ? msg[0] : 0;
    const uint8_t seq = len >= 2 ? msg[1] : 0;
    const size_t want = expected_len(type);
    if (len >= 1 && want == 0) {
        r.reply_len = reply_err(&r, seq, type, PUPU_SYNC_ERR_UNKNOWN);
        return r;
    }
    if (len == 0 || len != want) {
        r.reply_len = reply_err(&r, seq, type, PUPU_SYNC_ERR_LENGTH);
        return r;
    }
    switch (type) {
    case PUPU_MSG_TIME: handle_time(s, env, msg, &r); break;
    case PUPU_MSG_GET_STATUS: r.reply_len = reply_status(&r, seq, s, env); break;
    case PUPU_MSG_SAVE_BEGIN: handle_save_begin(s, env, seq, &r); break;
    case PUPU_MSG_SAVE_GET: handle_save_get(s, seq, msg[2], &r); break;
    default:  // PUPU_MSG_DONE
        r.reply[0] = PUPU_MSG_DONE_ACK;
        r.reply[1] = seq;
        r.reply_len = LEN_PLAIN;
        r.done = true;
        break;
    }
    return r;
}

// ---- 同步窗口 ----

void pupu_sync_window_open(pupu_sync_window_t *w, uint32_t now_ms) {
    memset(w, 0, sizeof(*w));
    w->open_ms = now_ms;
    w->activity_ms = now_ms;
}

void pupu_sync_window_link(pupu_sync_window_t *w, bool connected, uint32_t now_ms) {
    w->connected = connected;
    if (!connected) return;
    w->ever_connected = true;
    w->activity_ms = now_ms;
}

void pupu_sync_window_request(pupu_sync_window_t *w, uint32_t now_ms) {
    w->activity_ms = now_ms;
}

void pupu_sync_window_done(pupu_sync_window_t *w, uint32_t now_ms) {
    if (w->done) return;
    w->done = true;
    w->done_ms = now_ms;
}

pupu_win_close_t pupu_sync_window_check(const pupu_sync_window_t *w, uint32_t now_ms) {
    // 收到 DONE 以后只等挥完手:这 1.5 秒里不因为别的原因关(哪怕刚好到 2 分钟)
    if (w->done) {
        return now_ms - w->done_ms >= PUPU_SYNC_DONE_LINGER_MS ? PUPU_WIN_CLOSE_DONE : PUPU_WIN_OPEN;
    }
    if (now_ms - w->open_ms >= PUPU_SYNC_MAX_MS) return PUPU_WIN_CLOSE_MAX;
    if (!w->ever_connected) {
        return now_ms - w->open_ms >= PUPU_SYNC_WAIT_MS ? PUPU_WIN_CLOSE_NO_PHONE : PUPU_WIN_OPEN;
    }
    return now_ms - w->activity_ms >= PUPU_SYNC_IDLE_MS ? PUPU_WIN_CLOSE_IDLE : PUPU_WIN_OPEN;
}

unsigned pupu_sync_window_wait_left_s(const pupu_sync_window_t *w, uint32_t now_ms) {
    const uint32_t gone = now_ms - w->open_ms;
    if (w->ever_connected || gone >= PUPU_SYNC_WAIT_MS) return 0;
    return (unsigned)((PUPU_SYNC_WAIT_MS - gone + 999u) / 1000u);
}
