#include "pupu_ble.h"

#include "demo_radio.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "pupu_ble";

#define STOP_TIMEOUT_MS 2000  // 和 demo_ble.c 一样;手机还连着时 NimBLE 自己要先断开它
#define EVENT_QUEUE_DEPTH 8

// NUS 的 UUID。BLE_UUID128_INIT 从最低字节写起,所以和文字写法正好反过来。
static const ble_uuid128_t k_nus_svc = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t k_nus_rx = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t k_nus_tx = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static QueueHandle_t s_events;
static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static bool s_initialized;
static bool s_stop_in_progress;
static bool s_host_done;
static volatile bool s_running;  // 窗口开着:手机断开后要重新广播
static bool s_broken;            // nimble_port_deinit 只做了一半,见 pupu_ble_broken()
static uint8_t s_addr_type;
static uint16_t s_tx_handle;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;  // s_lock 保护
static bool s_subscribed;                           // s_lock 保护
static char s_name[PUPU_BLE_NAME_LEN];
static char s_ident[48];  // TX 读取时返回的标识,方便用 nRF Connect 或 Chrome 调试

static int gap_event(struct ble_gap_event *event, void *arg);
static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);

// 每个特征都要有 access_cb(包括只发通知的 TX),否则注册时报 rc=3,看起来很像内存不足。
static const struct ble_gatt_svc_def k_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &k_nus_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &k_nus_rx.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &k_nus_tx.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                .val_handle = &s_tx_handle,
            },
            {0},
        },
    },
    {0},
};

static void log_heap(const char *where) {
    ESP_LOGI(TAG, "内存[%s] 堆 %" PRIu32 " B 最大块 %" PRIu32 " B 历史最低 %" PRIu32 " B", where,
             esp_get_free_heap_size(),
             (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             esp_get_minimum_free_heap_size());
}

// 下面几个函数都运行在 NimBLE 主机任务里:只往队列里放事件,队列满了就丢掉。
static void post(uint8_t kind, int code) {
    pupu_ble_event_t ev = {.kind = kind, .code = (int16_t)code};
    if (s_events == NULL || xQueueSend(s_events, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "事件队列满,丢掉事件 %d", kind);
    }
}

static void set_link(uint16_t conn, bool subscribed) {
    portENTER_CRITICAL(&s_lock);
    s_conn = conn;
    s_subscribed = subscribed;
    portEXIT_CRITICAL(&s_lock);
}

static int advertise(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &k_nus_svc;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    // 广播包只有 31 字节:标志位 3 + UUID 18 以后放不下名字,名字放在扫描响应里
    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = (uint8_t)strlen(s_name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) return rc;

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;  // 可以连接
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == 0) post(PUPU_BLE_EV_ADVERTISING, 0);
    return rc;
}

