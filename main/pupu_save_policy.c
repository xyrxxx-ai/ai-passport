#include "pupu_save_policy.h"

#include "pupu_config.h"

static bool readable(const pupu_slot_read_t *s) {
    return s->present && s->status == PUPU_SAVE_OK;
}

pupu_slot_plan_t pupu_slot_plan(const pupu_slot_read_t slots[2]) {
    pupu_slot_plan_t p = {.load = -1, .write = 0, .next_seq = 1};
    const bool a = readable(&slots[0]);
    const bool b = readable(&slots[1]);
    if (a && b) {
        p.load = (int32_t)(slots[1].seq - slots[0].seq) > 0 ? 1 : 0;
    } else if (a) {
        p.load = 0;
    } else if (b) {
        p.load = 1;
    }
    if (p.load >= 0) {
        p.write = (uint8_t)(1 - p.load);
        p.next_seq = slots[p.load].seq + 1;
    }
    for (int i = 0; i < 2; i++) {
        if (slots[i].present && slots[i].status == PUPU_SAVE_FUTURE) p.locked = true;
    }
    p.corrupt = p.load < 0 && !p.locked && (slots[0].present || slots[1].present);
    return p;
}

void pupu_slot_advance(pupu_slot_plan_t *p) {
    p->write ^= 1;
    p->next_seq++;
}

void pupu_autosave_init(pupu_autosave_t *a, uint32_t now_ms) {
    a->dirty = false;
    a->battery_low = false;
    a->due_ms = now_ms;
    a->last_ms = now_ms;
}

void pupu_autosave_mark(pupu_autosave_t *a, uint32_t now_ms) {
    a->dirty = true;
    a->due_ms = now_ms + PUPU_SAVE_DELAY_MS;
}

void pupu_autosave_battery(pupu_autosave_t *a, int soc, uint32_t now_ms) {
    if (soc < 0) return;
    const bool low = soc < PUPU_SAVE_BATTERY_LOW;
    if (low && !a->battery_low) pupu_autosave_mark(a, now_ms);
    a->battery_low = low;
}

bool pupu_autosave_due(const pupu_autosave_t *a, uint32_t now_ms, bool busy) {
    if (busy) return false;
    if (a->dirty && (int32_t)(now_ms - a->due_ms) >= 0) return true;
    return now_ms - a->last_ms >= PUPU_SAVE_EVERY_MS;
}

void pupu_autosave_done(pupu_autosave_t *a, uint32_t now_ms) {
    a->dirty = false;
    a->last_ms = now_ms;
}
