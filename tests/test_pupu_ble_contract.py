#!/usr/bin/env python3
"""Static contracts for Pupu's Bluetooth sync window (main/pupu_ble.c and its callers).

The NimBLE traps these guard against were all hit on this board or in the community notes
(development plan section 8): stop from a callback deadlocks, a characteristic without an
access_cb fails registration with rc=3, notifications before SUBSCRIBE are dropped, a
connection silently ends legacy advertising. Standard library only; Python 3.9 compatible.
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


def code_only(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


# Functions that run on the NimBLE host task (GAP/GATT callbacks and what they call).
HOST_TASK_FUNCTIONS = ("gap_event", "gatt_access", "on_sync", "on_reset", "post",
                       "readvertise", "advertise", "set_link")
FORBIDDEN_ON_HOST_TASK = ("pupu_ble_stop(", "nimble_port_stop(", "nimble_port_deinit(",
                          "lv_", "bsp_lvgl_lock(", "nvs_", "pupu_store_", "pupu_model_",
                          "pupu_sync_handle(", "vTaskDelay(")


class PupuBleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ble = code_only(read("main/pupu_ble.c"))
        cls.ctl = code_only(read("main/pupu_ctl.c"))
        cls.page = code_only(read("main/pupu_ui_sync.c"))
        cls.app = code_only(read("main/pupu_app.c"))

    def test_host_task_callbacks_only_copy_and_queue(self):
        for name in HOST_TASK_FUNCTIONS:
            body = function_body(self.ble, name)
            for call in FORBIDDEN_ON_HOST_TASK:
                self.assertNotIn(call, body, "{} must not call {}".format(name, call))

    def test_every_characteristic_has_an_access_callback_and_no_encryption(self):
        table = self.ble[self.ble.index("k_services[]"):]
        table = table[:table.index("};") + 2]
        characteristics = re.findall(r"\{\s*\.uuid\s*=\s*&k_nus_(?:rx|tx)\.u,(.*?)\}", table, flags=re.S)
        self.assertEqual(len(characteristics), 2)
        for body in characteristics:
            self.assertIn(".access_cb = gatt_access", body)
        self.assertNotIn("_ENC", table)  # v1 does not pair
        tx = characteristics[1]
        self.assertIn("BLE_GATT_CHR_F_NOTIFY", tx)
        self.assertIn(".val_handle = &s_tx_handle", tx)

    def test_nus_uuids_are_little_endian(self):
        for suffix, short in (("svc", "0x01"), ("rx", "0x02"), ("tx", "0x03")):
            match = re.search(r"k_nus_" + suffix + r"\s*=\s*BLE_UUID128_INIT\((.*?)\);", self.ble, re.S)
            self.assertIsNotNone(match, suffix)
            octets = [b.strip() for b in match.group(1).split(",")]
            self.assertEqual(len(octets), 16)
            text = "".join(o[2:] for o in reversed(octets))
            self.assertEqual(text, "6e4000" + short[2:].zfill(2) + "b5a3f393e0a9e50e24dcca9e")

    def test_notifications_wait_for_subscribe(self):
        send = function_body(self.ble, "pupu_ble_send")
        self.assertIn("subscribed", send)
        self.assertLess(send.index("subscribed"), send.index("ble_gatts_notify_custom("))
        gap = function_body(self.ble, "gap_event")
        self.assertIn("BLE_GAP_EVENT_SUBSCRIBE", gap)
        self.assertIn("s_tx_handle", gap)

    def test_connectable_and_readvertises_after_disconnect(self):
        adv = function_body(self.ble, "advertise")
        self.assertIn("BLE_GAP_CONN_MODE_UND", adv)
        self.assertIn("ble_gap_adv_rsp_set_fields(", adv)  # the name lives in the scan response
        gap = function_body(self.ble, "gap_event")
        disconnect = gap[gap.index("BLE_GAP_EVENT_DISCONNECT"):gap.index("BLE_GAP_EVENT_SUBSCRIBE")]
        self.assertIn("readvertise()", disconnect)

    def test_start_and_stop_only_from_the_worker_task(self):
        for source, name in ((self.page, "pupu_ui_sync.c"), (self.app, "pupu_app.c")):
            self.assertNotIn("pupu_ble_start(", source, name)
            self.assertNotIn("pupu_ble_stop(", source, name)
        self.assertIn("pupu_ble_start()", function_body(self.ctl, "ble_start"))
        stop = function_body(self.ctl, "ble_stop")
        self.assertIn("pupu_ble_stop()", stop)
        self.assertNotIn("bsp_lvgl_lock(", stop)  # stopping can take ~2 s with a phone attached

    def test_stop_handshake_matches_demo_ble(self):
        stop = function_body(self.ble, "pupu_ble_stop")
        order = ["s_running = false", "nimble_port_stop()", "xSemaphoreTake(s_host_stopped",
                 "vTaskDelete(s_host_task)", "nimble_port_deinit()"]
        positions = [stop.index(step) for step in order]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("STOP_TIMEOUT_MS", stop)

    def test_radio_is_off_before_deep_sleep(self):
        sleep = function_body(self.ctl, "deep_sleep")
        stop = sleep.index("pupu_ble_stop()")
        # before the key check, so a key pressed during the (up to 2 s) stop still cancels sleep
        self.assertLess(stop, sleep.index("bsp_button_read_mv()"))
        self.assertLess(stop, sleep.index("esp_deep_sleep_start()"))
        # a radio that will not stop means restart, never sleep with it on
        failed = sleep[stop:sleep.index("bsp_button_read_mv()")]
        self.assertIn("esp_restart()", failed)

    def test_half_deinit_is_never_retried(self):
        stop = function_body(self.ble, "pupu_ble_stop")
        self.assertLess(stop.index("if (s_broken) return"), stop.index("nimble_port_deinit()"))
        self.assertIn("s_broken = true", stop[stop.index("nimble_port_deinit()"):])
        ctl_stop = function_body(self.ctl, "ble_stop")
        self.assertIn("pupu_ble_broken()", ctl_stop)
        self.assertIn("esp_restart()", ctl_stop)

    def test_sync_window_blocks_dim_and_sleep(self):
        busy = function_body(self.app, "app_busy")
        self.assertIn("PAGE_SYNC", busy)
        self.assertIn("pupu_ui_sync_busy()", busy)
        page_busy = function_body(self.page, "pupu_ui_sync_busy")
        self.assertIn("s_stuck", page_busy)  # a radio that failed to stop keeps the screen on

    def test_closing_stops_the_radio_listening_at_once(self):
        close = function_body(self.page, "begin_close")
        self.assertLess(close.index("pupu_ble_close_soon()"), close.index("pupu_ctl_ble_stop()"))
        self.assertIn("s_running = false", function_body(self.ble, "pupu_ble_close_soon"))
        access = function_body(self.ble, "gatt_access")
        self.assertIn("!s_running", access)

    def test_events_wait_until_the_page_knows_bluetooth_is_on(self):
        events = function_body(self.page, "handle_events")
        self.assertLess(events.index("PH_STARTING"), events.index("pupu_ble_poll("))
        start = function_body(self.ble, "pupu_ble_start")
        self.assertLess(start.index("xQueueReset(s_events)"), start.index("xTaskCreatePinnedToCore("))

    def test_connection_reattempt_is_off(self):
        # With it on, a failed connection setup is swallowed (no DISCONNECT) and NimBLE's own
        # legacy re-advertise always fails in IDF 5.5.3, so the device would vanish.
        defaults = read("sdkconfig.defaults")
        self.assertRegex(defaults, r"(?m)^CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT=n$")
        self.assertRegex(defaults, r"(?m)^CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1$")

    def test_subscribe_before_the_deferred_connect_event_is_kept(self):
        gap = function_body(self.ble, "gap_event")
        subscribe = gap[gap.index("BLE_GAP_EVENT_SUBSCRIBE"):gap.index("BLE_GAP_EVENT_MTU")]
        self.assertIn("if (s_conn == BLE_HS_CONN_HANDLE_NONE) s_conn = event->subscribe.conn_handle", subscribe)
        connect = gap[gap.index("BLE_GAP_EVENT_CONNECT"):gap.index("BLE_GAP_EVENT_DISCONNECT")]
        self.assertIn("early_sub", connect)

    def test_no_worker_means_nothing_to_stop(self):
        send = function_body(self.ctl, "send_ble")
        no_queue = send[send.index("if (s_queue == NULL)"):send.index("s_ble = pending")]
        self.assertIn("CMD_BLE_STOP ? PUPU_CTL_BLE_OFF", no_queue)

    def test_waking_on_a_closed_sync_page_goes_home(self):
        power = function_body(self.app, "apply_power")
        wake = power[power.index("PUPU_PWR_DO_WAKE"):power.index("PUPU_PWR_DO_SLEEP")]
        self.assertIn("s_page == PAGE_SYNC", wake)
        self.assertIn("PUPU_UI_GO_HOME", wake)

    def test_battery_retry_waits_while_bluetooth_is_on(self):
        task = function_body(self.ctl, "ctl_task")
        timeout = task[task.index("if (xQueueReceive"):task.index("switch (cmd.kind)")]
        self.assertLess(timeout.index("s_ble != PUPU_CTL_BLE_OFF"), timeout.index("battery_init()"))


if __name__ == "__main__":
    unittest.main()
