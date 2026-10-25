#include "pupu_sfx.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "pupu_sfx";

#define SAMPLE_RATE 16000u
#define CHUNK_SAMPLES 256u
#define QUEUE_DEPTH 4
#define TASK_PRIORITY 5  // 比 LVGL 任务(4)高,见头文件
#define TASK_STACK 3072
#define VOLUME 55
#define AMPLITUDE 2700
// BSP 的 I2S 有 6 × 240 帧 DMA 缓冲,16 kHz 下最多 90 ms;写完最后一块后再算忙一会儿。
#define TAIL_US 150000
#define STOP_MARK 0xFF

enum { SQUARE = 0, TRIANGLE, NOISE, SILENT };

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_stopped;
static volatile bool s_stop;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_pending;  // 排队和正在放的音效个数(s_lock 保护)
static int64_t s_end_us;    // 最后一次写完 I2S 的时间(s_lock 保护)
static uint32_t s_noise = 0xC001D00Du;

static int16_t wave_sample(uint32_t phase, uint8_t wave, int16_t amplitude) {
    if (wave == SILENT) return 0;
    const uint16_t point = (uint16_t)(phase >> 16);
    if (wave == SQUARE) return point < 32768u ? amplitude : (int16_t)-amplitude;
    if (wave == TRIANGLE) {
        const int32_t tri = point < 32768u ? (int32_t)point * 2 - 32768 : 98303 - (int32_t)point * 2;
        return (int16_t)(tri * amplitude / 32768);
    }
    s_noise ^= s_noise << 13;
    s_noise ^= s_noise >> 17;
    s_noise ^= s_noise << 5;
    return (int16_t)(((int32_t)(s_noise & 0xFFFFu) - 32768) * amplitude / 32768);
}

// 一个音:开头和结尾各有 1/8 长度的淡入淡出,避免咔哒声。噪声不看 hz。被 stop 打断时返回 false。
static bool tone(uint16_t hz, uint16_t ms, uint8_t wave) {
    const uint32_t total = SAMPLE_RATE * ms / 1000u;
    const uint32_t edge = total / 8u + 1u;
    const uint32_t step = (uint32_t)(((uint64_t)hz << 32) / SAMPLE_RATE);
    uint32_t phase = 0;
    int16_t samples[CHUNK_SAMPLES];
    for (uint32_t done = 0; done < total;) {
        if (s_stop) return false;
        const uint32_t n = total - done < CHUNK_SAMPLES ? total - done : CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t pos = done + i;
            const uint32_t env = pos < edge ? pos * 100u / edge
                                 : total - pos < edge ? (total - pos) * 100u / edge : 100u;
            samples[i] = wave_sample(phase, wave, (int16_t)(AMPLITUDE * env / 100u));
            phase += step;
        }
        if (bsp_audio_write(samples, n * sizeof(samples[0])) != ESP_OK) return false;
        done += n;
    }
    return true;
}

// 两个音之间的停顿:写进静音采样,排在前面的音后面。只睡觉的话,DMA 里还排着 70–90 ms
// 前面的音,停顿就听不出来了。
static bool rest(uint16_t ms) {
    return tone(0, ms, SILENT);
}

// 滑音:频率从 hz_from 匀速滑到 hz_to(每 256 个采样换一次频率,听不出台阶)。叹气、狼嚎用。
static bool slide(uint16_t hz_from, uint16_t hz_to, uint16_t ms, uint8_t wave) {
    const uint32_t total = SAMPLE_RATE * ms / 1000u;
    const uint32_t edge = total / 8u + 1u;
    uint32_t phase = 0;
    int16_t samples[CHUNK_SAMPLES];
    for (uint32_t done = 0; done < total;) {
        if (s_stop) return false;
        const uint32_t n = total - done < CHUNK_SAMPLES ? total - done : CHUNK_SAMPLES;
        const int32_t hz = hz_from + ((int32_t)hz_to - (int32_t)hz_from) * (int32_t)done / (int32_t)total;
        const uint32_t step = (uint32_t)(((uint64_t)(uint32_t)hz << 32) / SAMPLE_RATE);
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t pos = done + i;
            const uint32_t env = pos < edge ? pos * 100u / edge
                                 : total - pos < edge ? (total - pos) * 100u / edge : 100u;
            samples[i] = wave_sample(phase, wave, (int16_t)(AMPLITUDE * env / 100u));
            phase += step;
        }
        if (bsp_audio_write(samples, n * sizeof(samples[0])) != ESP_OK) return false;
        done += n;
    }
    return true;
}

