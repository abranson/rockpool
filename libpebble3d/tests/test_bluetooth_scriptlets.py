#!/usr/bin/env python3
"""Exercise RPM Bluetooth restart decisions without touching system services."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class BluetoothScriptletsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = Path(__file__).resolve().parents[2]
        self.spec = (self.repo / "rpm/rockpool.spec").read_text()
        self.override = self.root / "etc/systemd/system/bluetooth.service.d/50-libpebble3d.conf"
        self.override.parent.mkdir(parents=True)
        (self.root / "run").mkdir()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.calls = self.root / "calls"
        self.calls.touch()
        for name in ("systemctl", "systemctl-user", "update-desktop-database"):
            command = self.bin / name
            command.write_text('''#!/bin/sh
printf '%s %s\n' "${0##*/}" "$*" >> "$TEST_CALLS"
''')
            command.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update({"PATH": str(self.bin) + os.pathsep + self.env["PATH"],
                         "TEST_CALLS": str(self.calls)})

    def scriptlet(self, name, count):
        match = re.search(r"^%" + name + r"\n(.*?)(?=^%|\Z)",
                          self.spec, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(match)
        script = match.group(1).replace("%{_sysconfdir}", str(self.root / "etc"))
        script = script.replace("/run/", str(self.root / "run") + "/")
        script = script.replace("%{name}", "rockpool")
        subprocess.run(["sh", "-c", script, name, str(count)], env=self.env,
                       check=True, capture_output=True, text=True)

    def install(self, previous, replacement, count=2):
        if previous is not None:
            self.override.write_text(previous)
        self.scriptlet("pre", count)
        self.override.write_text(replacement)
        self.scriptlet("post", count)
        self.assertFalse((self.root / "run/rockpool-bluetooth-override.sha256").exists())

    def restarts(self):
        return self.calls.read_text().splitlines().count("systemctl try-restart bluetooth.service")

    def test_first_install_applies_override(self):
        self.install(None, "[Service]\nEnvironment=TRACING=-E\n", count=1)
        self.assertEqual(self.restarts(), 1)

    def test_unchanged_upgrade_and_old_package_removal_preserve_connections(self):
        content = "[Service]\nEnvironment=TRACING=-E\n"
        self.install(content, content)
        self.scriptlet("preun", 1)
        self.scriptlet("postun", 1)
        self.assertEqual(self.restarts(), 0)

    def test_reinstall_with_unchanged_override_preserves_connections(self):
        content = "[Service]\nEnvironment=TRACING=-E\n"
        self.install(content, content, count=1)
        self.assertEqual(self.restarts(), 0)

    def test_changed_override_is_applied(self):
        self.install("[Service]\nEnvironment=TRACING=\n",
                     "[Service]\nEnvironment=TRACING=-E\n")
        self.assertEqual(self.restarts(), 1)

    def test_upgrade_restores_missing_override(self):
        self.install(None, "[Service]\nEnvironment=TRACING=-E\n")
        self.assertEqual(self.restarts(), 1)

    def test_missing_snapshot_does_not_skip_applying_override(self):
        self.override.write_text("[Service]\nEnvironment=TRACING=-E\n")
        self.scriptlet("post", 2)
        self.assertEqual(self.restarts(), 1)

    def test_uninstall_applies_override_removal(self):
        self.override.write_text("[Service]\nEnvironment=TRACING=-E\n")
        self.scriptlet("preun", 0)
        self.override.unlink()
        self.scriptlet("postun", 0)
        self.assertEqual(self.restarts(), 1)


if __name__ == "__main__":
    unittest.main()
