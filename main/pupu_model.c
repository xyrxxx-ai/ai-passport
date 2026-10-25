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

// 零花钱 0–99 封顶。返回实际变了多少。
static int8_t add_coin(pupu_model_t *m, int delta) {
    int next = m->coin + delta;
    if (next < 0) next = 0;
    if (next > PUPU_COIN_MAX) next = PUPU_COIN_MAX;
    int8_t applied = (int8_t)(next - m->coin);
    m->coin = (uint8_t)next;
    return applied;
}

// 占掉当天的亲密额度(不加亲密)。返回实际占掉多少。
static uint8_t take_bond_budget(pupu_model_t *m, uint8_t want) {
    uint8_t room = (uint8_t)(PUPU_BOND_PER_DAY - m->bond_today);
    uint8_t taken = want < room ? want : room;
    m->bond_today = (uint8_t)(m->bond_today + taken);
    return taken;
}

// 所有来源的亲密都走这里,统一套每日上限。返回实际加了多少。
static uint8_t add_bond(pupu_model_t *m, uint8_t want) {
    uint8_t gain = take_bond_budget(m, want);
    m->bond = (uint16_t)(m->bond + gain);
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
    if (m->emo_min > 0) m->emo_min--;  // 蛋阶段也走:蛋不会 emo,没有影响
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
// emo 不按天清:它是倒计时,见 step_minute()。
static uint32_t advance_day(pupu_model_t *m, uint64_t pet_s) {
    uint64_t total = (uint64_t)m->day_s + pet_s;
    uint32_t days = (uint32_t)(total / PUPU_DAY_S);
    m->day_s = (uint32_t)(total % PUPU_DAY_S);
    if (days > 0) {
        m->bond_today = 0;
        m->candy_today = 0;
        m->synced_today = false;
        m->work_today = 0;
    }
    return days;
}

void pupu_model_init(pupu_model_t *m) {
    memset(m, 0, sizeof(*m));
    m->stage = PUPU_STAGE_EGG;
    m->full = PUPU_EGG_FULL;
    m->happy = PUPU_EGG_HAPPY;
    m->coin = PUPU_COIN_START;
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

static uint8_t food_price(pupu_food_t food) {
    switch (food) {
    case PUPU_FOOD_RICE: return PUPU_RICE_PRICE;
    case PUPU_FOOD_COOKIE: return PUPU_COOKIE_PRICE;
    default: return 0;  // 星星糖免费
    }
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
    const bool emo = m->emo_min > 0;
    // emo 中没胃口吃饼干:不扣钱、不计数、不开窗口。
    if (emo && food == PUPU_FOOD_COOKIE) {
        out.result = PUPU_SULKING;
        return out;
    }
    // 拒食时什么都不变:不扣钱,不扣星星糖,不算零食次数,也不开窗口。
    // emo 中的星星糖例外:它是解除 emo 的唯一快捷办法,太饱也照吃。
    if (m->full >= PUPU_REFUSE_FULL_AT && !(emo && food == PUPU_FOOD_CANDY)) {
        out.result = PUPU_REFUSED;
        return out;
    }
    const uint8_t price = food_price(food);
    if (m->coin < price) {
        out.result = PUPU_NO_COIN;
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
            out.result = PUPU_TIRED;  // 吃腻了:饱腹照加,钱照扣
        }
        m->cookie_count++;
        break;
    case PUPU_FOOD_CANDY:
        m->candy--;
        out.candy = -1;
        out.happy = add_stat(&m->happy, PUPU_CANDY_HAPPY);
        out.bond = add_bond(m, PUPU_CANDY_BOND);
        if (emo) {
            m->emo_min = 0;
            out.emo_cleared = true;
        }
        break;
    }
    out.coin = add_coin(m, -(int)price);  // 价格按份算:加到 100 封顶也照扣

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
    // emo 中不想理你:不计数,也不开窗口。
    if (m->emo_min > 0) {
        out.result = PUPU_SULKING;
        return out;
    }
    // 计数饱和以后(第 6 次起)只转身:不计数,也不延长窗口。
    if (m->pat_count >= PUPU_PAT_COUNT_MAX) {
        out.result = PUPU_DODGED;
        return out;
    }
    if (m->pat_win_min == 0) m->pat_win_min = PUPU_PAT_WINDOW_MIN;  // 第 1 次开窗
    m->pat_count++;
    if (m->pat_count <= PUPU_PAT_LIMIT) {
        out.happy = add_stat(&m->happy, PUPU_PAT_HAPPY);
        out.bond = add_bond(m, PUPU_PAT_BOND);
        out.grew = check_grow(m);
    } else if (m->pat_count == PUPU_PAT_ANGRY_AT) {
        out.result = PUPU_ANGRY;
        out.happy = add_stat(&m->happy, -PUPU_PAT_ANGRY_HAPPY);
    } else {
        out.result = PUPU_ANNOYED;  // 第 4 次:不耐烦,什么都不加
    }
    return out;
}

pupu_outcome_t pupu_model_work(pupu_model_t *m) {
    pupu_outcome_t out = { .result = PUPU_OK };
    if (m->stage == PUPU_STAGE_EGG) {
        out.result = PUPU_NOT_HATCHED;
        return out;
    }
    if (m->work_today >= PUPU_WORK_PER_DAY) {
        out.result = PUPU_EXHAUSTED;  // 做不动了:什么都不变
        return out;
    }
    m->work_today++;  // 第几次只看 work_today,不看 emo_min
    if (m->work_today <= PUPU_WORK_EMO_AT) {
        out.coin = add_coin(m, PUPU_WORK_COIN);
        out.happy = add_stat(&m->happy, -PUPU_WORK_HAPPY_COST);
        out.bond_budget = -(int8_t)take_bond_budget(m, PUPU_WORK_BOND_BUDGET);
        if (m->work_today == PUPU_WORK_EMO_AT) {
            m->emo_min = PUPU_EMO_MIN;  // 已经在 emo 中也重置成 4 小时
            out.result = PUPU_WENT_EMO;
        }
        return out;
    }
    // 第 4、5 次:emo 中做错表了,收益减半,不占额度
    out.result = PUPU_OVERWORKED;
    out.coin = add_coin(m, PUPU_WORK_EMO_COIN);
    out.happy = add_stat(&m->happy, -PUPU_WORK_EMO_HAPPY_COST);
    return out;
}

// 完成一局以后的奖励:赢了加开心、亲密,再给一颗星星糖(每天最多 3 颗,库存 99 封顶)。
static void win_reward(pupu_model_t *m, pupu_outcome_t *out, int happy, uint8_t bond) {
    out->happy = add_stat(&m->happy, happy);
    out->bond = add_bond(m, bond);
    if (m->candy_today < PUPU_CANDY_PER_DAY && m->candy < PUPU_CANDY_MAX) {
        m->candy++;
        m->candy_today++;
        out->candy = 1;
    }
    out->grew = check_grow(m);
}

pupu_outcome_t pupu_model_apply_gate_result(pupu_model_t *m, bool completed, bool won) {
    pupu_outcome_t out = { .result = PUPU_OK };
    if (m->stage == PUPU_STAGE_EGG) {
        out.result = PUPU_NOT_HATCHED;
        return out;
    }
    if (!completed) {
        out.result = PUPU_ABORTED;
        return out;
    }
    out.full = add_stat(&m->full, -PUPU_GATE_FULL_COST);
    if (!won) {
        out.result = PUPU_LOST;
        return out;
    }
    win_reward(m, &out, PUPU_GATE_HAPPY, PUPU_GATE_BOND);
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
