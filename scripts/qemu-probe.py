#!/usr/bin/env python3
"""Boot an ISO under QEMU, then poke the monitor for CPU/memory state.

Usage:
    qemu-probe.py [iso] [--addr ADDR] [--len N] [--wait SECONDS]

Prints the serial log, then `info registers`, a virtual read at ADDR, and a
physical read at ADDR. Useful when the boot chain hangs and serial output
stops: the monitor still answers.
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def monitor(sock_path, commands, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock_path)
            break
        except OSError:
            time.sleep(0.05)
    else:
        raise RuntimeError("monitor socket never appeared")

    s.settimeout(2.0)
    # Drain the banner.
    time.sleep(0.2)
    try:
        s.recv(65536)
    except socket.timeout:
        pass

    out = []
    for cmd in commands:
        s.sendall((cmd + "\n").encode())
        time.sleep(0.25)
        chunk = b""
        try:
            while True:
                part = s.recv(65536)
                if not part:
                    break
                chunk += part
        except socket.timeout:
            pass
        text = chunk.decode("utf-8", "replace")
        # Strip readline echo/escape noise.
        text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
        lines = [ln for ln in text.splitlines() if ln.strip() and ln.strip().startswith("(") is False]
        out.append("$ " + cmd)
        out.extend(lines)
    s.close()
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", nargs="?", default=os.path.join(ROOT, "build/test/os.iso"))
    ap.add_argument("--addr", default="0x20070")
    ap.add_argument("--len", type=int, default=32)
    ap.add_argument("--wait", type=float, default=4.0)
    ap.add_argument("--screendump", default="", help="save a framebuffer image")
    ap.add_argument("--keys", default="", help="comma-separated sendkey names")
    ap.add_argument("--mouse", default="", help="relative mouse move as dx,dy")
    ap.add_argument("--click", action="store_true", help="left click after moving")
    ap.add_argument("--wait-line", default="",
                    help="wait for this serial line before monitor commands")
    ap.add_argument("--serial-send", default="",
                    help="type this text on COM1 and capture the reply")
    args = ap.parse_args()

    iso = os.path.abspath(args.iso)
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    tmp = tempfile.mkdtemp(prefix="zenku-probe-")
    sock = os.path.join(tmp, "mon.sock")
    serial = os.path.join(tmp, "serial.log")
    serial_sock_path = os.path.join(tmp, "serial.sock") if args.serial_send else ""
    serial_arg = (f"unix:{serial_sock_path},server=on,nowait"
                  if serial_sock_path else f"file:{serial}")

    proc = subprocess.Popen(
        [
            qemu,
            "-cdrom", iso,
            "-m", "2G",
            "-machine", "pc",
            "-display", "none",
            "-serial", serial_arg,
            "-monitor", f"unix:{sock},server,nowait",
            "-device", "isa-debug-exit,iobase=0x501,iosize=0x04",
            "-no-reboot",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )

    ser = None
    serial_buf = bytearray()

    def open_serial():
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(serial_sock_path)
                s.setblocking(False)
                return s
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("serial socket never appeared")

    def pump(until):
        while time.time() < until:
            if ser is not None:
                try:
                    chunk = ser.recv(65536)
                except BlockingIOError:
                    chunk = b""
                if chunk:
                    serial_buf.extend(chunk)
            time.sleep(0.05)

    def seen_text():
        if ser is not None:
            return bytes(serial_buf).decode("latin-1")
        try:
            with open(serial, "rb") as f:
                return f.read().decode("latin-1")
        except OSError:
            return ""

    try:
        if serial_sock_path:
            ser = open_serial()

        if args.wait_line:
            deadline = time.time() + args.wait
            while time.time() < deadline:
                pump(min(time.time() + 0.05, deadline))
                if args.wait_line in seen_text():
                    pump(time.time() + 0.5)
                    break
        else:
            pump(time.time() + args.wait)

        if args.serial_send:
            text = args.serial_send
            if not text.endswith(("\r", "\n")):
                text += "\r"
            ser.sendall(text.encode())
            pump(time.time() + 1.5)
        n = args.len
        cmds = []
        if args.keys:
            cmds += ["sendkey " + key for key in args.keys.split(",")]
        if args.mouse:
            dx, dy = args.mouse.split(",")
            cmds.append(f"mouse_move {dx} {dy}")
        if args.click:
            cmds += ["mouse_button 1", "mouse_button 0"]
        if args.screendump:
            cmds.append(f"screendump {os.path.abspath(args.screendump)}")
        cmds += [
            "info registers",
            f"x /{n}xb {args.addr}",
            f"xp /{n}xb {args.addr}",
            "info mem",
        ]
        out = monitor(sock, cmds)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    if ser is not None:
        with open(serial, "wb") as f:
            f.write(serial_buf)

    print("=== serial ===")
    if os.path.exists(serial):
        with open(serial, "rb") as f:
            sys.stdout.write(f.read().decode("latin-1").replace("\0", "\\0"))
    print()
    print("=== monitor ===")
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
