#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "pupu_clock.h"
#include "pupu_config.h"
#include "pupu_model.h"
#include "pupu_save_codec.h"

#define US 1000000ull
#define MIN_S 60u
#define HOUR_S 3600u
#define T0 1798761600u  // 2027-01-01 00:00:00 UTC,测试里的"现实世界"起点

static void test_scale_and_remainder(void) {
    pupu_clock_t c;
    pupu_clock_init(&c, 1, 0);
    assert(pupu_clock_tick(&c, 700000) == 0);
    assert(pupu_clock_tick(&c, 700000) == 1);  // 0.7 + 0.7 秒,进位 1 秒,余 0.4 秒
    assert(c.real_rem_us == 400000);
    assert(pupu_clock_tick(&c, 600000) == 1);
    assert(c.real_rem_us == 0);

    // 倍率 60:真实 1 秒 = 宠物 60 秒。50 ms 一次,20 次才满 1 秒。
    pupu_clock_init(&c, 60, 0);
    uint32_t pet = 0;
    for (int i = 0; i < 19; i++) pet += pupu_clock_tick(&c, 50000);
    assert(pet == 0);
    pet += pupu_clock_tick(&c, 50000);
    assert(pet == 60);
    assert(pupu_clock_tick(&c, 1500000) == 60);
    assert(pupu_clock_tick(&c, 500000) == 60);
    assert(pupu_clock_to_pet(&c, 480) == 8 * HOUR_S);

    // 没同步过时 acct_now 一直是 0;同步过以后按真实秒前进,不乘倍率。
    assert(c.acct_now == 0);
    pupu_clock_init(&c, 60, T0);
    assert(pupu_clock_tick(&c, 2500000) == 120);
    assert(c.acct_now == T0 + 2);
    assert(pupu_clock_tick(&c, 500000) == 60);
    assert(c.acct_now == T0 + 3);
}

static void test_reset_reasons(void) {
    assert(!pupu_clock_reset_keeps_rtc(PUPU_RESET_POWERON));
    assert(!pupu_clock_reset_keeps_rtc(PUPU_RESET_BROWNOUT));
    assert(!pupu_clock_reset_keeps_rtc(PUPU_RESET_UNKNOWN));
    const pupu_reset_reason_t kept[] = {
        PUPU_RESET_DEEPSLEEP, PUPU_RESET_USB, PUPU_RESET_SW, PUPU_RESET_PANIC,
        PUPU_RESET_INT_WDT, PUPU_RESET_TASK_WDT, PUPU_RESET_WDT, PUPU_RESET_CPU_LOCKUP,
        PUPU_RESET_EXT, PUPU_RESET_PWR_GLITCH,
    };
    for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++) {
        assert(pupu_clock_reset_keeps_rtc(kept[i]));
    }
}

static void test_boot_checks(void) {
    pupu_clock_t c;
    uint64_t away = 99;
    pupu_boot_info_t b = {
        .reason = PUPU_RESET_DEEPSLEEP,
        .saved_token = 0xA5A5,
        .rtc_token = 0xA5A5,
        .saved_rtc_us = 10 * US,
        .now_rtc_us = 10 * US + 2 * HOUR_S * US + 999999,
    };
    pupu_clock_init(&c, 1, T0);
    assert(pupu_clock_boot(&c, &b, &away));
    assert(away == 2 * HOUR_S);  // 不满 1 秒的部分丢掉
    assert(c.acct_now == T0 + 2 * HOUR_S);

    // 没同步过也能按 RTC 补算,只是 acct_now 保持 0。
    pupu_clock_init(&c, 1, 0);
    assert(pupu_clock_boot(&c, &b, &away) && away == 2 * HOUR_S);
    assert(c.acct_now == 0);

    // 令牌对不上(断过电,RTC 内存是乱码):当作断过电。
    pupu_clock_init(&c, 1, T0);
    b.rtc_token = 0x1234;
    assert(!pupu_clock_boot(&c, &b, &away));
    assert(away == 0 && c.acct_now == T0);

    // RTC 倒退:当作断过电。
    b.rtc_token = b.saved_token;
    b.now_rtc_us = b.saved_rtc_us - 1;
    assert(!pupu_clock_boot(&c, &b, &away));
    assert(away == 0 && c.acct_now == T0);

    // 开机原因是上电:哪怕令牌碰巧一致也不信 RTC。
    b.now_rtc_us = b.saved_rtc_us + HOUR_S * US;
    b.reason = PUPU_RESET_POWERON;
    assert(!pupu_clock_boot(&c, &b, &away));

    // 令牌 0(M3 的存档都写 0):哪怕 RTC 内存碰巧也是 0,也当作断过电。
    b.reason = PUPU_RESET_USB;
    b.saved_token = 0;
    b.rtc_token = 0;
    assert(!pupu_clock_boot(&c, &b, &away));
    assert(away == 0 && c.acct_now == T0);
}

