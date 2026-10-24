// main/pupu_store.h —— 噗噗的存档:NVS 命名空间 "pupu",两个槽 slot_a / slot_b 轮流写。
//
// 记录格式见 pupu_save_codec.h;选哪个槽、写哪个槽见 pupu_save_policy.h。
// 写 Flash 在一个低优先级(2)的后台任务里做:pupu_store_save() 只用 xQueueOverwrite
// 把最新快照放进长度为 1 的队列就返回,界面和按键永远不等 Flash。
// 任何情况下都不擦除 NVS(擦除会把射频校准数据一起清掉)。
#pragma once

#include <stdbool.h>

#include "pupu_save_codec.h"

typedef struct {
    bool ready;    // NVS 能用,存档任务在跑;false 时这次开机不能存档
    bool loaded;   // *out 里是读出来的存档;false 时调用方从新蛋开始
    bool corrupt;  // 有存档数据但都读不出来:要提示"存档坏了"
    bool locked;   // 有更新版本固件的存档:这次开机一次都不写,要提示
} pupu_store_boot_t;

// 打开 NVS、读两个槽、启动存档任务。要在 nvs_flash_init() 成功之后调用,只调用一次。
pupu_store_boot_t pupu_store_start(pupu_save_t *out);

// 存一份快照。不阻塞;还没写的旧快照会被这份盖掉。不能存档或 locked 时什么都不做。
void pupu_store_save(const pupu_save_t *s);
