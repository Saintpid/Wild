#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM-1 Switch: a small window to put any FM-1 firmware file (.fwsc) on the FM-1, Wild or NB303,
whatever its number. M-UPGRADE only takes a number higher than the one installed; this takes any
package for the FM-1 model (the update itself is fm1_install.py's: Felucca's, over USB-MIDI).

It lists the .fwsc files in its folders (its own, and "Firmware Wild" / "Firmware NB303" beside it or
under C:\\Projects\\M-Vave FM1 n001se (~/M-Vave FM1 n001se on a Mac), and Downloads, if they are
there; more with Add folder), newest first, reads what the FM-1 runs, and installs the one chosen
with a progress bar.

  python tools/fm1_switch.pyw            (needs mido and python-rtmidi; Tk comes with Python)
  tools/build_switch_exe.bat             a FM1-Switch.exe for Windows (PyInstaller)
  tools/export_switch_mac.cmd            to the Mac, where Build_FM1Switch_Mac.command makes FM1-Switch.app
                                         and its DMG (tools/build_switch_mac.sh)
"""
import os
import re
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fm1_install as I  # noqa: E402

PROJECT = Path(r"C:\Projects\M-Vave FM1 n001se") if sys.platform == "win32" else Path.home() / "M-Vave FM1 n001se"
SUBDIRS = ("Firmware Wild", "Firmware NB303")


# ------------------------------------------------------------------ core ---
# (no Tk here: tests/switch_test.py drives it against a simulated FM-1)

def base_dir():
    """where the program is: the .exe's folder, the folder the .app is in, or this script's"""
    if not getattr(sys, "frozen", False):
        return Path(__file__).resolve().parent
    exe = Path(sys.executable).resolve()
    for p in exe.parents:               # FM1-Switch.app/Contents/MacOS/FM1-Switch
        if p.suffix == ".app":
            return p.parent
    return exe.parent


def default_folders(here=None):
    here = Path(here) if here else base_dir()
    out = [here]
    for root in (here, here.parent, PROJECT):
        for sub in SUBDIRS:
            p = root / sub
            if p.is_dir() and p not in out:
                out.append(p)
    dl = Path.home() / "Downloads"      # where a file from Patreon lands
    if dl.is_dir() and dl not in out:
        out.append(dl)
    return out


class Package:
    def __init__(self, path, product):
        self.path, self.product = Path(path), product
        self.number = int(product.split("_")[1])

    @property
    def family(self):
        n = self.path.name.lower()
        return "NB303" if n.startswith("nb303") else "Wild" if n.startswith("wild") else \
            "n001se" if n.startswith("n001se") else "?"

    def label(self):
        return f"{self.path.stem}   ({self.product})"


def scan(folders):
    """every FM-1 package in the folders, the highest number first"""
    seen, out = set(), []
    for d in folders:
        try:
            files = sorted(Path(d).glob("*.fwsc"))
        except OSError:
            continue
        for f in files:
            key = f.resolve()
            if key in seen:
                continue
            seen.add(key)
            try:
                raw = f.read_bytes()
                product = I.product_of(raw)
            except (OSError, I.InstallError):
                continue
            if re.fullmatch(r"[^_]+_\d+", product) and I.model_of(product) == "FM-1" and I.LOADER_MARK in raw:
                out.append(Package(f, product))
    out.sort(key=lambda p: (-p.number, p.path.name))
    return out


def device_text(backend):
    """what the FM-1 runs ("FM-1_846"), or None"""
    up = I.Updater(backend)
    dev = up.find()
    if not dev:
        return None
    dev.link.close()
    return dev.id.text + ("  (in update mode: install again to finish)" if dev.id.loader else "")


def install(backend, pkg, step):
    """the whole update; step(kind, value) as fm1_install's Progress gets it. -> the identity after"""
    product, image = I.load_package(str(pkg.path), False)
    up = I.Updater(backend)
    dev = up.find()
    if not dev:
        raise I.not_found(up)
    return up.install(dev, image, product, step)


STEP_TEXT = {"start": "Starting the update on {}", "check": "Checking the package ({} reads)",
             "loader": "Switching to update mode", "write": "Writing {}%", "reboot": "Restarting",
             "done": "Done: the FM-1 runs {}"}


def step_text(kind, value=None):
    return STEP_TEXT[kind].format(value if value is not None else "")


# ------------------------------------------------------------------- window ---

