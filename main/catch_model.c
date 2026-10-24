#include "catch_model.h"

#include <string.h>

const catch_tuning_t catch_tuning = {
    .spawn_min_ticks = 45,
    .spawn_max_ticks = 50,
    .cloud_pct = 20,
    .speed_q4 = 42,
    .first_spawn_tick = 10,
};

#define SEED_FALLBACK 0x9E3779B9u
#define LINE_Q4 (CATCH_LINE_CX * CATCH_Q4)
#define GONE_Q4 (CATCH_GONE_CX * CATCH_Q4)

static uint32_t next_rand(catch_model_t *m) {
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return x;
}

// [lo, hi] 里的一个随机整数。
static uint32_t rand_range(catch_model_t *m, uint32_t lo, uint32_t hi) {
    return lo + next_rand(m) % (hi - lo + 1u);
}

void catch_init(catch_model_t *m, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    m->state = CATCH_READY;
    m->lane = 1;
    m->countdown = CATCH_COUNTDOWN_TICKS;
    m->next_spawn = catch_tuning.first_spawn_tick;
    m->rng = seed != 0 ? seed : SEED_FALLBACK;
}

uint16_t catch_travel_ticks(void) {
    const int32_t dist = (CATCH_SPAWN_CX - CATCH_LINE_CX) * CATCH_Q4;
    return (uint16_t)((dist + catch_tuning.speed_q4 - 1) / catch_tuning.speed_q4);
}

static uint32_t move_lane(catch_model_t *m, int delta) {
    const int lane = (int)m->lane + delta;
    if (lane < 0 || lane >= CATCH_LANES) return 0;
    m->lane = (uint8_t)lane;
    return CATCH_EV_LANE;
}

uint32_t catch_input(catch_model_t *m, catch_input_t in) {
    const uint8_t st = m->state;
    if (st == CATCH_FINISHED || st == CATCH_ABORTED) return 0;
    switch (in) {
    case CATCH_IN_UP:
    case CATCH_IN_DOWN:
        if (st == CATCH_PAUSED) return 0;
        return move_lane(m, in == CATCH_IN_UP ? -1 : 1);
    case CATCH_IN_OK_PRESS:
        if (st == CATCH_PLAYING) {
            m->state = CATCH_PAUSED;
            m->resume_on_release = false;
            return CATCH_EV_PAUSED;
        }
        if (st == CATCH_PAUSED) m->resume_on_release = true;
        return 0;
    case CATCH_IN_OK_RELEASE:
        if (st == CATCH_PAUSED && m->resume_on_release) {
            m->state = CATCH_PLAYING;
            m->resume_on_release = false;
            return CATCH_EV_RESUMED;
        }
        return 0;
    case CATCH_IN_OK_LONG:
        m->state = CATCH_ABORTED;
        return CATCH_EV_EXIT;
    }
    return 0;
}

static uint32_t spawn(catch_model_t *m) {
    m->next_spawn = (uint16_t)(m->ticks +
        rand_range(m, catch_tuning.spawn_min_ticks, catch_tuning.spawn_max_ticks));
    // 先把随机数都抽完,这样"位置满了"不会改变后面的随机序列
    const bool cloud = rand_range(m, 0, 99) < catch_tuning.cloud_pct;
    const uint8_t lane = (uint8_t)rand_range(m, 0, CATCH_LANES - 1);
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        catch_item_t *it = &m->items[i];
        if (it->kind != CATCH_ITEM_NONE) continue;
        it->kind = cloud ? CATCH_ITEM_CLOUD : CATCH_ITEM_STAR;
        it->lane = lane;
        it->resolved = false;
        it->cx_q4 = CATCH_SPAWN_CX * CATCH_Q4;
        if (cloud) {
            m->spawned_clouds++;
        } else {
            m->spawned_stars++;
        }
        return CATCH_EV_SPAWN;
    }
    return CATCH_EV_FULL;
}

static uint32_t resolve(catch_model_t *m, catch_item_t *it) {
    it->resolved = true;
    const bool here = it->lane == m->lane;
    if (it->kind == CATCH_ITEM_STAR) {
        if (!here) return CATCH_EV_MISS;  // 飞过去,继续往左飞出屏幕
        if (m->stars < UINT8_MAX) m->stars++;
        if (m->caught < UINT8_MAX) m->caught++;
        it->kind = CATCH_ITEM_NONE;       // 接住了:消失
        return CATCH_EV_STAR;
    }
    if (!here) return 0;  // 躲开了乌云
    if (m->stars > 0) m->stars--;
    if (m->clouds_hit < UINT8_MAX) m->clouds_hit++;
    it->kind = CATCH_ITEM_NONE;
    return CATCH_EV_CLOUD;
}

uint32_t catch_tick(catch_model_t *m) {
    if (m->state == CATCH_READY) {
        if (m->countdown > 0) m->countdown--;
        if (m->countdown > 0) return 0;
        m->state = CATCH_PLAYING;
        return CATCH_EV_START;
    }
    if (m->state != CATCH_PLAYING) return 0;

    uint32_t ev = 0;
    m->ticks++;
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        catch_item_t *it = &m->items[i];
        if (it->kind == CATCH_ITEM_NONE) continue;
        const int32_t prev = it->cx_q4;
        it->cx_q4 -= catch_tuning.speed_q4;
        // 越线的那一帧计分,只算一次:不会因为走得快跳过,也不会因为走得慢算两次
        if (!it->resolved && prev >= LINE_Q4 && it->cx_q4 < LINE_Q4) ev |= resolve(m, it);
        if (it->kind != CATCH_ITEM_NONE && it->cx_q4 < GONE_Q4) it->kind = CATCH_ITEM_NONE;
    }
    if (m->ticks >= m->next_spawn && m->ticks + catch_travel_ticks() <= CATCH_ROUND_TICKS) {
        ev |= spawn(m);
    }
    if (m->ticks >= CATCH_ROUND_TICKS) {
        m->state = CATCH_FINISHED;
        ev |= CATCH_EV_FINISHED;
    }
    return ev;
}

unsigned catch_seconds_left(const catch_model_t *m) {
    const unsigned left = CATCH_ROUND_TICKS - (m->ticks < CATCH_ROUND_TICKS ? m->ticks : CATCH_ROUND_TICKS);
    const unsigned per_s = 1000 / CATCH_TICK_MS;
    return (left + per_s - 1) / per_s;
}

unsigned catch_countdown_number(const catch_model_t *m) {
    if (m->state != CATCH_READY) return 0;
    const unsigned per_s = 1000 / CATCH_TICK_MS;
    return (m->countdown + per_s - 1) / per_s;
}

catch_result_t catch_result(const catch_model_t *m) {
    const catch_result_t r = {.completed = m->state == CATCH_FINISHED, .stars = m->stars};
    return r;
}

bool catch_active(const catch_model_t *m) {
    return m->state == CATCH_READY || m->state == CATCH_PLAYING;
}

bool catch_in_round(const catch_model_t *m) {
    return catch_active(m) || m->state == CATCH_PAUSED;
}
