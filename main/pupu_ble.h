// main/pupu_ble.h —— 噗噗的蓝牙:同步窗口里的 NimBLE 外设,Nordic UART(NUS)通道布局。
//
// 平时蓝牙是关着的(NimBLE 启动要占约 60 KB 内存,常开也耗电)。同步窗口打开时
// pupu_ctl 工作任务调用 pupu_ble_start(),窗口结束时调用 pupu_ble_stop()。启动和停止的写法
// 照搬 main/demo_ble.c:自己建主机任务、用信号量确认它停下、最多等 2 秒、失败时保留所有权
// 以便再试。每次启动都重新注册 GATT 表(停止时 NimBLE 会把它释放掉)。
//
// GATT(v1 不配对,所有特征都不加密):
//   服务 NUS 6e400001-b5a3-f393-e0a9-e50e24dcca9e
//   RX   6e400002-…  WRITE | WRITE_NO_RSP  手机写入一条消息(最多 20 字节,见 pupu_sync_proto.h)
//   TX   6e400003-…  NOTIFY | READ         设备回复;读取时返回 "PUPU/<协议版本>/<固件版本>"
// 广播包里放标志位和 128 位 NUS UUID;名字 Pupu-XXXX(蓝牙地址末 4 位)放在扫描响应里。
//
// 线程:
// - NimBLE 回调运行在 NimBLE 主机任务里:只把事件和收到的字节拷进队列,
//   不碰界面、不碰 NVS,也绝不调用 stop(nimble_port_stop 会永远等下去,死锁)。
// - pupu_ble_poll()、pupu_ble_send() 只在 LVGL 任务(app 主循环)里调用;
//   主循环在请求关窗口以后就不再调用 pupu_ble_send()。
// - pupu_ble_start()、pupu_ble_stop() 只在 pupu_ctl 工作任务里调用(会阻塞,停止最多约 2 秒)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pupu_sync_proto.h"

#define PUPU_BLE_NAME_LEN 10  // "Pupu-XXXX" 加结尾的 0

typedef enum {
    PUPU_BLE_EV_ADVERTISING = 1,  // 开始广播(启动后,或者手机断开后重新广播)
    PUPU_BLE_EV_CONNECTED,
    PUPU_BLE_EV_DISCONNECTED,
    PUPU_BLE_EV_SUBSCRIBED,  // 手机打开了 TX 通知:从这以后才能回复
    PUPU_BLE_EV_RX,          // 收到一条消息(data 前 len 个字节)
    PUPU_BLE_EV_FAILED,      // 广播失败或者主机复位(code 是 NimBLE 错误码):关窗口
} pupu_ble_ev_kind_t;

typedef struct {
    uint8_t kind;  // pupu_ble_ev_kind_t
    uint8_t len;   // RX:写入的长度;超过 20 字节时是 21,data 里只拷了前 21 个字节
    int16_t code;  // FAILED:错误码;DISCONNECTED:断开原因
    uint8_t data[PUPU_SYNC_MSG_MAX + 1];
} pupu_ble_event_t;

// 设备的蓝牙名字 "Pupu-XXXX"。任何时候都能调用,蓝牙不用开着。
const char *pupu_ble_name(void);

// 打开蓝牙并开始广播(广播在主机任务同步完成后才开始,会发 ADVERTISING 事件)。
// 已经开着时返回 ESP_ERR_INVALID_STATE。只在 pupu_ctl 任务里调用。
esp_err_t pupu_ble_start(void);

// 关掉蓝牙(手机还连着时会先断开它)。失败时保留所有权,可以再调用一次重试。
// 只在 pupu_ctl 任务里调用,绝不能在 NimBLE 回调里调用。
esp_err_t pupu_ble_stop(void);

// 主机部分已经释放、控制器却没关掉(nimble_port_deinit 只做了一半):这时不能再调用
// pupu_ble_stop() 重试(会碰到已经释放的内存),只能重启。
bool pupu_ble_broken(void);

// 主循环决定关窗口时马上调用(只改一个标志):从这一刻起不再重新广播、不再收写入、
// 不再发通知,不用等 pupu_ctl 真的去关。
void pupu_ble_close_soon(void);

// 取出下一个事件,没有时返回 false。只在主循环里调用。
bool pupu_ble_poll(pupu_ble_event_t *ev);

// 给手机发一条回复(TX 通知)。手机还没打开通知、或者没连着时返回 false,什么都不发。
bool pupu_ble_send(const uint8_t *data, size_t len);