def main():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    try:
        backend = I.MidoBackend()
    except ImportError:
        backend = None

    root = tk.Tk()
    root.title("FM-1 Switch")
    mac = sys.platform == "darwin"
    ui_font, mono = ("Helvetica", 13) if mac else ("Segoe UI", 11), ("Menlo", 12) if mac else ("Consolas", 10)
    root.geometry("560x420")
    folders = default_folders()
    state = {"pkgs": [], "busy": False}

    top = ttk.Frame(root, padding=10)
    top.pack(fill="both", expand=True)
    dev_var = tk.StringVar(value="FM-1: not read yet")
    ttk.Label(top, textvariable=dev_var, font=ui_font + ("bold",)).pack(anchor="w")
    ttk.Label(top, text="Firmware files (highest number first):").pack(anchor="w", pady=(10, 2))
    lst = tk.Listbox(top, height=12, font=mono, activestyle="none")
    lst.pack(fill="both", expand=True)
    bar = ttk.Progressbar(top, maximum=100)
    bar.pack(fill="x", pady=(10, 2))
    status = tk.StringVar(value="Connect the FM-1 with a USB data cable, close other MIDI apps.")
    ttk.Label(top, textvariable=status, wraplength=530).pack(anchor="w")
    row = ttk.Frame(top)
    row.pack(fill="x", pady=(10, 0))

    def ui(f, *a):
        root.after(0, f, *a)

    def refresh():
        state["pkgs"] = scan(folders)
        lst.delete(0, "end")
        for p in state["pkgs"]:
            lst.insert("end", f"{p.family:<7} {p.label()}")
        if not state["pkgs"]:
            lst.insert("end", "(no .fwsc files: Add folder…)")

    def busy(on):
        state["busy"] = on
        for b in buttons:
            b.state(["disabled"] if on else ["!disabled"])

    def read_device():
        if backend is None:
            messagebox.showerror("FM-1 Switch", "Needs mido and python-rtmidi:\npip install mido python-rtmidi")
            return
        busy(True)
        dev_var.set("FM-1: looking…")

        def work():
            try:
                t = device_text(backend)
            except Exception as e:      # (a port that will not open: another app has it)
                t, err = None, e
            else:
                err = None
            ui(dev_var.set, f"FM-1: runs {t}" if t else "FM-1: not found (USB data cable? another app using it?)")
            if err:
                ui(status.set, f"Error: {err}")
            ui(busy, False)
        threading.Thread(target=work, daemon=True).start()

    def do_install():
        sel = lst.curselection()
        if not sel or not state["pkgs"]:
            messagebox.showinfo("FM-1 Switch", "Choose a firmware file first.")
            return
        pkg = state["pkgs"][sel[0]]
        if backend is None:
            messagebox.showerror("FM-1 Switch", "Needs mido and python-rtmidi:\npip install mido python-rtmidi")
            return
        if not messagebox.askyesno("FM-1 Switch", f"Install {pkg.path.name} ({pkg.product})?\n\n"
                                                  "Do not unplug the FM-1 while it writes."):
            return
        busy(True)
        bar["value"] = 0

        def step(kind, value=None):
            ui(status.set, step_text(kind, value))
            if kind == "write":
                ui(bar.configure, value=value)
            elif kind == "done":
                ui(bar.configure, value=100)
                ui(dev_var.set, f"FM-1: runs {value}")

        def work():
            try:
                install(backend, pkg, step)
            except I.InstallError as e:
                ui(status.set, f"Error: {e}")
                ui(messagebox.showerror, "FM-1 Switch", str(e))
            except Exception as e:
                ui(status.set, f"Error: {e}")
            ui(busy, False)
        threading.Thread(target=work, daemon=True).start()

    def add_folder():
        d = filedialog.askdirectory(title="A folder with .fwsc files")
        if d and Path(d) not in folders:
            folders.append(Path(d))
            refresh()

    buttons = [ttk.Button(row, text="Read FM-1", command=read_device),
               ttk.Button(row, text="Install", command=do_install),
               ttk.Button(row, text="Add folder…", command=add_folder),
               ttk.Button(row, text="Refresh", command=refresh)]
    for b in buttons:
        b.pack(side="left", padx=(0, 6))

    def closing():
        if state["busy"] and not messagebox.askyesno(
                "FM-1 Switch", "An update is running. Closing now leaves the FM-1 in update mode "
                               "(install again to finish). Close anyway?"):
            return
        root.destroy()
    root.protocol("WM_DELETE_WINDOW", closing)
    refresh()
    if backend is not None:
        read_device()
    root.mainloop()


if __name__ == "__main__":
    main()
