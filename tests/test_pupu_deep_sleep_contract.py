#!/usr/bin/env python3
"""Static contracts for Pupu's deep-sleep path in main/pupu_ctl.c and its wiring in pupu_app.c.

Mirrors tests/test_deep_sleep_contract.py, which checks the same terminal order in the
(no longer compiled) Low Power demo. Standard library only; Python 3.9 compatible.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source)
    if not match:
        raise AssertionError("function not found: " + name)
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError("function is unterminated: " + name)


def code_only(body):
    """Drop // and /* */ comments so the order checks only see real calls."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return re.sub(r"//[^\n]*", "", body)


class PupuDeepSleepContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ctl = read("main/pupu_ctl.c")
        cls.app = read("main/pupu_app.c")
        cls.sleep = code_only(function_body(cls.ctl, "deep_sleep"))

    def test_terminal_shutdown_order(self):
        calls = [
            "pupu_store_flush(",
            "bsp_button_read_mv()",
            "bsp_button_prepare_deep_sleep(",
            "esp_deep_sleep_enable_gpio_wakeup(",
            "bsp_battery_sleep()",
            "pupu_sfx_stop(",
            "bsp_audio_sleep()",
            "bsp_audio_prepare_deep_sleep()",
            "bsp_i2c_prepare_deep_sleep()",
            "bsp_lvgl_lock(1000)",
            "bsp_display_prepare_deep_sleep()",
            "esp_deep_sleep_start()",
        ]
        positions = [self.sleep.index(call) for call in calls]
        self.assertEqual(positions, sorted(positions))
        # If deep sleep ever returns, the peripherals are already down: restart.
        self.assertIn("esp_restart()", self.sleep[self.sleep.index("esp_deep_sleep_start()"):])

    def test_held_key_refuses_before_the_point_of_no_return(self):
        check = self.sleep.index("bsp_button_read_mv()")
        handoff = self.sleep.index("bsp_button_prepare_deep_sleep(")
        between = self.sleep[check:handoff]
        self.assertIn("BSP_BTN_MV_RELEASED_MIN", between)
        self.assertIn("s_sleep_refused = true", between)
        self.assertIn("return", between)

    def test_wake_source_is_the_key_pin_mask_active_low(self):
        self.assertRegex(
            self.sleep,
            r"esp_deep_sleep_enable_gpio_wakeup\(\s*1ULL\s*<<\s*BSP_BTN_GPIO\s*,\s*"
            r"ESP_GPIO_WAKEUP_GPIO_LOW\s*\)",
        )
        self.assertNotIn("esp_sleep_enable_timer_wakeup", self.ctl)

    def test_never_stops_the_lvgl_port(self):
        # The main loop must keep running while the screen is off (development plan 5.3).
        self.assertNotIn("lvgl_port_stop", self.ctl)
        self.assertNotIn("lvgl_port_stop", self.app)

    def test_app_stops_touching_peripherals_before_handing_off(self):
        body = code_only(function_body(self.app, "apply_power"))
        self.assertLess(body.index("s_sleeping = true"), body.index("pupu_ctl_deep_sleep("))
        loop = code_only(function_body(self.app, "loop_cb"))
        guard = loop.index("if (s_sleeping)")
        self.assertLess(guard, loop.index("bsp_battery_soc()"))
        self.assertLess(guard, loop.index("save_now("))


if __name__ == "__main__":
    unittest.main()
