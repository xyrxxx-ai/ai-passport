#include "pupu_store.h"

#include "pupu_save_policy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <inttypes.h>

static const char *TAG = "pupu_store";

#define NAMESPACE "pupu"
#define TASK_PRIORITY 2  // 比 LVGL(4)和音效(5)都低
#define TASK_STACK 3072
// 读槽用的缓冲区。比现在的记录(67 字节)大很多,给以后的版本留余地;
// 比它还大的记录只可能来自更新的固件,当作"太新"处理,不覆盖。
#define READ_MAX 256

static const char *const k_keys[2] = {"slot_a", "slot_b"};

// 每份快照带一个编号,用来判断"写完了没有"(pupu_store_busy / pupu_store_flush)。
typedef struct {
    pupu_save_t save;
    uint32_t gen;
} job_t;

static nvs_handle_t s_nvs;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_written;  // 存档任务每处理完一份快照给一次
static pupu_slot_plan_t s_plan;      // 启动后只有存档任务在用
static bool s_ready;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_gen_queued;  // 最新交出去的编号(s_lock 保护)
static uint32_t s_gen_done;    // 存档任务处理完的编号(s_lock 保护)
static uint32_t s_gen_ok;      // 最近一次写成功的编号(s_lock 保护)

static pupu_slot_read_t read_slot(int i, pupu_save_t *out) {
    pupu_slot_read_t r = {.present = true, .status = PUPU_SAVE_BAD_LENGTH, .seq = 0};
    static uint8_t buf[READ_MAX];  // 只在开机时用一次;放静态区,不占 app_main 的栈
    size_t len = sizeof(buf);
    const esp_err_t err = nvs_get_blob(s_nvs, k_keys[i], buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        r.present = false;
    } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGW(TAG, "%s 有 %u 字节,比这个固件认识的都大,当作更新版本的存档", k_keys[i],
                 (unsigned)len);
        r.status = PUPU_SAVE_FUTURE;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "读 %s 失败: %s", k_keys[i], esp_err_to_name(err));
    } else {
        r.status = pupu_save_decode(buf, len, out, &r.seq);
    }
    if (r.present) {
        ESP_LOGI(TAG, "%s: 结果 %d 序号 %" PRIu32 " (%u 字节)", k_keys[i], (int)r.status, r.seq,
                 (unsigned)len);
    }
    return r;
}

static void save_task(void *arg) {
    (void)arg;
    job_t job;
    uint8_t rec[PUPU_SAVE_RECORD_SIZE];
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) continue;
        const size_t n = pupu_save_encode(&job.save, s_plan.next_seq, rec, sizeof(rec));
        const char *key = k_keys[s_plan.write];
        esp_err_t err = n > 0 ? nvs_set_blob(s_nvs, key, rec, n) : ESP_ERR_INVALID_SIZE;
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        if (err != ESP_OK) {
            // 下次还写同一个槽;绝不为了腾地方擦除 NVS。
            ESP_LOGE(TAG, "写 %s 失败: %s", key, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "已存档 %s 序号 %" PRIu32, key, s_plan.next_seq);
            pupu_slot_advance(&s_plan);
        }
        taskENTER_CRITICAL(&s_lock);
        s_gen_done = job.gen;
        if (err == ESP_OK) s_gen_ok = job.gen;
        taskEXIT_CRITICAL(&s_lock);
        xSemaphoreGive(s_written);
    }
}

pupu_store_boot_t pupu_store_start(pupu_save_t *out) {
    pupu_store_boot_t boot = {0};
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 失败: %s;这次开机不能存档", esp_err_to_name(err));
        return boot;
    }
    static pupu_save_t got[2];
    pupu_slot_read_t slots[2];
    for (int i = 0; i < 2; i++) slots[i] = read_slot(i, &got[i]);
    s_plan = pupu_slot_plan(slots);
    boot.corrupt = s_plan.corrupt;
    boot.locked = s_plan.locked;
    if (s_plan.load >= 0) {
        *out = got[s_plan.load];
        boot.loaded = true;
    }
    ESP_LOGI(TAG, "读 %s,下次写 %s 序号 %" PRIu32 "%s%s",
             s_plan.load >= 0 ? k_keys[s_plan.load] : "(无)", k_keys[s_plan.write],
             s_plan.next_seq, s_plan.corrupt ? ",存档坏了" : "",
             s_plan.locked ? ",有更新版本的存档,这次不写" : "");

    s_queue = xQueueCreate(1, sizeof(job_t));
    s_written = xSemaphoreCreateBinary();
    if (s_queue == NULL || s_written == NULL ||
        xTaskCreate(save_task, "pupu_store", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "存档任务启动失败;这次开机不能存档");
        return boot;
    }
    s_ready = true;
    boot.ready = true;
    return boot;
}

// 交出一份快照,返回它的编号;不能存档时返回 0。
static uint32_t enqueue(const pupu_save_t *s) {
    if (!s_ready || s_plan.locked) return 0;
    job_t job = {.save = *s};
    taskENTER_CRITICAL(&s_lock);
    job.gen = ++s_gen_queued;
    if (job.gen == 0) job.gen = ++s_gen_queued;  // 0 留给"没有交出去"
    taskEXIT_CRITICAL(&s_lock);
    xQueueOverwrite(s_queue, &job);
    return job.gen;
}

void pupu_store_save(const pupu_save_t *s) {
    (void)enqueue(s);
}

bool pupu_store_busy(void) {
    taskENTER_CRITICAL(&s_lock);
    const bool busy = s_gen_done != s_gen_queued;
    taskEXIT_CRITICAL(&s_lock);
    return busy;
}

bool pupu_store_flush(const pupu_save_t *s, uint32_t timeout_ms) {
    const uint32_t gen = enqueue(s);
    if (gen == 0) return false;
    const TickType_t start = xTaskGetTickCount();
    const TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        taskENTER_CRITICAL(&s_lock);
        const bool done = (int32_t)(s_gen_done - gen) >= 0;
        const bool ok = s_gen_ok == gen;
        taskEXIT_CRITICAL(&s_lock);
        if (done) return ok;
        const TickType_t spent = xTaskGetTickCount() - start;
        if (spent >= limit || xSemaphoreTake(s_written, limit - spent) != pdTRUE) {
            ESP_LOGE(TAG, "等存档写完超时(%" PRIu32 " ms)", timeout_ms);
            return false;
        }
    }
}
