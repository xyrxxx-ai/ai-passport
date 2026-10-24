#include <assert.h>
#include <stdbool.h>

#include "pupu_config.h"
#include "pupu_model.h"

#define MIN_S 60u
#define HOUR_S 3600u

static pupu_model_t hatched(void) {
    pupu_model_t m;
    pupu_model_init(&m);
    bool did_hatch = pupu_model_hatch(&m);
    assert(did_hatch);
    return m;
}

// 逐字段比较(结构体有填充字节,不能用 memcmp)。
static bool same_model(const pupu_model_t *a, const pupu_model_t *b) {
    return a->stage == b->stage && a->full == b->full && a->happy == b->happy
        && a->bond == b->bond && a->candy == b->candy
        && a->life_min == b->life_min && a->life_sec == b->life_sec && a->day_s == b->day_s
        && a->bond_today == b->bond_today && a->candy_today == b->candy_today
        && a->synced_today == b->synced_today
        && a->cookie_count == b->cookie_count && a->cookie_win_min == b->cookie_win_min
        && a->pat_count == b->pat_count && a->pat_win_min == b->pat_win_min
        && a->has_fed == b->has_fed && a->fed_life_min == b->fed_life_min
        && a->fed_acct == b->fed_acct;
}

static void test_new_egg(void) {
    pupu_model_t m;
    pupu_model_init(&m);
    assert(m.stage == PUPU_STAGE_EGG);
    assert(m.full == 80 && m.happy == 80 && m.bond == 0 && m.candy == 0);

    // 蛋阶段不衰减,也不能喂、摸、玩、领同步奖励。
    pupu_model_advance(&m, 10 * HOUR_S);
    pupu_model_catch_up(&m, 8 * HOUR_S);
    assert(m.full == 80 && m.happy == 80);
    pupu_model_t before = m;
    assert(pupu_model_feed(&m, PUPU_FOOD_RICE, 0).result == PUPU_NOT_HATCHED);
    assert(pupu_model_pat(&m).result == PUPU_NOT_HATCHED);
    assert(pupu_model_apply_catch_result(&m, true, 10).result == PUPU_NOT_HATCHED);
    assert(pupu_model_apply_sync(&m).result == PUPU_NOT_HATCHED);
    assert(same_model(&m, &before));

    bool did_hatch = pupu_model_hatch(&m);
    assert(did_hatch);
    assert(m.stage == PUPU_STAGE_CHILD);
    assert(m.full == 80 && m.happy == 80);
    did_hatch = pupu_model_hatch(&m);
    assert(!did_hatch);
    assert(m.stage == PUPU_STAGE_CHILD);
}

static void test_hourly_decay(void) {
    pupu_model_t m = hatched();
    pupu_model_advance(&m, HOUR_S);
    assert(m.full == 70);   // 每小时 -10
    assert(m.happy == 72);  // 每小时 -8
    pupu_model_advance(&m, HOUR_S);
    assert(m.full == 60 && m.happy == 64);

    // 很饿(饱腹 < 15)时开心每小时 -16。
    m = hatched();
    m.full = 14;
    m.happy = 80;
    pupu_model_advance(&m, HOUR_S);
    assert(m.full == 4);
    assert(m.happy == 64);

    // 边界:饱腹正好 15 不算很饿,14 才算。第 15 分钟只有开心衰减。
    m = hatched();
    m.life_min = 14;
    m.full = 15;
    m.happy = 50;
    pupu_model_advance(&m, MIN_S);
    assert(m.full == 15 && m.happy == 48);
    m = hatched();
    m.life_min = 14;
    m.full = 14;
    m.happy = 50;
    pupu_model_advance(&m, MIN_S);
    assert(m.happy == 46);

    // 在线推进可以降到 0,不会倒卷。
    m = hatched();
    m.full = 3;
    m.happy = 3;
    pupu_model_advance(&m, 5 * HOUR_S);
    assert(m.full == 0 && m.happy == 0);
}

static void test_seconds_carry(void) {
    pupu_model_t m = hatched();
    pupu_model_advance(&m, 30);
    assert(m.life_min == 0 && m.life_sec == 30);
    pupu_model_advance(&m, 29);
    assert(m.life_min == 0 && m.life_sec == 59);
    pupu_model_advance(&m, 1);
    assert(m.life_min == 1 && m.life_sec == 0);
    pupu_model_advance(&m, 125);
    assert(m.life_min == 3 && m.life_sec == 5);
    assert(m.day_s == 185);
}