static void test_sync_rules(void) {
    pupu_clock_t c;
    uint64_t away = 99;
    pupu_clock_init(&c, 1, 0);

    // 早于 2026-01-01 的时间拒绝,什么都不改。
    assert(!pupu_clock_sync(&c, PUPU_EPOCH_MIN - 1, &away));
    // 晚于 2100-01-01 也拒绝:acct_now 是 u32,再走几年会回绕成 0("从没同步过")
    assert(!pupu_clock_sync(&c, PUPU_EPOCH_MAX + 1, &away));
    assert(!pupu_clock_sync(&c, 0xFFFFFFFFu, &away));
    assert(away == 0 && c.acct_now == 0);
    assert(!pupu_clock_sync(&c, 0, &away));

    // 第一次同步只对时,不补算。
    assert(pupu_clock_sync(&c, PUPU_EPOCH_MIN, &away));
    assert(away == 0 && c.acct_now == PUPU_EPOCH_MIN);

    // 之后按差值补算;同一个时间重复发(手机重试)不会重复补算。
    assert(pupu_clock_sync(&c, PUPU_EPOCH_MIN + 600, &away) && away == 600);
    assert(pupu_clock_sync(&c, PUPU_EPOCH_MIN + 600, &away) && away == 0);

    // 更早的时间不补算,但 acct_now 照样设成它。
    assert(pupu_clock_sync(&c, PUPU_EPOCH_MIN + 100, &away) && away == 0);
    assert(c.acct_now == PUPU_EPOCH_MIN + 100);
}

// ---- 模拟一台设备,走完整的"存档 → 关机或深睡 → 开机 → 同步"流程 ----

typedef struct {
    uint32_t scale;
    pupu_model_t m;
    pupu_clock_t c;
    uint8_t flash[PUPU_SAVE_RECORD_SIZE];  // 存档(NVS)
    uint32_t seq;
    uint64_t rtc_us;      // 芯片 RTC 计时器:深睡和普通重启时继续走,断电后从头开始
    uint64_t rtc_token;   // RTC_NOINIT 变量:断电后是乱码
    uint64_t token;       // 本次会话的令牌
    uint64_t next_token;
    uint32_t real_now;    // 现实世界的 Unix 秒,只有手机知道
    uint64_t caught_up_s; // 累计实际补算的宠物秒数
} sim_t;

static void sim_new(sim_t *s, uint32_t scale) {
    s->scale = scale;
    pupu_model_init(&s->m);
    pupu_model_hatch(&s->m);
    pupu_clock_init(&s->c, scale, 0);
    s->seq = 0;
    s->rtc_us = 3 * US;
    s->token = 0x1001;
    s->next_token = 0x2001;
    s->rtc_token = s->token;
    s->real_now = T0;
    s->caught_up_s = 0;
}

static void sim_online(sim_t *s, uint32_t real_s) {
    for (uint32_t i = 0; i < real_s * 2; i++) {
        pupu_model_advance(&s->m, pupu_clock_tick(&s->c, 500000));
    }
    s->rtc_us += (uint64_t)real_s * US;
    s->real_now += real_s;
}

static void sim_save(sim_t *s) {
    pupu_save_t save = {
        .model = s->m,
        .acct_epoch = s->c.acct_now,
        .rtc_us = s->rtc_us,
        .token = s->token,
    };
    size_t n = pupu_save_encode(&save, ++s->seq, s->flash, sizeof(s->flash));
    assert(n == sizeof(s->flash));
}

// 深睡、USB 重启、看门狗重启:RTC 继续走,RTC 内存保留。
static void sim_sleep(sim_t *s, uint32_t real_s) {
    s->rtc_us += (uint64_t)real_s * US;
    s->real_now += real_s;
}

// 按电源键关机:RTC 从头开始,RTC 内存变成乱码。
static void sim_power_off(sim_t *s, uint32_t real_s) {
    s->rtc_us = 2 * US;
    s->rtc_token = 0xDEADBEEFCAFEull;
    s->real_now += real_s;
}

static void sim_catch_up(sim_t *s, uint64_t away_real_s) {
    pupu_catch_up_t r = pupu_model_catch_up(&s->m, pupu_clock_to_pet(&s->c, away_real_s));
    s->caught_up_s += r.applied_s;
}