static void readvertise(void) {
    if (!s_running) return;
    const int rc = advertise();
    if (rc != 0) {
        ESP_LOGE(TAG, "重新广播失败: %d", rc);
        post(PUPU_BLE_EV_FAILED, rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "手机连上了,连接号 %d", event->connect.conn_handle);
            // 外设的 CONNECT 事件要等读完对方版本和功能才发,手机可能已经先打开了通知
            // (见下面 SUBSCRIBE):同一个连接号时保留它
            portENTER_CRITICAL(&s_lock);
            const bool early_sub = s_conn == event->connect.conn_handle && s_subscribed;
            s_conn = event->connect.conn_handle;
            s_subscribed = early_sub;
            portEXIT_CRITICAL(&s_lock);
            post(PUPU_BLE_EV_CONNECTED, 0);
            if (early_sub) post(PUPU_BLE_EV_SUBSCRIBED, 0);
        } else {
            ESP_LOGW(TAG, "连接失败: %d", event->connect.status);
            readvertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "手机断开,原因 0x%x", event->disconnect.reason);
        set_link(BLE_HS_CONN_HANDLE_NONE, false);
        post(PUPU_BLE_EV_DISCONNECTED, event->disconnect.reason);
        // 一旦有手机连上,传统广播就停了,而且不会发 ADV_COMPLETE:窗口还开着就自己重新广播
        readvertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        bool matched = false;
        // 只允许一个连接(CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1):CONNECT 事件还没到时,
        // 订阅只可能来自正在建立的那个连接,先认下它的连接号
        portENTER_CRITICAL(&s_lock);
        if (s_conn == BLE_HS_CONN_HANDLE_NONE) s_conn = event->subscribe.conn_handle;
        if (event->subscribe.conn_handle == s_conn && event->subscribe.attr_handle == s_tx_handle) {
            s_subscribed = event->subscribe.cur_notify != 0;
            matched = s_subscribed;
        }
        portEXIT_CRITICAL(&s_lock);
        if (matched) post(PUPU_BLE_EV_SUBSCRIBED, 0);
        return 0;
    }
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU 协商为 %d(消息仍然最多 20 字节)", event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE: {
        portENTER_CRITICAL(&s_lock);
        const bool linked = s_conn != BLE_HS_CONN_HANDLE_NONE;
        portEXIT_CRITICAL(&s_lock);
        if (!linked) readvertise();
        return 0;
    }
    default:
        return 0;
    }
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // NimBLE 会按偏移量自己裁剪长读取;s_ident 在窗口里不变
        return os_mbuf_append(ctxt->om, s_ident, strlen(s_ident)) == 0 ? 0
                                                                       : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    portENTER_CRITICAL(&s_lock);
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) s_conn = conn_handle;  // 同上:CONNECT 事件可能晚到
    const bool ours = conn_handle == s_conn;
    portEXIT_CRITICAL(&s_lock);
    if (!s_running || !ours) return 0;

    // 只拷字节,处理交给主循环(这里不碰宠物状态、界面和 NVS)。
    // 超过 20 字节时多拷 1 个,长度写 21,协议会回"长度不对"。
    pupu_ble_event_t ev = {.kind = PUPU_BLE_EV_RX};
    const uint16_t total = OS_MBUF_PKTLEN(ctxt->om);
    const uint16_t n = total > sizeof(ev.data) ? (uint16_t)sizeof(ev.data) : total;
    if (os_mbuf_copydata(ctxt->om, 0, n, ev.data) != 0) return BLE_ATT_ERR_UNLIKELY;
    ev.len = (uint8_t)n;
    if (s_events == NULL || xQueueSend(s_events, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "事件队列满,丢掉一条 %u 字节的写入", (unsigned)total);
    }
    return 0;
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE 主机复位: %d", reason);
    post(PUPU_BLE_EV_FAILED, reason);
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) {
        uint8_t addr[6] = {0};
        if (ble_hs_id_copy_addr(s_addr_type, addr, NULL) == 0) {
            ESP_LOGI(TAG, "蓝牙地址 %02X:%02X:%02X:%02X:%02X:%02X,名字 %s", addr[5], addr[4],
                     addr[3], addr[2], addr[1], addr[0], s_name);
        }
    }
    if (rc == 0 && s_running) rc = advertise();
    if (rc != 0) {
        ESP_LOGE(TAG, "开始广播失败: %d", rc);
        post(PUPU_BLE_EV_FAILED, rc);
    }
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    // 和 demo_ble.c 一样不用 nimble_port_freertos_init:它自己记着任务句柄,建任务失败也不管。
    xSemaphoreGive(s_host_stopped);
    for (;;) vTaskSuspend(NULL);
}

const char *pupu_ble_name(void) {
    if (s_name[0] == '\0') {
        uint8_t mac[6] = {0};
        if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) ESP_LOGW(TAG, "读不到蓝牙地址");
        snprintf(s_name, sizeof(s_name), "Pupu-%02X%02X", mac[4], mac[5]);
    }
    return s_name;
}