static void test_catch_up(void) {
    // 最多补 8 小时。
    pupu_model_t m = hatched();
    m.full = 100;
    m.happy = 100;
    pupu_catch_up_t r = pupu_model_catch_up(&m, 10 * HOUR_S);
    assert(r.applied_s == 8 * HOUR_S);
    assert(r.days == 0);
    assert(m.life_min == 8 * 60);
    assert(m.full == 20);   // 100 - 8 × 10,不是 10 小时的 100 - 100
    assert(m.happy == 36);  // 100 - 8 × 8
    assert(m.day_s == 10 * HOUR_S);  // 天数按完整的离开时长走

    // 补算部分最低只降到 10。
    m = hatched();
    m.full = 30;
    m.happy = 30;
    pupu_model_catch_up(&m, 8 * HOUR_S);
    assert(m.full == 10 && m.happy == 10);

    // 本来就低于 10 的,补算后不变,也不会被抬高到 10。
    m = hatched();
    m.full = 5;
    m.happy = 7;
    pupu_model_catch_up(&m, 8 * HOUR_S);
    assert(m.full == 5 && m.happy == 7);
    // 下限只管补算;回到在线以后照常降到 0。
    pupu_model_advance(&m, 2 * HOUR_S);
    assert(m.full == 0 && m.happy == 0);

    // 补算不满 8 小时时按实际时长,和在线推进结果一样。
    pupu_model_t live = hatched();
    m = hatched();
    pupu_model_advance(&live, 3 * HOUR_S);
    r = pupu_model_catch_up(&m, 3 * HOUR_S);
    assert(r.applied_s == 3 * HOUR_S);
    assert(same_model(&m, &live));

    // 补算期间零食和摸摸的窗口也会过期。
    m = hatched();
    m.full = 20;
    pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0);
    pupu_model_pat(&m);
    assert(m.cookie_win_min == 60 && m.pat_win_min == 10);
    pupu_model_catch_up(&m, 2 * HOUR_S);
    assert(m.cookie_win_min == 0 && m.cookie_count == 0);
    assert(m.pat_win_min == 0 && m.pat_count == 0);
}

static void test_pet_day(void) {
    // 补算过程中跨过宠物日,每日计数清零。
    pupu_model_t m = hatched();
    m.day_s = PUPU_DAY_S - MIN_S;
    m.bond_today = 20;
    m.candy_today = 3;
    m.synced_today = true;
    pupu_catch_up_t r = pupu_model_catch_up(&m, 2 * MIN_S);
    assert(r.days == 1);
    assert(m.day_s == MIN_S);
    assert(m.bond_today == 0 && m.candy_today == 0 && !m.synced_today);

    // 离开 3 天:数值只补 8 小时,但天数前进 3 天。
    m = hatched();
    m.day_s = 1000;
    m.bond_today = 5;
    r = pupu_model_catch_up(&m, 3 * PUPU_DAY_S);
    assert(r.applied_s == 8 * HOUR_S);
    assert(r.days == 3);
    assert(m.day_s == 1000);
    assert(m.life_min == 8 * 60);
    assert(m.bond_today == 0);

    // 在线推进同样会跨日。
    m = hatched();
    m.bond_today = 20;
    pupu_model_advance(&m, PUPU_DAY_S - 1);
    assert(m.bond_today == 20);
    pupu_model_advance(&m, 1);
    assert(m.bond_today == 0 && m.day_s == 0);
}

static void test_feeding(void) {
    // 饿时(饱腹 < 40)喂饭团,亲密 +3;不饿时不加。
    pupu_model_t m = hatched();
    m.full = 39;
    pupu_outcome_t o = pupu_model_feed(&m, PUPU_FOOD_RICE, 0);
    assert(o.result == PUPU_OK && o.full == 35 && o.bond == 3);
    assert(m.full == 74 && m.bond == 3);
    m.full = 40;
    o = pupu_model_feed(&m, PUPU_FOOD_RICE, 0);
    assert(o.result == PUPU_OK && o.bond == 0 && m.full == 75);

    // 加到 100 封顶,结果里写实际加了多少。
    m.full = 89;
    o = pupu_model_feed(&m, PUPU_FOOD_RICE, 0);
    assert(o.full == 11 && m.full == 100);

    // 饱腹 >= 90 时三种食物都拒绝,什么都不变。
    m = hatched();
    m.full = 90;
    m.candy = 2;
    pupu_model_t before = m;
    assert(pupu_model_feed(&m, PUPU_FOOD_RICE, 0).result == PUPU_REFUSED);
    assert(pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0).result == PUPU_REFUSED);
    assert(pupu_model_feed(&m, PUPU_FOOD_CANDY, 0).result == PUPU_REFUSED);
    assert(same_model(&m, &before));
    m.full = 89;
    assert(pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0).result == PUPU_OK);

    // 星星糖:开心 +25、亲密 +2,用掉一颗;没有星星糖时不能喂。
    m = hatched();
    m.happy = 50;
    m.candy = 1;
    o = pupu_model_feed(&m, PUPU_FOOD_CANDY, 0);
    assert(o.result == PUPU_OK && o.happy == 25 && o.bond == 2 && o.candy == -1);
    assert(m.candy == 0 && m.happy == 75 && m.full == 80);
    before = m;
    assert(pupu_model_feed(&m, PUPU_FOOD_CANDY, 0).result == PUPU_NO_CANDY);
    assert(same_model(&m, &before));

    // 记录上次喂食;被拒绝的不算。
    m = hatched();
    pupu_model_advance(&m, 5 * MIN_S);
    assert(!m.has_fed);
    pupu_model_feed(&m, PUPU_FOOD_RICE, 1800000000u);
    assert(m.has_fed && m.fed_life_min == 5 && m.fed_acct == 1800000000u);
    m.full = 95;
    pupu_model_advance(&m, 5 * MIN_S);
    pupu_model_feed(&m, PUPU_FOOD_RICE, 1800000600u);
    assert(m.fed_life_min == 5 && m.fed_acct == 1800000000u);
}

