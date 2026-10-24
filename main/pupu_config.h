// main/pupu_config.h —— 噗噗的全部规则数值(纯逻辑,只用标准 C)。
//
// 数值来自产品说明 docs/assets/pupu/design.zh_CN.md 第 2–5 节和第 12 节。
// 都是暂定值,真机试玩后在这里统一调整。时间类数值都是"宠物时间",
// 即真实秒数 × CONFIG_PUPU_TIME_SCALE(倍率换算见 pupu_clock.h)。
#pragma once

// ---- 状态范围和新蛋初始值 ----
#define PUPU_STAT_MAX              100  // 饱腹、开心的上限
#define PUPU_EGG_FULL              80   // 新蛋饱腹
#define PUPU_EGG_HAPPY             80   // 新蛋开心
#define PUPU_ADULT_BOND            100  // 幼年亲密攒满这个数长成成年
#define PUPU_CANDY_MAX             99   // 星星糖最多存这么多

// ---- 每分钟衰减(宠物时间) ----
#define PUPU_FULL_DECAY_EVERY_MIN  6    // 饱腹每 6 分钟 -1,即每小时 -10
#define PUPU_HAPPY_DECAY_EVERY_MIN 15   // 开心每 15 分钟掉一次
#define PUPU_HAPPY_DECAY           2    // 平时每次 -2,即每小时 -8
#define PUPU_HAPPY_DECAY_STARVING  4    // 很饿时每次 -4,即每小时 -16
#define PUPU_STARVING_BELOW        15   // 饱腹低于这个数算"很饿"
#define PUPU_HUNGRY_BELOW          40   // 饱腹低于这个数算"饿"(冒气泡、饭团加亲密)

// ---- 离开期间的补算 ----
#define PUPU_CATCH_UP_MAX_S        (8u * 3600u)  // 数值最多只补 8 小时
#define PUPU_CATCH_UP_FLOOR        10            // 补算部分最低降到 10
#define PUPU_DAY_S                 86400u        // 一个宠物日 = 24 小时宠物时间

// ---- 喂食 ----
#define PUPU_REFUSE_FULL_AT        90   // 饱腹 >= 90 时三种食物都不吃
#define PUPU_RICE_FULL             35   // 饭团:饱腹 +35
#define PUPU_RICE_HUNGRY_BOND      3    // 饿时喂饭团:亲密 +3
#define PUPU_COOKIE_FULL           10   // 小饼干:饱腹 +10
#define PUPU_COOKIE_HAPPY          5    // 小饼干:开心 +5
#define PUPU_COOKIE_WINDOW_MIN     60   // 第一块饼干开启 60 分钟窗口
#define PUPU_COOKIE_HAPPY_LIMIT    3    // 窗口内前 3 块加开心,第 4 块起吃腻
#define PUPU_CANDY_HAPPY           25   // 星星糖:开心 +25
#define PUPU_CANDY_BOND            2    // 星星糖:亲密 +2

// ---- 摸摸头 ----
#define PUPU_PAT_HAPPY             5    // 开心 +5
#define PUPU_PAT_BOND              1    // 亲密 +1
#define PUPU_PAT_WINDOW_MIN        10   // 第一次摸摸开启 10 分钟窗口
#define PUPU_PAT_LIMIT             3    // 窗口内前 3 次有效,第 4 次起躲开

// ---- 小游戏「接星星」结算 ----
#define PUPU_CATCH_WIN_STARS       8    // 接到这么多颗算赢
#define PUPU_CATCH_FULL_COST       5    // 完成一局:饱腹 -5
#define PUPU_CATCH_HAPPY           15   // 赢:开心 +15
#define PUPU_CATCH_BOND            3    // 赢:亲密 +3
#define PUPU_CANDY_PER_DAY         3    // 每天最多从小游戏得 3 颗星星糖

// ---- 同步和每日上限 ----
#define PUPU_SYNC_BOND             2    // 每个宠物日第一次被接受的对时:亲密 +2
#define PUPU_BOND_PER_DAY          20   // 所有来源的亲密加起来每天最多 +20
#define PUPU_EPOCH_MIN             1767225600u  // 2026-01-01 00:00:00 UTC,更早的手机时间直接拒绝
#define PUPU_EPOCH_MAX             4102444800u  // 2100-01-01 00:00:00 UTC,更晚的也拒绝:离 u32 上限还有 6 年,acct_now 不会回绕

// ---- 同步窗口(真实毫秒,不受时间倍率影响;产品说明第 12 节第 7 条) ----
#define PUPU_SYNC_WAIT_MS          60000u   // 60 秒内没有手机来连:关窗口
#define PUPU_SYNC_IDLE_MS          20000u   // 连上过以后 20 秒没有新请求:关窗口
#define PUPU_SYNC_MAX_MS           120000u  // 整次最长 2 分钟
#define PUPU_SYNC_DONE_LINGER_MS   1500u    // 收到 DONE 以后挥手、"叮"一声,这么久以后再关

// ---- 按键(真实毫秒,不受时间倍率影响) ----
#define PUPU_HOLD_MS               3000  // 蛋:按住 OK 这么久孵化
#define PUPU_HOLD_GRACE_MS         150   // 蛋:松开后这么久内又按下,算接触抖动,不取消
#define PUPU_KEY_SWITCH_MS         120   // 一个键松开后这么久内另一个键按下,当作幻影键忽略
#define PUPU_GAME_EXIT_MS          500   // 小游戏:按住 OK 这么久退出(和按键驱动的长按一样长)

// ---- 变暗、熄屏、深睡(真实毫秒,不受时间倍率影响;测出耗电后再调) ----
#define PUPU_DIM_AFTER_MS          20000u              // 没动静 20 秒:变暗
#define PUPU_OFF_AFTER_MS          45000u              // 没动静 45 秒:熄屏
#define PUPU_SLEEP_AFTER_OFF_MS    (5u * 60u * 1000u)  // 熄屏 5 分钟:深睡
#define PUPU_SLEEP_RETRY_MS        10000u              // 深睡被拒绝(有键按着)后多久再试
#define PUPU_DIM_BACKLIGHT         30                  // 变暗时的背光百分比
#define PUPU_WAKE_SWALLOW_MS       250u                // 亮屏的键松开后,再吞这么久的事件
#define PUPU_WAKE_GUARD_MAX_MS     10000u              // 亮屏的键一直不松开时,最多吞这么久

// ---- 离开提示 ----
#define PUPU_WELCOME_AWAY_S        3600u  // 补算的离开时长(宠物时间)超过 1 小时,显示"你回来啦"

// ---- 存档时机(真实毫秒,不受时间倍率影响) ----
#define PUPU_SAVE_DELAY_MS         1000u              // 状态变了以后等这么久(并且等音效放完)再存
#define PUPU_SAVE_EVERY_MS         (5u * 60u * 1000u) // 开机期间每 5 分钟存一次
#define PUPU_SAVE_BATTERY_LOW      5                  // 电量降到这个百分比以下时存一次
