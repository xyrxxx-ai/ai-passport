#include "pupu_ctl.h"

#include "pupu_ble.h"
#include "pupu_sfx.h"
#include "pupu_store.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "pupu_ctl";

#define TASK_PRIORITY 3  // 比 LVGL(4)低:界面先画
#define TASK_STACK 4096
#define QUEUE_DEPTH 4
#define FLUSH_TIMEOUT_MS 3000
#define SFX_STOP_TIMEOUT_MS 500
// 电量计第一次算不出电量(比如深睡很久以后)时,隔一会儿再试。每次最多等 5 秒(在 BSP 里)。
#define BATTERY_RETRY_MS 30000
#define BATTERY_RETRIES 10

typedef enum { CMD_BATTERY_INIT = 1, CMD_DEEP_SLEEP, CMD_BLE_START, CMD_BLE_STOP } cmd_kind_t;

typedef struct {
    uint8_t kind;
    pupu_save_t snapshot;
} cmd_t;

static QueueHandle_t s_queue;
static volatile bool s_battery_ready;
static volatile bool s_sleep_refused;
static volatile bool s_key_seen;  // 交出深睡命令以后有没有按过键
static volatile pupu_ctl_ble_t s_ble = PUPU_CTL_BLE_OFF;
static int s_battery_retries_left;  // 这三个只在工作任务里用
static bool s_battery_retry_pending;
static TickType_t s_battery_retry_at;

// step 只进串口日志;用英文,字表检查分不出它不上屏(见 tests/test_pupu_charset.py)。
static void warn_if_failed(const char *step, esp_err_t err) {
    if (err != ESP_OK) ESP_LOGW(TAG, "深睡继续:%s 失败: %s", step, esp_err_to_name(err));
}

// 失败时 BSP 已经把芯片切回正常模式、拿掉了 I2C 设备,再调用一次只需要等它算出电量。
// 板子上没有电量计(NOT_FOUND)时不再试。
static void battery_init(void) {
    s_battery_retry_pending = false;
    const esp_err_t err = bsp_battery_init();
    if (err == ESP_OK) {
        s_battery_ready = true;
        return;
    }
    if (err != ESP_ERR_NOT_FOUND && s_battery_retries_left > 0) {
        s_battery_retries_left--;
        s_battery_retry_pending = true;
        s_battery_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(BATTERY_RETRY_MS);
        ESP_LOGW(TAG, "电量计还没就绪(%s),%d 秒后第 %d 次重试", esp_err_to_name(err),
                 BATTERY_RETRY_MS / 1000, BATTERY_RETRIES - s_battery_retries_left);
        return;
    }
    ESP_LOGW(TAG, "电量计不可用,顶栏不显示电量: %s", esp_err_to_name(err));
}

static void ble_start(void) {
    const esp_err_t err = pupu_ble_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "蓝牙打不开: %s", esp_err_to_name(err));
    s_ble = err == ESP_OK ? PUPU_CTL_BLE_ON : PUPU_CTL_BLE_START_FAILED;
}

static void ble_stop(void) {
    const esp_err_t err = pupu_ble_stop();
    if (err != ESP_OK) ESP_LOGE(TAG, "蓝牙关不掉: %s", esp_err_to_name(err));
    if (pupu_ble_broken()) {
        // 主机已经释放、控制器没关掉:再试会碰到已经释放的内存。重启是唯一干净的办法
        // (软件重启后 RTC 连续,开机照常补算;最多退回到最近一次存档)。
        ESP_LOGE(TAG, "蓝牙只关掉了一半,重启");
        esp_restart();
    }
    s_ble = err == ESP_OK ? PUPU_CTL_BLE_OFF : PUPU_CTL_BLE_STOP_FAILED;
}

// 顺序照搬 main/demo_low_power.c 的 sleep_task(tests/test_pupu_deep_sleep_contract.py 检查)。
static void deep_sleep(const pupu_save_t *snapshot) {
    // 1. 同步存档:等它真的写进 Flash。写不进去也继续睡,醒来按上一次存档补算。
    if (!pupu_store_flush(snapshot, FLUSH_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "深睡前的存档没写成,继续");
    }
    // 同步窗口开着时主循环不会要求深睡(算"忙碌");这里只是保险:射频必须先停下。
    // 蓝牙本来就关着时它马上返回。放在检查按键之前:关蓝牙最多要等 2 秒,这期间按的键也算数。
    // 关不掉就重启(存档刚写好;软件重启后 RTC 连续,开机照常补算),绝不带着射频睡下去。
    if (pupu_ble_stop() != ESP_OK) {
        ESP_LOGE(TAG, "深睡前蓝牙关不掉,重启");
        esp_restart();
    }
    // 2. 有键按着时 GPIO0 是低电平,一睡下去就会立刻醒:这次先不睡,稍后再试。
    //    等存档的这段时间里按过键(想亮屏),也不睡。这时按键驱动还活着,可以回头。
    const int mv = bsp_button_read_mv();
    if (s_key_seen || (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN)) {
        ESP_LOGW(TAG, "有键按着或刚按过(%d mV),这次不深睡", mv);
        s_sleep_refused = true;
        return;
    }
    ESP_LOGI(TAG, "进入深度休眠,按任意键唤醒");

    // 3. 从这里开始不能回头:按键交回数字输入后,本次运行就收不到按键了。
    int level = 0;
    const esp_err_t err = bsp_button_prepare_deep_sleep(&level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键交回失败,重启: %s", esp_err_to_name(err));
        esp_restart();
    }
    if (level != 1) ESP_LOGW(TAG, "入睡前按键电平是 %d,这次深睡可能马上醒", level);
    // 4. 三个键都把 GPIO0 拉低。第一个参数是位掩码,不是引脚号。
    warn_if_failed("GPIO wake arm",
                   esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));
    // 5. 电量计和音频芯片共用 I2C:先让电量计睡。
    warn_if_failed("CW2017 suspend", bsp_battery_sleep());
    // 6. 先让音效任务停下、不再写 I2S,再关音频。
    if (!pupu_sfx_stop(SFX_STOP_TIMEOUT_MS)) ESP_LOGW(TAG, "音效没在 %d ms 内停下", SFX_STOP_TIMEOUT_MS);
    warn_if_failed("ES8311 suspend", bsp_audio_sleep());
    warn_if_failed("I2S pin release", bsp_audio_prepare_deep_sleep());
    // 7. 共享 I2C 引脚。
    warn_if_failed("shared I2C pin release", bsp_i2c_prepare_deep_sleep());
    // 8. 拿着 LVGL 锁:等正在刷的那一帧刷完,之后 LVGL 不会再碰屏幕。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "拿不到 LVGL 锁,重启");
        esp_restart();
    }
    warn_if_failed("ST7789 suspend", bsp_display_prepare_deep_sleep());
    // 9. 睡下去。醒来等于重新开机;万一返回了,外设已经关掉,只能重启。
    esp_deep_sleep_start();
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回,重启");
    esp_restart();
}