static void test_cookie_window(void) {
    pupu_model_t m = hatched();
    m.full = 20;
    m.happy = 50;
    for (int i = 0; i < 3; i++) {
        pupu_outcome_t o = pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0);
        assert(o.result == PUPU_OK && o.full == 10 && o.happy == 5);
    }
    assert(m.cookie_win_min == 60 && m.cookie_count == 3);
    assert(m.full == 50 && m.happy == 65);

    // 窗口第 59 分钟还在窗口里:吃腻了,饱腹照加,开心不加,也不延长窗口。
    pupu_model_advance(&m, 59 * MIN_S);
    assert(m.cookie_win_min == 1);
    uint8_t happy = m.happy;
    pupu_outcome_t o = pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0);
    assert(o.result == PUPU_TIRED && o.full == 10 && o.happy == 0);
    assert(m.happy == happy && m.cookie_win_min == 1);

    // 第 60 分钟窗口结束,重新计数,下一块饼干又加开心并开新窗口。
    pupu_model_advance(&m, MIN_S);
    assert(m.cookie_win_min == 0 && m.cookie_count == 0);
    o = pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0);
    assert(o.result == PUPU_OK && o.happy == 5);
    assert(m.cookie_win_min == 60 && m.cookie_count == 1);

    // 被拒绝的饼干不计数,也不开窗口。
    m = hatched();
    m.full = 95;
    pupu_model_feed(&m, PUPU_FOOD_COOKIE, 0);
    assert(m.cookie_win_min == 0 && m.cookie_count == 0);
}

static void test_pat_window(void) {
    pupu_model_t m = hatched();
    m.happy = 50;
    for (int i = 0; i < 3; i++) {
        pupu_outcome_t o = pupu_model_pat(&m);
        assert(o.result == PUPU_OK && o.happy == 5 && o.bond == 1);
    }
    assert(m.pat_win_min == 10 && m.pat_count == 3 && m.bond == 3);

    // 第 4 次起躲开:什么都不加,不计数,也不延长窗口。
    pupu_model_t before = m;
    assert(pupu_model_pat(&m).result == PUPU_DODGED);
    assert(same_model(&m, &before));

    pupu_model_advance(&m, 9 * MIN_S);
    assert(pupu_model_pat(&m).result == PUPU_DODGED);
    assert(m.pat_win_min == 1);

    // 10 分钟到了,窗口结束,又可以摸。
    pupu_model_advance(&m, MIN_S);
    assert(m.pat_win_min == 0 && m.pat_count == 0);
    assert(pupu_model_pat(&m).result == PUPU_OK);
    assert(m.pat_win_min == 10 && m.pat_count == 1);
}

static void test_daily_bond_cap(void) {
    // 一天里反复摸摸,亲密最多 +20;第二天又能加。
    pupu_model_t m = hatched();
    for (int i = 0; i < 40; i++) {
        pupu_model_pat(&m);
        pupu_model_advance(&m, 10 * MIN_S);
    }
    assert(m.bond == 20 && m.bond_today == 20);
    // 超过上限后摸摸仍然加开心,只是亲密不加。
    m.happy = 50;
    m.pat_win_min = 0;
    m.pat_count = 0;
    pupu_outcome_t o = pupu_model_pat(&m);
    assert(o.result == PUPU_OK && o.happy == 5 && o.bond == 0);

    // 不同来源合起来算,超出部分截掉。
    m = hatched();
    m.bond_today = 18;
    m.candy = 1;
    o = pupu_model_feed(&m, PUPU_FOOD_CANDY, 0);
    assert(o.bond == 2 && m.bond_today == 20);
    m = hatched();
    m.bond_today = 19;
    m.full = 10;
    o = pupu_model_feed(&m, PUPU_FOOD_RICE, 0);
    assert(o.bond == 1 && m.bond == 1 && m.bond_today == 20);

    m.day_s = PUPU_DAY_S - 1;
    pupu_model_advance(&m, 1);
    o = pupu_model_pat(&m);
    assert(o.bond == 1 && m.bond_today == 1);
}

