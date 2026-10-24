#include "pupu_ctl.h"

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
#define QUEUE_DEPTH 2
#define FLUSH_TIMEOUT_MS 3000
#define SFX_STOP_TIMEOUT_MS 500

typedef enum { CMD_BATTERY_INIT = 1, CMD_DEEP_SLEEP } cmd_kind_t;

typedef struct {
    uint8_t kind;
    pupu_save_t snapshot;
} cmd_t;

static QueueHandle_t s_queue;
static volatile bool s_battery_ready;
static volatile bool s_sleep_refused;
static volatile bool s_key_seen;  // 交出深睡命令以后有没有按过键

// step 只进串口日志;用英文,字表检查分不出它不上屏(见 tests/test_pupu_charset.py)。
static void warn_if_failed(const char *step, esp_err_t err) {
    if (err != ESP_OK) ESP_LOGW(TAG, "深睡继续:%s 失败: %s", step, esp_err_to_name(err));
}

static void battery_init(void) {
    const esp_err_t err = bsp_battery_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用,顶栏不显示电量: %s", esp_err_to_name(err));
        return;
    }
    s_battery_ready = true;
}

// 顺序照搬 main/demo_low_power.c 的 sleep_task(tests/test_pupu_deep_sleep_contract.py 检查)。
static void deep_sleep(const pupu_save_t *snapshot) {
    // 1. 同步存档:等它真的写进 Flash。写不进去也继续睡,醒来按上一次存档补算。
    if (!pupu_store_flush(snapshot, FLUSH_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "深睡前的存档没写成,继续");
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
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd.kind == CMD_BATTERY_INIT) {
            battery_init();
        } else if (cmd.kind == CMD_DEEP_SLEEP) {
            deep_sleep(&cmd.snapshot);
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

void pupu_ctl_note_key(void) {
    s_key_seen = true;
}

bool pupu_ctl_take_sleep_refused(void) {
    if (!s_sleep_refused) return false;
    s_sleep_refused = false;
    return true;
}
