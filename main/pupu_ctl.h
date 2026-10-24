// main/pupu_ctl.h —— 噗噗的"慢活"工作任务:初始化电量计、进入深度休眠(M6 还会加蓝牙开关)。
//
// 这些事要等 I2C、等 Flash,或者根本不再返回,不能放在 LVGL 任务(app 主循环)里做。
// 主循环只把命令放进队列;结果用标志位告诉主循环(pupu_ctl_battery_ready、
// pupu_ctl_take_sleep_refused)。工作任务从不碰 LVGL 对象。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "pupu_save_codec.h"

// 启动工作任务。只调用一次。
esp_err_t pupu_ctl_start(void);

// 初始化电量计(bsp_battery_init 要等芯片算出第一个电量,至少 100 ms、最长 5 秒)。
// 开机时先画出宠物再调用它。成功以后 pupu_ctl_battery_ready() 变成 true。
void pupu_ctl_battery_init(void);
bool pupu_ctl_battery_ready(void);

// 进入深度休眠:先同步存档(等写完),有键按着就放弃,否则按顺序关掉外设、睡下去。
// 调用以后主循环不能再碰 I2C、音频和存档,直到 pupu_ctl_take_sleep_refused() 返回 true;
// 如果真的睡下去了,醒来等于重新开机。返回 true 表示命令交给了工作任务(它会先同步存档);
// false 表示工作任务不可用,这次不深睡(之后 pupu_ctl_take_sleep_refused() 会返回 true)。
bool pupu_ctl_deep_sleep(const pupu_save_t *snapshot);

// 深睡被放弃过一次就返回 true(只返回一次)。
bool pupu_ctl_take_sleep_refused(void);

// 按键回调里调用(只写一个标志):交出深睡命令以后又有按键,就放弃这次深睡。
void pupu_ctl_note_key(void);