static void play(pupu_sfx_t sfx) {
    switch (sfx) {
    case PUPU_SFX_EAT:  // 三口:咔嚓、咔嚓、咕
        (void)(tone(0, 35, NOISE) && rest(70) && tone(0, 35, NOISE) && rest(70) &&
               tone(392, 70, TRIANGLE));
        break;
    case PUPU_SFX_HAPPY:  // "噗"一声,再往上扬
        (void)(tone(0, 50, NOISE) && rest(30) && tone(660, 70, TRIANGLE) &&
               tone(880, 120, TRIANGLE));
        break;
    case PUPU_SFX_NO:  // 往下掉的两声
        (void)(tone(330, 90, SQUARE) && tone(247, 150, SQUARE));
        break;
    case PUPU_SFX_HATCH:
        (void)(tone(523, 80, TRIANGLE) && tone(659, 80, TRIANGLE) && tone(784, 80, TRIANGLE) &&
               tone(1047, 180, TRIANGLE));
        break;
    case PUPU_SFX_GROW:
        (void)(tone(392, 90, SQUARE) && tone(523, 90, TRIANGLE) && tone(659, 90, SQUARE) &&
               tone(784, 90, TRIANGLE) && tone(1047, 240, TRIANGLE));
        break;
    case PUPU_SFX_BEEP:
        (void)tone(880, 60, SQUARE);
        break;
    case PUPU_SFX_WIN:
        (void)(tone(523, 90, TRIANGLE) && tone(659, 90, TRIANGLE) && tone(784, 90, TRIANGLE) &&
               rest(40) && tone(1047, 120, SQUARE) && tone(1319, 220, TRIANGLE));
        break;
    case PUPU_SFX_LOSE:
        (void)(tone(392, 120, TRIANGLE) && tone(330, 120, TRIANGLE) && tone(262, 220, TRIANGLE));
        break;
    case PUPU_SFX_SYNC:  // "叮——":比接星星的"叮"长,高音拖一下
        (void)(tone(1568, 60, TRIANGLE) && rest(20) && tone(2093, 260, TRIANGLE));
        break;
    case PUPU_SFX_PAT:  // "啪":一点噪声加一个很短的低音
        (void)(tone(0, 20, NOISE) && tone(220, 50, TRIANGLE));
        break;
    case PUPU_SFX_ANNOYED:  // "哼":鼻音似的两声,比 NO 短、不那么低
        (void)(tone(311, 60, TRIANGLE) && tone(262, 140, TRIANGLE));
        break;
    case PUPU_SFX_ANGRY:  // 三连降
        (void)(tone(523, 80, SQUARE) && tone(440, 80, SQUARE) && tone(349, 170, SQUARE));
        break;
    case PUPU_SFX_COIN:  // "叮叮":两声一样高的叮,再往上一闪
        (void)(tone(1760, 45, TRIANGLE) && rest(45) && tone(1760, 45, TRIANGLE) && rest(20) &&
               tone(2349, 110, TRIANGLE));
        break;
    case PUPU_SFX_THUD:  // "砰":噪声 40 ms + 110 Hz 方波 160 ms,比 CLOUD 更低更长
        (void)(tone(0, 40, NOISE) && tone(110, 160, SQUARE));
        break;
    case PUPU_SFX_SIGH:  // 叹气:往下滑的长音
        (void)slide(440, 220, 420, TRIANGLE);
        break;
    case PUPU_SFX_CARD_GOOD:  // 短短的往上两声
        (void)(tone(988, 50, TRIANGLE) && tone(1319, 90, TRIANGLE));
        break;
    case PUPU_SFX_CARD_BAD:  // 低低的"嗯哼"
        (void)(tone(220, 70, SQUARE) && tone(185, 120, SQUARE));
        break;
    case PUPU_SFX_HOWL:  // 狼嚎:滑上去再滑下来
        (void)(slide(392, 659, 350, TRIANGLE) && slide(659, 440, 450, TRIANGLE));
        break;
    default:
        break;
    }
}

static void sfx_task(void *arg) {
    (void)arg;
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "设置音频格式失败,这次开机没有音效");
    }
    for (;;) {
        uint8_t req;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        if (req == STOP_MARK) {
            // 前面排队的都已经跳过,正在放的也切断了:从此不再写 I2S。
            xSemaphoreGive(s_stopped);
            continue;
        }
        if (!s_stop) play((pupu_sfx_t)req);
        taskENTER_CRITICAL(&s_lock);
        s_pending--;
        s_end_us = esp_timer_get_time();
        taskEXIT_CRITICAL(&s_lock);
    }
}

esp_err_t pupu_sfx_start(void) {
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) return err;
    bsp_audio_set_volume(VOLUME);
    QueueHandle_t queue = xQueueCreate(QUEUE_DEPTH, sizeof(uint8_t));
    s_stopped = xSemaphoreCreateBinary();
    if (queue == NULL || s_stopped == NULL) return ESP_ERR_NO_MEM;
    s_queue = queue;
    if (xTaskCreate(sfx_task, "pupu_sfx", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        // 没有任务消费队列时,pupu_sfx_busy() 会一直是 true,存档就永远等不到:
        // 所以退回"没有音效"的状态。
        s_queue = NULL;
        vQueueDelete(queue);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pupu_sfx_play(pupu_sfx_t sfx) {
    if (s_queue == NULL || s_stop || sfx >= PUPU_SFX_COUNT) return;
    const uint8_t req = (uint8_t)sfx;
    taskENTER_CRITICAL(&s_lock);
    s_pending++;
    taskEXIT_CRITICAL(&s_lock);
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        taskENTER_CRITICAL(&s_lock);
        s_pending--;
        taskEXIT_CRITICAL(&s_lock);
    }
}

bool pupu_sfx_busy(void) {
    if (s_queue == NULL) return false;
    taskENTER_CRITICAL(&s_lock);
    const uint32_t pending = s_pending;
    const int64_t end_us = s_end_us;
    taskEXIT_CRITICAL(&s_lock);
    return pending > 0 || (end_us != 0 && esp_timer_get_time() - end_us < TAIL_US);
}

bool pupu_sfx_stop(uint32_t timeout_ms) {
    if (s_queue == NULL) return true;
    s_stop = true;
    const uint8_t mark = STOP_MARK;
    if (xQueueSend(s_queue, &mark, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    return xSemaphoreTake(s_stopped, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