static void ctl_task(void *arg) {
    (void)arg;
    // cmd_t 里有一份存档快照,放静态区,不占任务栈。
    static cmd_t cmd;
    for (;;) {
        // 有电量计重试在等时,队列最多等到重试的时间;命令总是先处理
        TickType_t wait = portMAX_DELAY;
        if (s_battery_retry_pending) {
            const TickType_t now = xTaskGetTickCount();
            wait = (int32_t)(s_battery_retry_at - now) > 0 ? s_battery_retry_at - now : 0;
        }
        if (xQueueReceive(s_queue, &cmd, wait) != pdTRUE) {
            if (!s_battery_retry_pending) continue;
            if (s_ble != PUPU_CTL_BLE_OFF) {
                // 蓝牙开着(或正在开关):重试要卡住工作任务最多 5 秒,会拖住关蓝牙。晚点再试,不算次数
                s_battery_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(BATTERY_RETRY_MS);
                continue;
            }
            battery_init();
            continue;
        }
        switch (cmd.kind) {
        case CMD_BATTERY_INIT:
            s_battery_retries_left = BATTERY_RETRIES;
            battery_init();
            break;
        case CMD_DEEP_SLEEP: deep_sleep(&cmd.snapshot); break;
        case CMD_BLE_START: ble_start(); break;
        case CMD_BLE_STOP: ble_stop(); break;
        default: break;
        }
    }
}

esp_err_t pupu_ctl_start(void) {
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(cmd_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(ctl_task, "pupu_ctl", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pupu_ctl_battery_init(void) {
    static const cmd_t cmd = {.kind = CMD_BATTERY_INIT};
    if (s_queue == NULL || xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "工作任务不可用,顶栏不显示电量");
    }
}

bool pupu_ctl_battery_ready(void) {
    return s_battery_ready;
}

bool pupu_ctl_deep_sleep(const pupu_save_t *snapshot) {
    static cmd_t cmd;  // 只在 LVGL 任务里调用;放静态区,不占 LVGL 任务的栈
    cmd.kind = CMD_DEEP_SLEEP;
    cmd.snapshot = *snapshot;
    s_key_seen = false;
    if (s_queue == NULL || xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "工作任务不可用,这次不深睡");
        s_sleep_refused = true;
        return false;
    }
    return true;
}

// 蓝牙命令只在主循环里发。状态先改好再交出命令,主循环看到的总是"正在开/正在关"。
static bool send_ble(cmd_kind_t kind, pupu_ctl_ble_t pending, pupu_ctl_ble_t failed) {
    static cmd_t cmd;  // 放静态区,不占 LVGL 任务的栈
    cmd.kind = (uint8_t)kind;
    if (s_queue == NULL) {
        // 工作任务根本没启动:蓝牙从来没打开过。要开就是"打不开";要关就是本来就关着,
        // 不能报"关不掉"(否则同步页会一直等下去,屏幕也不灭)。
        ESP_LOGW(TAG, "工作任务不可用,蓝牙命令 %d 没交出去", (int)kind);
        s_ble = kind == CMD_BLE_STOP ? PUPU_CTL_BLE_OFF : PUPU_CTL_BLE_START_FAILED;
        return false;
    }
    s_ble = pending;
    if (xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "工作任务不可用,蓝牙命令 %d 没交出去", (int)kind);
        s_ble = failed;
        return false;
    }
    return true;
}

bool pupu_ctl_ble_start(void) {
    return send_ble(CMD_BLE_START, PUPU_CTL_BLE_STARTING, PUPU_CTL_BLE_START_FAILED);
}

bool pupu_ctl_ble_stop(void) {
    return send_ble(CMD_BLE_STOP, PUPU_CTL_BLE_STOPPING, PUPU_CTL_BLE_STOP_FAILED);
}

pupu_ctl_ble_t pupu_ctl_ble_state(void) {
    return s_ble;
}

void pupu_ctl_note_key(void) {
    s_key_seen = true;
}

bool pupu_ctl_take_sleep_refused(void) {
    if (!s_sleep_refused) return false;
    s_sleep_refused = false;
    return true;
}