esp_err_t pupu_ble_start(void) {
    if (s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_events == NULL) {
        s_events = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(pupu_ble_event_t));
        if (s_events == NULL) return ESP_ERR_NO_MEM;
    }
    xQueueReset(s_events);  // 上一个窗口剩下的事件不要
    set_link(BLE_HS_CONN_HANDLE_NONE, false);
    (void)pupu_ble_name();
    snprintf(s_ident, sizeof(s_ident), "PUPU/%d/%s", PUPU_SYNC_PROTO_VER,
             esp_app_get_description()->version);
    log_heap("before");

    s_stop_in_progress = false;
    s_host_done = false;
    // NVS 在开机时已经初始化过(存档要用),这里只是确认;失败时不擦除
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s", esp_err_to_name(err));
        return err;
    }
    s_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (s_host_stopped == NULL) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(k_services);
    if (rc == 0) rc = ble_gatts_add_svcs(k_services);
    if (rc == 0) rc = ble_svc_gap_device_name_set(s_name);  // GAP 设备名和广播名一致
    if (rc != 0) {
        ESP_LOGE(TAG, "注册 GATT 表或设备名失败: %d", rc);
        err = ESP_FAIL;
        goto failed_start;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    s_running = true;
    if (xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE, NULL,
                                configMAX_PRIORITIES - 4, &s_host_task, NIMBLE_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }
    log_heap("started");
    return ESP_OK;

failed_start:
    // 还没有主机任务,nimble_port_stop() 会返回 BLE_HS_EALREADY:直接 deinit;
    // 清理本身失败时保留所有权,下次 stop 再试。
    s_running = false;
    {
        const esp_err_t cleanup = pupu_ble_stop();
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "启动失败后的清理也失败: %s", esp_err_to_name(cleanup));
    }
    return err;
}

esp_err_t pupu_ble_stop(void) {
    s_running = false;  // 从这里开始,断开时不再重新广播,写入也不再收
    if (s_broken) return ESP_FAIL;
    if (!s_initialized) return ESP_OK;

    if (s_host_task && !s_stop_in_progress) {
        (void)ble_gap_adv_stop();
        // 手机还连着时,ble_hs_stop 会先断开它(最多等 2 秒)
        const int rc = nimble_port_stop();
        if (rc != 0) {
            ESP_LOGE(TAG, "nimble_port_stop 失败: %d", rc);
            return ESP_FAIL;
        }
        s_stop_in_progress = true;
    }

    if (s_host_task && !s_host_done) {
        if (s_host_stopped == NULL ||
            xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "等待 NimBLE 主机任务停止超时");
            return ESP_ERR_TIMEOUT;
        }
        s_host_done = true;
    }

    if (s_host_task) {
        vTaskDelete(s_host_task);
        s_host_task = NULL;
    }

    // nimble_port_deinit 先释放主机(这一步总会成功),再关控制器。失败只可能是控制器那一半,
    // 这时主机的内存已经释放,再调用一次会崩溃:记下来,由调用方重启。
    const esp_err_t err = nimble_port_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_deinit 失败: %s", esp_err_to_name(err));
        s_broken = true;
        return err;
    }
    s_initialized = false;
    s_stop_in_progress = false;
    s_host_done = false;
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    set_link(BLE_HS_CONN_HANDLE_NONE, false);
    log_heap("stopped");
    return ESP_OK;
}

bool pupu_ble_broken(void) {
    return s_broken;
}

void pupu_ble_close_soon(void) {
    s_running = false;
}

bool pupu_ble_poll(pupu_ble_event_t *ev) {
    return s_events != NULL && xQueueReceive(s_events, ev, 0) == pdTRUE;
}

bool pupu_ble_send(const uint8_t *data, size_t len) {
    portENTER_CRITICAL(&s_lock);
    const uint16_t conn = s_conn;
    const bool subscribed = s_subscribed;
    portEXIT_CRITICAL(&s_lock);
    // 手机打开通知之前发的通知会被它丢掉,所以干脆不发
    if (!s_running || conn == BLE_HS_CONN_HANDLE_NONE || !subscribed) return false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om == NULL) {
        ESP_LOGW(TAG, "发通知时没有缓冲区");
        return false;
    }
    const int rc = ble_gatts_notify_custom(conn, s_tx_handle, om);  // 不管成败 om 都会被释放
    if (rc != 0) {
        ESP_LOGW(TAG, "发通知失败: %d", rc);
        return false;
    }
    return true;
}