// 开机:读存档,做 RTC 连续性检查。返回是否按 RTC 补算了。
static bool sim_boot(sim_t *s, pupu_reset_reason_t reason) {
    pupu_save_t save;
    uint32_t seq = 0;
    assert(pupu_save_decode(s->flash, sizeof(s->flash), &save, &seq) == PUPU_SAVE_OK);
    assert(seq == s->seq);
    s->m = save.model;
    pupu_clock_init(&s->c, s->scale, save.acct_epoch);

    pupu_boot_info_t b = {
        .reason = reason,
        .saved_token = save.token,
        .rtc_token = s->rtc_token,
        .saved_rtc_us = save.rtc_us,
        .now_rtc_us = s->rtc_us,
    };
    uint64_t away = 0;
    bool continuous = pupu_clock_boot(&s->c, &b, &away);
    if (continuous) {
        sim_catch_up(s, away);
        s->token = save.token;
    } else {
        s->token = s->next_token++;
    }
    s->rtc_token = s->token;
    return continuous;
}

// 手机发来 TIME:先补算,再领首次同步奖励。
static bool sim_sync(sim_t *s, uint32_t phone_epoch) {
    uint64_t away = 0;
    if (!pupu_clock_sync(&s->c, phone_epoch, &away)) return false;
    sim_catch_up(s, away);
    pupu_model_apply_sync(&s->m);
    return true;
}

// 场景 1:深睡 4 小时 → 醒来 → 同步:总共只补 4 小时。
static void test_scenario_deep_sleep_then_sync(void) {
    sim_t s;
    sim_new(&s, 1);
    assert(sim_sync(&s, s.real_now));
    sim_online(&s, 10 * MIN_S);
    sim_save(&s);
    sim_sleep(&s, 4 * HOUR_S);
    assert(sim_boot(&s, PUPU_RESET_DEEPSLEEP));
    assert(s.caught_up_s == 4 * HOUR_S);
    assert(s.c.acct_now == s.real_now);

    assert(sim_sync(&s, s.real_now));
    assert(s.caught_up_s == 4 * HOUR_S);
    assert(s.m.life_min == 10 + 4 * 60);
}

// 场景 2:断电 → 开机后不同步、在线 30 分钟、存档 → 再断电 → 同步:
// 第一次开机的在线时间不会被算两次。
static void test_scenario_power_loss_twice(void) {
    sim_t s;
    sim_new(&s, 1);
    assert(sim_sync(&s, s.real_now));
    sim_save(&s);
    sim_power_off(&s, HOUR_S);
    assert(!sim_boot(&s, PUPU_RESET_POWERON));
    assert(s.caught_up_s == 0);

    sim_online(&s, 30 * MIN_S);
    sim_save(&s);
    sim_power_off(&s, 2 * HOUR_S);
    assert(!sim_boot(&s, PUPU_RESET_POWERON));

    assert(sim_sync(&s, s.real_now));
    assert(s.caught_up_s == 3 * HOUR_S);       // 两次关机,1 + 2 小时
    assert(s.m.life_min == 30 + 3 * 60);       // 在线的 30 分钟只算一次
    assert(s.c.acct_now == s.real_now);
}

// 场景 3:深睡 10 小时 → 同步:醒来时补了 8 小时,同步时不会再多补 2 小时。
static void test_scenario_long_deep_sleep(void) {
    sim_t s;
    sim_new(&s, 1);
    assert(sim_sync(&s, s.real_now));
    sim_save(&s);
    sim_sleep(&s, 10 * HOUR_S);
    assert(sim_boot(&s, PUPU_RESET_DEEPSLEEP));
    assert(s.caught_up_s == 8 * HOUR_S);
    assert(s.m.day_s == 10 * HOUR_S);  // 天数按完整的 10 小时走

    assert(sim_sync(&s, s.real_now));
    assert(s.caught_up_s == 8 * HOUR_S);
    assert(s.m.life_min == 8 * 60);
}

// 场景 4:先收到一条 2100 年的 TIME,再收到正确的 TIME:时间能纠正过来,
// 之后断电再同步也正常。
static void test_scenario_bad_phone_time(void) {
    sim_t s;
    sim_new(&s, 1);
    assert(sim_sync(&s, s.real_now));
    assert(sim_sync(&s, 4102444800u));  // 2100-01-01
    assert(s.caught_up_s == 8 * HOUR_S);  // 错误时间最多造成 8 小时补算(最低 10)
    assert(s.m.full >= 10 && s.m.happy >= 10);

    assert(sim_sync(&s, s.real_now));
    assert(s.caught_up_s == 8 * HOUR_S);
    assert(s.c.acct_now == s.real_now);

    sim_save(&s);
    sim_power_off(&s, HOUR_S);
    assert(!sim_boot(&s, PUPU_RESET_POWERON));
    assert(sim_sync(&s, s.real_now));
    assert(s.caught_up_s == 9 * HOUR_S);  // 只多补了关机的 1 小时
    assert(s.c.acct_now == s.real_now);
}

