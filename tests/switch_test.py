#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_switch.pyw's core against the simulated FM-1 of install_test.py (no Tk, no hardware):
it lists FM-1 packages (highest number first, not other models or files without the loader), and
installs both ways: a lower number over a higher one (NB303 900 -> Wild 846) and back."""
import importlib.machinery
import importlib.util
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
sys.argv = [sys.argv[0]]
import install_test as T  # noqa: E402  (its fake FM-1 and packages; its own tests run on import's main only)

loader = importlib.machinery.SourceFileLoader("fm1_switch", str(ROOT / "tools" / "fm1_switch.pyw"))
spec = importlib.util.spec_from_loader("fm1_switch", loader)
S = importlib.util.module_from_spec(spec)
loader.exec_module(S)
I = S.I
I.DELAY.update(T.I.DELAY)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond


d = Path(tempfile.mkdtemp(prefix="fm1-switch-"))
(d / "Firmware Wild").mkdir()
(d / "Firmware NB303").mkdir()
wild = d / "Firmware Wild" / "Wild-0.3.12.fwsc"
nb = d / "Firmware NB303" / "NB303-0.1.0.fwsc"
wild.write_bytes(T.package("FM-1_846"))
nb.write_bytes(T.package("FM-1_900"))
(d / "Firmware Wild" / "other.fwsc").write_bytes(T.package("XYZ-2_5"))
(d / "Firmware Wild" / "noloader.fwsc").write_bytes(T.package("FM-1_999", marker=False))
(d / "Firmware Wild" / "notes.txt").write_text("x")

folders = S.default_folders(d)
ok(d / "Firmware Wild" in folders and d / "Firmware NB303" in folders, "default folders: its own and the Firmware ones")
pk = S.scan(folders)
ok([p.product for p in pk] == ["FM-1_900", "FM-1_846"] and [p.family for p in pk] == ["NB303", "Wild"],
   f"scan: FM-1 packages only, highest first ({[p.product for p in pk]})")

dev = T.FakeFM1(I.logical_image(T.package("FM-1_846")), identity="FM-1_900", after_write="FM-1_846")
ok(S.device_text(dev) == "FM-1_900", "reads the FM-1: FM-1_900")
steps = []
got = S.install(dev, pk[1], lambda k, v=None: steps.append((k, v)))
ok(got == "FM-1_846" and dev.bad == 0 and ("write", 100) in steps and steps[-1] == ("done", "FM-1_846"),
   "NB303 900 -> Wild 846: a lower number installs")
ok(all(S.step_text(k, v) for k, v in steps), "every step has its text")

dev = T.FakeFM1(I.logical_image(T.package("FM-1_900")), identity="FM-1_846", after_write="FM-1_900")
ok(S.install(dev, pk[0], lambda *a: None) == "FM-1_900" and dev.bad == 0, "Wild 846 -> NB303 900: and back")

dev = T.FakeFM1(b"", identity="XYZ-2_5")
try:
    S.install(dev, pk[0], lambda *a: None)
    ok(False, "another model: refused")
except I.InstallError as e:
    ok(e.code == "model", "another model: refused")

dev = T.FakeFM1(b"")
dev.connected = False
ok(S.device_text(dev) is None, "no FM-1: none")
print("switch: all ok" if not failed else f"switch: {failed} FAILED")
sys.exit(failed != 0)
