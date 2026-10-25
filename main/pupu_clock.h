// main/pupu_clock.h —— 真实时间换算成宠物时间,以及"离开了多久"的纪元记账(纯逻辑)。
//
// 三种时间:
// - 真实时间:esp_timer_get_time() 的差值(微秒),由调用方传进来;
// - 宠物时间:真实秒数 × 倍率(CONFIG_PUPU_TIME_SCALE),交给 pupu_model;
// - 纪元时间:手机告诉设备的 Unix 秒,只用来算"离开了多久",永远不乘倍率。
//
// acct_now 的意思是"当前宠物状态对应现实世界的哪一秒",0 表示从没同步过。
// 每次存档都把 acct_now 写成存档里的 acct_epoch;开机时从 acct_epoch 恢复。
// 这样深睡醒来先按 RTC 补算、之后再同步时,同步算出的离开≈0,不会重复补算;
// 断电后同步时,离开 = 关机时长 + 断电前没来得及存档的在线时间,正好对应已经
// 退回到上次存档的宠物状态。
//
// 本模块不调用任何 ESP-IDF 函数;开机原因、RTC 时间和令牌都由调用方读好传进来。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 和 ESP-IDF 5.5.3 的 esp_reset_reason_t 编号一一对应,设备端代码要用
// _Static_assert 确认两边一致后再强转。
typedef enum {
    PUPU_RESET_UNKNOWN = 0,
    PUPU_RESET_POWERON = 1,
    PUPU_RESET_EXT = 2,
    PUPU_RESET_SW = 3,
    PUPU_RESET_PANIC = 4,
    PUPU_RESET_INT_WDT = 5,
    PUPU_RESET_TASK_WDT = 6,
    PUPU_RESET_WDT = 7,
    PUPU_RESET_DEEPSLEEP = 8,
    PUPU_RESET_BROWNOUT = 9,
    PUPU_RESET_SDIO = 10,
    PUPU_RESET_USB = 11,
    PUPU_RESET_JTAG = 12,
    PUPU_RESET_EFUSE = 13,
    PUPU_RESET_PWR_GLITCH = 14,
    PUPU_RESET_CPU_LOCKUP = 15,
} pupu_reset_reason_t;

typedef struct {
    uint32_t scale;       // 时间倍率 1–120(Kconfig 限定范围)
    uint32_t acct_now;    // 纪元记账秒,0 = 从没同步过
    int64_t real_rem_us;  // 还没换算的不满 1 秒的真实微秒,下次进位
} pupu_clock_t;

// 开机时读好的信息。token 是会话令牌:设备端每次存档都写进存档,同一个值也放在
// RTC_NOINIT_ATTR 变量里;断过电时 RTC 内存是乱码,两边就对不上。设备端只在
// 连续性检查不通过时才换新令牌,通过时沿用存档里的令牌。令牌永远不是 0:
// 存档里的令牌是 0(M3 及更早的固件写的)时一律当作断过电。
typedef struct {
    pupu_reset_reason_t reason;  // esp_reset_reason()
    uint64_t saved_token;        // 存档里的令牌
    uint64_t rtc_token;          // RTC_NOINIT 变量里的令牌
    uint64_t saved_rtc_us;       // 存档时的 esp_rtc_get_time_us()
    uint64_t now_rtc_us;         // 现在的 esp_rtc_get_time_us()
} pupu_boot_info_t;

// saved_acct_epoch 是存档里的 acct_epoch;没有存档时传 0。
void pupu_clock_init(pupu_clock_t *c, uint32_t scale, uint32_t saved_acct_epoch);

// 主循环每次调用一次,real_delta_us 是距上次调用的真实微秒(非负)。
// 不满 1 秒的部分留到下次进位。返回这次要推进的宠物秒数;
// 同步过时 acct_now 同时前进相同的真实秒数(不乘倍率)。
uint32_t pupu_clock_tick(pupu_clock_t *c, int64_t real_delta_us);

// 真实秒 → 宠物秒。补算时先乘倍率,再由 pupu_model_catch_up 套 8 小时上限。
uint64_t pupu_clock_to_pet(const pupu_clock_t *c, uint64_t real_s);

// 这次开机后 RTC 是否还连续可信。POWERON、BROWNOUT、UNKNOWN 当作断过电。
bool pupu_clock_reset_keeps_rtc(pupu_reset_reason_t reason);

// 开机时的补算判断。连续性检查(开机原因、令牌一致、RTC 没有倒退)都通过时返回
// true,*away_real_s 是离开的真实秒数(不封顶),acct_now 也前进这么多。
// 不通过时返回 false、*away_real_s = 0,当作断过电:宠物保持存档时的样子。
bool pupu_clock_boot(pupu_clock_t *c, const pupu_boot_info_t *boot, uint64_t *away_real_s);

// 收到手机的 TIME。早于 2026-01-01 或晚于 2100-01-01 的时间返回 false,什么都不改
// (回 ERR,错误码 3)。上限保证 acct_now(u32)再走几年也不会回绕成 0("从没同步过")。
// 接受时返回 true:第一次同步不补算(*away_real_s = 0);否则
// *away_real_s = max(0, phone_epoch - acct_now)。无论哪种,acct_now 都设成手机时间,
// 哪怕它比 acct_now 还早,这样一次错误的手机时间下次同步就能纠正。
// 调用方接着做:pupu_model_catch_up(to_pet(away)),再 pupu_model_apply_sync()。
bool pupu_clock_sync(pupu_clock_t *c, uint32_t phone_epoch, uint64_t *away_real_s);
