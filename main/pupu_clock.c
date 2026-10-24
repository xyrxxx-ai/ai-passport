#include "pupu_clock.h"

#include "pupu_config.h"

#define US_PER_S 1000000

void pupu_clock_init(pupu_clock_t *c, uint32_t scale, uint32_t saved_acct_epoch) {
    c->scale = scale;
    c->acct_now = saved_acct_epoch;
    c->real_rem_us = 0;
}

uint32_t pupu_clock_tick(pupu_clock_t *c, int64_t real_delta_us) {
    c->real_rem_us += real_delta_us;
    uint32_t real_s = (uint32_t)(c->real_rem_us / US_PER_S);
    c->real_rem_us %= US_PER_S;
    if (c->acct_now != 0) c->acct_now += real_s;
    return real_s * c->scale;
}

uint64_t pupu_clock_to_pet(const pupu_clock_t *c, uint64_t real_s) {
    return real_s * c->scale;
}

bool pupu_clock_reset_keeps_rtc(pupu_reset_reason_t reason) {
    return reason != PUPU_RESET_POWERON
        && reason != PUPU_RESET_BROWNOUT
        && reason != PUPU_RESET_UNKNOWN;
}

bool pupu_clock_boot(pupu_clock_t *c, const pupu_boot_info_t *boot, uint64_t *away_real_s) {
    *away_real_s = 0;
    if (!pupu_clock_reset_keeps_rtc(boot->reason)) return false;
    if (boot->saved_token == 0) return false;  // 0 不是有效令牌(M3 及更早的存档都写 0)
    if (boot->saved_token != boot->rtc_token) return false;
    if (boot->now_rtc_us < boot->saved_rtc_us) return false;

    *away_real_s = (boot->now_rtc_us - boot->saved_rtc_us) / US_PER_S;
    if (c->acct_now != 0) c->acct_now += (uint32_t)*away_real_s;
    return true;
}

bool pupu_clock_sync(pupu_clock_t *c, uint32_t phone_epoch, uint64_t *away_real_s) {
    *away_real_s = 0;
    if (phone_epoch < PUPU_EPOCH_MIN) return false;
    if (c->acct_now != 0 && phone_epoch > c->acct_now) {
        *away_real_s = phone_epoch - c->acct_now;
    }
    c->acct_now = phone_epoch;
    return true;
}
