#include "pupu_model.h"

#include <string.h>

#include "pupu_config.h"

static int8_t add_stat(uint8_t *value, int delta) {
    int next = *value + delta;
    if (next < 0) next = 0;
    if (next > PUPU_STAT_MAX) next = PUPU_STAT_MAX;
    int8_t applied = (int8_t)(next - *value);
    *value = (uint8_t)next;
    return applied;
}

// 所有来源的亲密都走这里,统一套每日上限。返回实际加了多少。
static uint8_t add_bond(pupu_model_t *m, uint8_t want) {
    uint8_t room = (uint8_t)(PUPU_BOND_PER_DAY - m->bond_today);
    uint8_t gain = want < room ? want : room;
    m->bond = (uint16_t)(m->bond + gain);
    m->bond_today = (uint8_t)(m->bond_today + gain);
    return gain;
}

static bool check_grow(pupu_model_t *m) {
    if (m->stage != PUPU_STAGE_CHILD || m->bond < PUPU_ADULT_BOND) return false;
    m->stage = PUPU_STAGE_ADULT;
    return true;
}

// 走一整分钟。full_floor/happy_floor 是这次推进允许降到的最低值:在线时是 0,
// 补算时是 min(10, 补算开始时的值)。下限永远不高于当前值,所以不会把数抬高。
static void step_minute(pupu_model_t *m, uint8_t full_floor, uint8_t happy_floor) {
    m->life_min++;
    if (m->cookie_win_min > 0 && --m->cookie_win_min == 0) m->cookie_count = 0;
    if (m->pat_win_min > 0 && --m->pat_win_min == 0) m->pat_count = 0;
    if (m->stage == PUPU_STAGE_EGG) return;

    if (m->life_min % PUPU_FULL_DECAY_EVERY_MIN == 0 && m->full > full_floor) {
        m->full--;
    }
    if (m->life_min % PUPU_HAPPY_DECAY_EVERY_MIN == 0) {
        int step = m->full < PUPU_STARVING_BELOW ? PUPU_HAPPY_DECAY_STARVING : PUPU_HAPPY_DECAY;
        m->happy = m->happy > happy_floor + step ? (uint8_t)(m->happy - step) : happy_floor;
    }
}

static void advance_life(pupu_model_t *m, uint32_t pet_s, uint8_t full_floor, uint8_t happy_floor) {
    uint64_t total = (uint64_t)m->life_sec + pet_s;
    uint64_t minutes = total / 60;
    m->life_sec = (uint8_t)(total % 60);
    while (minutes-- > 0) step_minute(m, full_floor, happy_floor);
}

// 宠物日按 24 小时宠物时间滚动,和日历日期无关。返回跨过了几天。
static uint32_t advance_day(pupu_model_t *m, uint64_t pet_s) {
    uint64_t total = (uint64_t)m->day_s + pet_s;
    uint32_t days = (uint32_t)(total / PUPU_DAY_S);
    m->day_s = (uint32_t)(total % PUPU_DAY_S);
    if (days > 0) {
        m->bond_today = 0;
        m->candy_today = 0;
        m->synced_today = false;
    }
    return days;
}

void pupu_model_init(pupu_model_t *m) {
    memset(m, 0, sizeof(*m));
    m->stage = PUPU_STAGE_EGG;
    m->full = PUPU_EGG_FULL;
    m->happy = PUPU_EGG_HAPPY;
}

bool pupu_model_hatch(pupu_model_t *m) {
    if (m->stage != PUPU_STAGE_EGG) return false;
    m->stage = PUPU_STAGE_CHILD;
    return true;
}

void pupu_model_advance(pupu_model_t *m, uint32_t pet_s) {
    advance_day(m, pet_s);
    advance_life(m, pet_s, 0, 0);
}

pupu_catch_up_t pupu_model_catch_up(pupu_model_t *m, uint64_t away_pet_s) {
    pupu_catch_up_t out;
    out.applied_s = away_pet_s > PUPU_CATCH_UP_MAX_S ? PUPU_CATCH_UP_MAX_S : (uint32_t)away_pet_s;
    uint8_t full_floor = m->full < PUPU_CATCH_UP_FLOOR ? m->full : PUPU_CATCH_UP_FLOOR;
    uint8_t happy_floor = m->happy < PUPU_CATCH_UP_FLOOR ? m->happy : PUPU_CATCH_UP_FLOOR;
    advance_life(m, out.applied_s, full_floor, happy_floor);
    out.days = advance_day(m, away_pet_s);
    return out;
}

