#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "pupu_config.h"
#include "pupu_save_codec.h"
#include "pupu_save_policy.h"

static pupu_slot_read_t ok(uint32_t seq) {
    pupu_slot_read_t s = {.present = true, .status = PUPU_SAVE_OK, .seq = seq};
    return s;
}

static pupu_slot_read_t bad(pupu_save_status_t status) {
    pupu_slot_read_t s = {.present = true, .status = status, .seq = 0};
    return s;
}

static const pupu_slot_read_t EMPTY = {.present = false, .status = PUPU_SAVE_OK, .seq = 0};

static pupu_slot_plan_t plan2(pupu_slot_read_t a, pupu_slot_read_t b) {
    const pupu_slot_read_t slots[2] = {a, b};
    return pupu_slot_plan(slots);
}

static void test_first_boot(void) {
    const pupu_slot_plan_t p = plan2(EMPTY, EMPTY);
    assert(p.load == -1 && p.write == 0 && p.next_seq == 1);
    assert(!p.locked && !p.corrupt);  // 从没存过:新蛋,不提示"坏了"
}

static void test_newer_slot_wins(void) {
    pupu_slot_plan_t p = plan2(ok(7), ok(8));
    assert(p.load == 1 && p.write == 0 && p.next_seq == 9);
    p = plan2(ok(8), ok(7));
    assert(p.load == 0 && p.write == 1 && p.next_seq == 9);
    // 只有一个槽:读它,下次写另一个
    p = plan2(ok(1), EMPTY);
    assert(p.load == 0 && p.write == 1 && p.next_seq == 2);
    p = plan2(EMPTY, ok(2));
    assert(p.load == 1 && p.write == 0 && p.next_seq == 3);
    assert(!p.locked && !p.corrupt);
}

static void test_seq_wraps(void) {
    // 序号回绕:0 比 0xFFFFFFFF 新
    pupu_slot_plan_t p = plan2(ok(0xFFFFFFFFu), ok(0));
    assert(p.load == 1 && p.next_seq == 1);
    p = plan2(ok(0xFFFFFFFEu), ok(0xFFFFFFFFu));
    assert(p.load == 1 && p.write == 0 && p.next_seq == 0);
}

static void test_one_slot_damaged(void) {
    // 写到一半断电,一个槽坏了:读另一个,下次正好覆盖坏的那个,不算"存档坏了"
    const pupu_save_status_t kinds[] = {PUPU_SAVE_BAD_CRC, PUPU_SAVE_BAD_LENGTH,
                                        PUPU_SAVE_BAD_MAGIC, PUPU_SAVE_BAD_VERSION};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        pupu_slot_plan_t p = plan2(ok(5), bad(kinds[i]));
        assert(p.load == 0 && p.write == 1 && p.next_seq == 6 && !p.corrupt && !p.locked);
        p = plan2(bad(kinds[i]), ok(5));
        assert(p.load == 1 && p.write == 0 && p.next_seq == 6 && !p.corrupt && !p.locked);
    }
}

static void test_all_damaged(void) {
    pupu_slot_plan_t p = plan2(bad(PUPU_SAVE_BAD_CRC), bad(PUPU_SAVE_BAD_CRC));
    assert(p.load == -1 && p.corrupt && !p.locked && p.write == 0 && p.next_seq == 1);
    p = plan2(bad(PUPU_SAVE_BAD_MAGIC), EMPTY);
    assert(p.load == -1 && p.corrupt);
}

static void test_future_locks(void) {
    // 更新版本的存档:不覆盖。另一个槽能读就照常读,只是不写
    pupu_slot_plan_t p = plan2(ok(3), bad(PUPU_SAVE_FUTURE));
    assert(p.load == 0 && p.locked && !p.corrupt);
    p = plan2(bad(PUPU_SAVE_FUTURE), EMPTY);
    assert(p.load == -1 && p.locked && !p.corrupt);  // 提示"太新",不提示"坏了"
    p = plan2(bad(PUPU_SAVE_FUTURE), bad(PUPU_SAVE_BAD_CRC));
    assert(p.load == -1 && p.locked && !p.corrupt);
    // 一个 NVS 键不存在时,它的 status 不算数
    pupu_slot_read_t ghost = bad(PUPU_SAVE_FUTURE);
    ghost.present = false;
    p = plan2(ghost, ok(1));
    assert(!p.locked && p.load == 1);
}