// 场景 5:开机原因是 USB 或 TASK_WDT 时,也走 RTC 补算。
static void test_scenario_usb_and_watchdog(void) {
    sim_t s;
    sim_new(&s, 1);
    assert(sim_sync(&s, s.real_now));
    sim_save(&s);
    sim_sleep(&s, 10 * MIN_S);
    assert(sim_boot(&s, PUPU_RESET_USB));
    assert(s.caught_up_s == 10 * MIN_S);

    sim_save(&s);
    sim_sleep(&s, 20 * MIN_S);
    assert(sim_boot(&s, PUPU_RESET_TASK_WDT));
    assert(s.caught_up_s == 30 * MIN_S);
    assert(s.c.acct_now == s.real_now);

    // 重启前没来得及存档的在线时间:宠物退回到存档,RTC 差值正好把它补回来。
    sim_save(&s);
    sim_online(&s, 5 * MIN_S);
    sim_sleep(&s, 1 * MIN_S);
    assert(sim_boot(&s, PUPU_RESET_PANIC));
    assert(s.caught_up_s == 36 * MIN_S);
    assert(s.c.acct_now == s.real_now);
}

// 断电重启后在第一次存档前又重启:旧存档的 RTC 来自上一次通电,不能信。
static void test_restart_before_first_save(void) {
    sim_t s;
    sim_new(&s, 1);
    sim_save(&s);
    sim_power_off(&s, HOUR_S);
    assert(!sim_boot(&s, PUPU_RESET_POWERON));
    sim_sleep(&s, 10 * MIN_S);
    assert(!sim_boot(&s, PUPU_RESET_USB));
    assert(s.caught_up_s == 0);
}

// 从没同步过的设备,深睡醒来照样按 RTC 补算。
static void test_never_synced_deep_sleep(void) {
    sim_t s;
    sim_new(&s, 1);
    sim_save(&s);
    sim_sleep(&s, 2 * HOUR_S);
    assert(sim_boot(&s, PUPU_RESET_DEEPSLEEP));
    assert(s.caught_up_s == 2 * HOUR_S);
    assert(s.c.acct_now == 0);
}

// 时间加速:离开时长先乘倍率再套 8 小时上限,acct_now 一直用真实秒。
static void test_scaled_away(void) {
    sim_t s;
    sim_new(&s, 60);
    assert(sim_sync(&s, s.real_now));
    sim_save(&s);
    sim_sleep(&s, 10 * MIN_S);  // 真实 10 分钟 = 宠物 10 小时
    assert(sim_boot(&s, PUPU_RESET_DEEPSLEEP));
    assert(s.caught_up_s == 8 * HOUR_S);
    assert(s.m.day_s == 10 * HOUR_S);
    assert(s.c.acct_now == s.real_now);

    // 在线 1 真实分钟 = 宠物 1 小时。
    uint32_t life = s.m.life_min;
    sim_online(&s, MIN_S);
    assert(s.m.life_min == life + 60);
    assert(s.c.acct_now == s.real_now);
}

// 补算跨过宠物日以后,首次同步奖励算在新的一天(先补算,再加 +2)。
static void test_sync_bonus_after_rollover(void) {
    sim_t s;
    sim_new(&s, 1);
    assert(sim_sync(&s, s.real_now));
    assert(s.m.synced_today && s.m.bond == 2);
    s.m.bond_today = PUPU_BOND_PER_DAY;
    s.m.day_s = PUPU_DAY_S - HOUR_S;
    s.real_now += 2 * HOUR_S;
    assert(sim_sync(&s, s.real_now));
    assert(s.m.synced_today && s.m.bond == 4 && s.m.bond_today == 2);
}

int main(void) {
    test_scale_and_remainder();
    test_reset_reasons();
    test_boot_checks();
    test_sync_rules();
    test_scenario_deep_sleep_then_sync();
    test_scenario_power_loss_twice();
    test_scenario_long_deep_sleep();
    test_scenario_bad_phone_time();
    test_scenario_usb_and_watchdog();
    test_restart_before_first_save();
    test_never_synced_deep_sleep();
    test_scaled_away();
    test_sync_bonus_after_rollover();
    return 0;
}