pupu_outcome_t pupu_model_feed(pupu_model_t *m, pupu_food_t food, uint32_t acct_now) {
    pupu_outcome_t out = { .result = PUPU_OK };
    if (m->stage == PUPU_STAGE_EGG) {
        out.result = PUPU_NOT_HATCHED;
        return out;
    }
    if (food == PUPU_FOOD_CANDY && m->candy == 0) {
        out.result = PUPU_NO_CANDY;
        return out;
    }
    // 拒食时什么都不变:不扣星星糖,不算零食次数,也不开窗口。
    if (m->full >= PUPU_REFUSE_FULL_AT) {
        out.result = PUPU_REFUSED;
        return out;
    }

    switch (food) {
    case PUPU_FOOD_RICE: {
        bool hungry = m->full < PUPU_HUNGRY_BELOW;
        out.full = add_stat(&m->full, PUPU_RICE_FULL);
        if (hungry) out.bond = add_bond(m, PUPU_RICE_HUNGRY_BOND);
        break;
    }
    case PUPU_FOOD_COOKIE:
        out.full = add_stat(&m->full, PUPU_COOKIE_FULL);
        // 只有第一块饼干开窗口;窗口内后面的饼干不延长它。
        if (m->cookie_win_min == 0) m->cookie_win_min = PUPU_COOKIE_WINDOW_MIN;
        if (m->cookie_count < PUPU_COOKIE_HAPPY_LIMIT) {
            out.happy = add_stat(&m->happy, PUPU_COOKIE_HAPPY);
        } else {
            out.result = PUPU_TIRED;
        }
        m->cookie_count++;
        break;
    case PUPU_FOOD_CANDY:
        m->candy--;
        out.candy = -1;
        out.happy = add_stat(&m->happy, PUPU_CANDY_HAPPY);
        out.bond = add_bond(m, PUPU_CANDY_BOND);
        break;
    }

    m->has_fed = true;
    m->fed_life_min = m->life_min;
    m->fed_acct = acct_now;
    out.grew = check_grow(m);
    return out;
}

pupu_outcome_t pupu_model_pat(pupu_model_t *m) {
    pupu_outcome_t out = { .result = PUPU_OK };
    if (m->stage == PUPU_STAGE_EGG) {
        out.result = PUPU_NOT_HATCHED;
        return out;
    }
    // 躲开的那一次不计数,也不延长窗口。
    if (m->pat_win_min > 0 && m->pat_count >= PUPU_PAT_LIMIT) {
        out.result = PUPU_DODGED;
        return out;
    }
    if (m->pat_win_min == 0) m->pat_win_min = PUPU_PAT_WINDOW_MIN;
    m->pat_count++;
    out.happy = add_stat(&m->happy, PUPU_PAT_HAPPY);
    out.bond = add_bond(m, PUPU_PAT_BOND);
    out.grew = check_grow(m);
    return out;
}

pupu_outcome_t pupu_model_apply_catch_result(pupu_model_t *m, bool completed, unsigned stars) {
    pupu_outcome_t out = { .result = PUPU_OK };
    if (m->stage == PUPU_STAGE_EGG) {
        out.result = PUPU_NOT_HATCHED;
        return out;
    }
    if (!completed) {
        out.result = PUPU_ABORTED;
        return out;
    }
    out.full = add_stat(&m->full, -PUPU_CATCH_FULL_COST);
    if (stars < PUPU_CATCH_WIN_STARS) {
        out.result = PUPU_LOST;
        return out;
    }
    out.happy = add_stat(&m->happy, PUPU_CATCH_HAPPY);
    out.bond = add_bond(m, PUPU_CATCH_BOND);
    if (m->candy_today < PUPU_CANDY_PER_DAY && m->candy < PUPU_CANDY_MAX) {
        m->candy++;
        m->candy_today++;
        out.candy = 1;
    }
    out.grew = check_grow(m);
    return out;
}

pupu_outcome_t pupu_model_apply_sync(pupu_model_t *m) {
    pupu_outcome_t out = { .result = PUPU_OK };
    // 蛋阶段数值不变,所以也不领同步奖励,留到孵化后的第一次同步。
    if (m->stage == PUPU_STAGE_EGG) {
        out.result = PUPU_NOT_HATCHED;
        return out;
    }
    if (m->synced_today) return out;
    m->synced_today = true;
    out.bond = add_bond(m, PUPU_SYNC_BOND);
    out.grew = check_grow(m);
    return out;
}