static void test_advance_alternates(void) {
    pupu_slot_plan_t p = plan2(EMPTY, EMPTY);
    pupu_slot_advance(&p);
    assert(p.write == 1 && p.next_seq == 2);
    pupu_slot_advance(&p);
    assert(p.write == 0 && p.next_seq == 3);
}

// 用真的编码和解码模拟两个槽:每次保存写"下一个槽",中途有一次写坏。
static uint8_t s_slot[2][PUPU_SAVE_RECORD_SIZE];
static size_t s_len[2];

static pupu_slot_plan_t boot(pupu_save_t *out, int *loaded_full) {
    pupu_slot_read_t r[2];
    pupu_save_t got[2];
    for (int i = 0; i < 2; i++) {
        r[i].present = s_len[i] > 0;
        r[i].status = r[i].present ? pupu_save_decode(s_slot[i], s_len[i], &got[i], &r[i].seq)
                                   : PUPU_SAVE_OK;
    }
    const pupu_slot_plan_t p = pupu_slot_plan(r);
    *loaded_full = -1;
    if (p.load >= 0) {
        *out = got[p.load];
        *loaded_full = out->model.full;
    }
    return p;
}

static void save(pupu_slot_plan_t *p, uint8_t full) {
    pupu_save_t s;
    memset(&s, 0, sizeof(s));
    pupu_model_init(&s.model);
    s.model.full = full;
    s_len[p->write] = pupu_save_encode(&s, p->next_seq, s_slot[p->write], PUPU_SAVE_RECORD_SIZE);
    assert(s_len[p->write] == PUPU_SAVE_RECORD_SIZE);
    pupu_slot_advance(p);
}

static void test_power_cut_simulation(void) {
    memset(s_len, 0, sizeof(s_len));
    pupu_save_t s;
    int full;
    pupu_slot_plan_t p = boot(&s, &full);
    assert(full == -1);
    save(&p, 70);  // slot_a seq 1
    save(&p, 60);  // slot_b seq 2
    save(&p, 50);  // slot_a seq 3
    p = boot(&s, &full);
    assert(full == 50 && p.write == 1 && p.next_seq == 4);

    // 写 slot_b 写到一半断电:最后 4 个字节(CRC)还是旧的
    pupu_save_t next = s;
    next.model.full = 40;
    uint8_t rec[PUPU_SAVE_RECORD_SIZE];
    assert(pupu_save_encode(&next, p.next_seq, rec, sizeof(rec)) == sizeof(rec));
    memcpy(s_slot[1], rec, sizeof(rec) - 4);
    p = boot(&s, &full);
    assert(full == 50 && !p.corrupt);  // 退回上一次完整的存档
    assert(p.write == 1 && p.next_seq == 4);  // 下次正好覆盖写坏的槽

    save(&p, 30);
    p = boot(&s, &full);
    assert(full == 30 && p.write == 0 && p.next_seq == 5);
}

static void test_autosave_after_change(void) {
    pupu_autosave_t a;
    pupu_autosave_init(&a, 1000);
    assert(!pupu_autosave_due(&a, 1000, false));
    pupu_autosave_mark(&a, 2000);
    assert(!pupu_autosave_due(&a, 2000 + PUPU_SAVE_DELAY_MS - 1, false));
    assert(pupu_autosave_due(&a, 2000 + PUPU_SAVE_DELAY_MS, false));
    // 再有变化就重新等
    pupu_autosave_mark(&a, 2500);
    assert(!pupu_autosave_due(&a, 2000 + PUPU_SAVE_DELAY_MS, false));
    assert(pupu_autosave_due(&a, 2500 + PUPU_SAVE_DELAY_MS, false));
    pupu_autosave_done(&a, 3600);
    assert(!a.dirty && !pupu_autosave_due(&a, 3700, false));
}