static void test_catch_result(void) {
    // 中途退出:没有任何影响。
    pupu_model_t m = hatched();
    pupu_model_t before = m;
    assert(pupu_model_apply_catch_result(&m, false, 12).result == PUPU_ABORTED);
    assert(same_model(&m, &before));

    // 完成但不到 8 颗:只扣饱腹 5。
    pupu_outcome_t o = pupu_model_apply_catch_result(&m, true, 7);
    assert(o.result == PUPU_LOST && o.full == -5 && o.happy == 0 && o.bond == 0 && o.candy == 0);
    assert(m.full == 75 && m.happy == 80 && m.candy == 0);

    // 接到 8 颗:饱腹 -5,开心 +15,亲密 +3,星星糖 +1。
    m = hatched();
    m.happy = 50;
    o = pupu_model_apply_catch_result(&m, true, 8);
    assert(o.result == PUPU_OK && o.full == -5 && o.happy == 15 && o.bond == 3 && o.candy == 1);
    assert(m.full == 75 && m.happy == 65 && m.bond == 3 && m.candy == 1 && m.candy_today == 1);

    // 饱腹最低扣到 0。
    m = hatched();
    m.full = 3;
    o = pupu_model_apply_catch_result(&m, true, 0);
    assert(o.full == -3 && m.full == 0);

    // 每天最多得 3 颗星星糖;第 4 次赢照样加开心和亲密。
    m = hatched();
    m.happy = 0;
    for (int i = 0; i < 3; i++) {
        o = pupu_model_apply_catch_result(&m, true, 9);
        assert(o.candy == 1);
    }
    o = pupu_model_apply_catch_result(&m, true, 9);
    assert(o.result == PUPU_OK && o.candy == 0 && o.happy == 15 && o.bond == 3);
    assert(m.candy == 3 && m.candy_today == 3 && m.bond == 12);
    // 新的一天又能得。
    m.day_s = PUPU_DAY_S - 1;
    pupu_model_advance(&m, 1);
    o = pupu_model_apply_catch_result(&m, true, 9);
    assert(o.candy == 1 && m.candy == 4);

    // 星星糖最多存 99 颗,存满时不再给,也不占当天的名额。
    m = hatched();
    m.candy = 99;
    o = pupu_model_apply_catch_result(&m, true, 8);
    assert(o.result == PUPU_OK && o.candy == 0);
    assert(m.candy == 99 && m.candy_today == 0);
}

static void test_sync_bonus(void) {
    pupu_model_t m = hatched();
    pupu_outcome_t o = pupu_model_apply_sync(&m);
    assert(o.result == PUPU_OK && o.bond == 2 && m.synced_today);
    // 同一天再同步不加。
    o = pupu_model_apply_sync(&m);
    assert(o.result == PUPU_OK && o.bond == 0 && m.bond == 2);
    // 跨日后又能加。
    pupu_model_advance(&m, PUPU_DAY_S);
    o = pupu_model_apply_sync(&m);
    assert(o.bond == 2 && m.bond == 4);
    // 当天亲密已满 20 时只记"已同步",不加亲密。
    pupu_model_advance(&m, PUPU_DAY_S);
    m.bond_today = 20;
    o = pupu_model_apply_sync(&m);
    assert(o.bond == 0 && m.synced_today);
}

static void test_growth(void) {
    // 亲密满 100 长成成年,任何来源都会触发,只触发一次。
    pupu_model_t m = hatched();
    m.bond = 99;
    pupu_outcome_t o = pupu_model_pat(&m);
    assert(o.grew && m.stage == PUPU_STAGE_ADULT && m.bond == 100);
    o = pupu_model_pat(&m);
    assert(!o.grew && m.stage == PUPU_STAGE_ADULT && m.bond == 101);

    m = hatched();
    m.bond = 98;
    o = pupu_model_apply_sync(&m);
    assert(o.grew && m.stage == PUPU_STAGE_ADULT);

    m = hatched();
    m.bond = 97;
    o = pupu_model_apply_catch_result(&m, true, 8);
    assert(o.grew && m.stage == PUPU_STAGE_ADULT);

    // 当天亲密已满时不会因为别的来源越过 100。
    m = hatched();
    m.bond = 99;
    m.bond_today = 20;
    o = pupu_model_pat(&m);
    assert(!o.grew && m.stage == PUPU_STAGE_CHILD && m.bond == 99);
}

int main(void) {
    test_new_egg();
    test_hourly_decay();
    test_seconds_carry();
    test_catch_up();
    test_pet_day();
    test_feeding();
    test_cookie_window();
    test_pat_window();
    test_daily_bond_cap();
    test_catch_result();
    test_sync_bonus();
    test_growth();
    return 0;
}
