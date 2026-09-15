"""Runs the SDK against the compiled firmware transaction and CCM service."""
import os
from pathlib import Path
import subprocess
import threading
import unittest
from copy import deepcopy
from makxd.settings import DeviceConfiguration, SettingsError, SettingsSection, ControllerChannel


class Firmware:
    def __init__(self):
        self.process = subprocess.Popen([os.environ["MAKXD_SETTINGS_PEER"]], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True, bufsize=1)
        self.configuration_lock = threading.RLock()

    def send_mak_api(self, opcode, payload=b"", **_):
        assert opcode == 0x3e
        with self.configuration_lock:
            self.process.stdin.write(payload.hex()+"\n"); self.process.stdin.flush()
            reply = self.process.stdout.readline()
            if not reply:
                raise RuntimeError("Firmware fixture stopped")
            return bytes.fromhex(reply)

    def close(self):
        self.process.stdin.close(); self.process.wait(timeout=5); self.process.stdout.close()


@unittest.skipUnless(os.environ.get("MAKXD_SETTINGS_PEER"), "Compiled firmware peer required")
class DeviceSettingsTests(unittest.TestCase):
    def setUp(self):
        self.wire = Firmware(); self.api = DeviceConfiguration(self.wire)

    def tearDown(self):
        self.wire.close()

    def saved_count(self):
        return int.from_bytes(self.wire.send_mak_api(0x3e, b"\xf1"), "little")

    def test_controller_hash_presets_persist_and_stay_hidden(self):
        key=bytes.fromhex("0102030405060708090a0b0c0d0e0f10")
        value=self.api.read();value.settings.controller.buffer_ms=21
        value=self.api.apply(value,SettingsSection.CONTROLLER)
        self.api.save_controller_preset(key,value)
        self.assertEqual(self.api.read_controller_preset(key).controller.buffer_ms,21)
        public=self.wire.send_mak_api(0x3e,b"\x1e\x00")
        self.assertEqual(public[5:10],bytes(5))
        value.settings.controller.buffer_ms=24
        value=self.api.apply(value,SettingsSection.CONTROLLER)
        self.assertEqual(self.api.read_controller_preset(key).controller.buffer_ms,21)
        self.api.save_controller_preset(key,value)
        self.wire.send_mak_api(0x3e,b"\xf0")
        self.assertEqual(self.api.read().settings.controller.buffer_ms,24)
        self.assertEqual(self.api.load_controller_preset(key).settings.controller.buffer_ms,24)
        with self.assertRaises(SettingsError) as missing:
            self.api.load_controller_preset(bytes([2])*16)
        self.assertEqual(missing.exception.status,7)
        with self.assertRaises(ValueError):self.api.save_controller_preset(bytes(16))

    def test_live_save_and_export_are_separate(self):
        original = self.api.read()
        tuned = deepcopy(original)
        tuned.settings.controller.behavior(ControllerChannel.RIGHT_STICK).strength_percent = 73
        tuned.settings.controller.buffer_ms = 13
        tuned.settings.translation[1].scale = 9
        tuned.settings.mouse_spread_percent = 81
        applied = self.api.apply(tuned)
        self.assertEqual(applied.settings.controller.buffer_ms, 13)
        self.assertEqual(applied.settings.controller.behavior(ControllerChannel.RIGHT_STICK).strength_percent, 73)
        self.assertEqual(applied.settings.translation[1].scale, 9)
        self.assertEqual(applied.settings.mouse_spread_percent, 81)
        self.assertEqual(self.saved_count(), 0)
        exported = self.api.export_preset(applied)
        self.assertTrue(exported.startswith(b"MKSE\x01\x01"))
        self.assertNotEqual(exported, self.api.export_preset(applied))
        self.assertEqual(self.saved_count(), 0, "Export persisted live settings")
        self.wire.send_mak_api(0x3e, b"\xf0")
        self.assertEqual(self.api.read().settings.controller.buffer_ms, original.settings.controller.buffer_ms)
        imported = self.api.import_preset(exported)
        self.assertEqual(imported.settings.controller.buffer_ms, 13)
        self.assertEqual(self.saved_count(), 0, "Import persisted without Save")
        self.api.save(imported)
        self.assertGreater(self.saved_count(), 0)
        self.wire.send_mak_api(0x3e, b"\xf0")
        self.assertEqual(self.api.read().settings._encode(), imported.settings._encode())
        if os.environ.get("MAKXD_SETTINGS_ARTIFACTS"):
            directory = Path(os.environ["MAKXD_SETTINGS_ARTIFACTS"]); directory.mkdir(parents=True, exist_ok=True)
            (directory/"python.makxd-settings").write_bytes(exported)
            (directory/"python.settings.bin").write_bytes(imported.settings._encode())

    def test_invalid_corrupt_and_stale_never_apply(self):
        first = self.api.read(); changed = deepcopy(first)
        changed.settings.mouse_spread_percent = 17
        current = self.api.apply(changed, SettingsSection.MOUSE)
        with self.assertRaises(SettingsError) as error:
            self.api.apply(first)
        self.assertEqual(error.exception.status, 3)
        invalid = deepcopy(current); invalid.settings.translation[3].timeout_ms = 0
        with self.assertRaises(ValueError): self.api.apply(invalid)
        valid_file = self.api.export_preset(current)
        for position in (6, 30, 59, len(valid_file)-1):
            corrupt = bytearray(valid_file); corrupt[position] ^= 1
            with self.assertRaises((ValueError, SettingsError)): self.api.import_preset(bytes(corrupt))
        self.assertEqual(self.api.read().settings._encode(), current.settings._encode())
        self.assertEqual(self.saved_count(), 0)

    def test_repeated_slider_updates_are_live(self):
        current = self.api.read()
        for percent in range(0, 101):
            current.settings.mouse_spread_percent = percent
            current = self.api.apply(current, SettingsSection.MOUSE)
            self.assertEqual(current.settings.mouse_spread_percent, percent)
        self.assertEqual(self.saved_count(), 0)

    def test_browser_preset(self):
        path = Path(os.environ.get("MAKXD_SETTINGS_ARTIFACTS", "."))/"web.makxd-settings"
        if not path.exists(): self.skipTest("Browser export not produced yet")
        current = self.api.import_preset(path.read_bytes())
        self.assertEqual(current.settings.controller.behavior(ControllerChannel.RIGHT_STICK).name, "Live shared preset")
        self.assertEqual(current.settings.translation[1].scale, 19)
        self.assertEqual(self.saved_count(), 0)

    def test_all_sdk_presets_and_export_reads_mcu(self):
        directory = Path(os.environ.get("MAKXD_SETTINGS_ARTIFACTS", "."))
        for source, buffer_ms in (("cpp", 29), ("rust", 23), ("csharp", 17)):
            path = directory/(source+".makxd-settings")
            if not path.exists(): continue
            current = self.api.import_preset(path.read_bytes())
            self.assertEqual(current.settings.controller.buffer_ms, buffer_ms)
        current = self.api.read(); active = current.settings.controller.buffer_ms
        current.settings.controller.buffer_ms = 64 if active != 64 else 63
        exported = self.api.export_preset(current)
        imported = self.api.import_preset(exported)
        self.assertEqual(imported.settings.controller.buffer_ms, active, "Export included an unsent host edit")
        self.assertEqual(self.saved_count(), 0)


if __name__ == "__main__": unittest.main()