static void test_autosave_waits_for_sound(void) {
    pupu_autosave_t a;
    pupu_autosave_init(&a, 0);
    pupu_autosave_mark(&a, 0);
    assert(!pupu_autosave_due(&a, 5000, true));  // 音效还在放:先不存
    assert(pupu_autosave_due(&a, 5050, false));  // 放完马上存
    pupu_autosave_done(&a, 5050);
    // 定时存档也等音效
    assert(!pupu_autosave_due(&a, 5050 + PUPU_SAVE_EVERY_MS, true));
    assert(pupu_autosave_due(&a, 5050 + PUPU_SAVE_EVERY_MS, false));
}

// 变暗、熄屏时马上存:不等 1 秒,但仍然等音效放完。
static void test_autosave_request_now(void) {
    pupu_autosave_t a;
    pupu_autosave_init(&a, 0);
    pupu_autosave_mark(&a, 1000);
    pupu_autosave_request(&a, 1200);
    assert(pupu_autosave_due(&a, 1200, false));
    assert(!pupu_autosave_due(&a, 1200, true));
    pupu_autosave_done(&a, 1200);
    assert(!a.dirty);
    pupu_autosave_request(&a, 0xFFFFFFFFu);  // 回绕
    assert(pupu_autosave_due(&a, 0xFFFFFFFFu, false));
    assert(pupu_autosave_due(&a, 3, false));
}

static void test_autosave_periodic(void) {
    pupu_autosave_t a;
    pupu_autosave_init(&a, 100);
    assert(!pupu_autosave_due(&a, 100 + PUPU_SAVE_EVERY_MS - 1, false));
    assert(pupu_autosave_due(&a, 100 + PUPU_SAVE_EVERY_MS, false));
    pupu_autosave_done(&a, 100 + PUPU_SAVE_EVERY_MS);
    assert(!pupu_autosave_due(&a, 100 + PUPU_SAVE_EVERY_MS + 1000, false));
    // 毫秒计数回绕(49.7 天)也照常工作
    pupu_autosave_init(&a, 0xFFFFF000u);
    assert(!pupu_autosave_due(&a, 0x00001000u, false));
    assert(pupu_autosave_due(&a, 0xFFFFF000u + PUPU_SAVE_EVERY_MS, false));
    pupu_autosave_mark(&a, 0xFFFFFF00u);
    assert(!pupu_autosave_due(&a, 0xFFFFFFFFu, false));
    assert(pupu_autosave_due(&a, 0xFFFFFF00u + PUPU_SAVE_DELAY_MS, false));
}

static void test_autosave_battery(void) {
    pupu_autosave_t a;
    pupu_autosave_init(&a, 0);
    pupu_autosave_battery(&a, -1, 10);  // 读不到电量:不管
    assert(!a.dirty);
    pupu_autosave_battery(&a, PUPU_SAVE_BATTERY_LOW, 20);
    assert(!a.dirty);
    pupu_autosave_battery(&a, PUPU_SAVE_BATTERY_LOW - 1, 30);  // 刚跌破:存一次
    assert(a.dirty && a.due_ms == 30 + PUPU_SAVE_DELAY_MS);
    pupu_autosave_done(&a, 1100);
    pupu_autosave_battery(&a, PUPU_SAVE_BATTERY_LOW - 2, 2000);  // 还在低电量:不再重复存
    assert(!a.dirty);
    pupu_autosave_battery(&a, -1, 2500);  // 读不到不改状态
    pupu_autosave_battery(&a, PUPU_SAVE_BATTERY_LOW - 3, 3000);
    assert(!a.dirty);
    pupu_autosave_battery(&a, 50, 4000);  // 充上电以后再跌破,又存一次
    pupu_autosave_battery(&a, 3, 5000);
    assert(a.dirty);
}

int main(void) {
    test_first_boot();
    test_newer_slot_wins();
    test_seq_wraps();
    test_one_slot_damaged();
    test_all_damaged();
    test_future_locks();
    test_advance_alternates();
    test_power_cut_simulation();
    test_autosave_after_change();
    test_autosave_waits_for_sound();
    test_autosave_request_now();
    test_autosave_periodic();
    test_autosave_battery();
    return 0;
}
