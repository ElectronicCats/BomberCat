#!/usr/bin/env python3

# Electronic Cats
# serialctl_hosttest.py — verify the DeviceLink control-protocol parser
# (tools/modules/core/bombercat.py) against a scripted fake device over a pty.
# No hardware and no nfcgate-server needed; this is the control-plane counterpart
# to tools/testserver/codec_hosttest (which checks the RF/protobuf wire format).
#
# It proves the CLI side of docs/NFCGATE_PLAN.md Fase 6: the leading-marker protocol
# (`:key value`, `+OK`, `-ERR`), log-noise filtering, data collection, values
# with spaces, and error propagation — all mirroring firmware SerialControl.
#
# Run:  python3 tools/tests/serialctl_hosttest.py   (Linux/macOS; uses os.openpty)

import os
import sys
import threading
from pathlib import Path

# tools/tests/serialctl_hosttest.py -> tools/ on sys.path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from modules.core.bombercat import DeviceLink  # noqa: E402


def _fake_device(master_fd: int) -> None:
    """Emulate firmware SerialControl: reply to command lines, interleaving log
    noise to prove the client ignores non-marker lines."""
    buf = b""
    while True:
        try:
            data = os.read(master_fd, 256)
        except OSError:
            return
        if not data:
            return
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            cmd = line.decode(errors="replace").strip()

            def send(s: str) -> None:
                os.write(master_fd, (s + "\r\n").encode())

            if cmd == "ping":
                send("[boot] noise line ignored")  # log noise before the marker
                send("+OK bombercat")
            elif cmd == "info":
                send("LOG: some debug chatter")  # noise between data lines
                send(":fw 0.6.0")
                send(":role reader")
                send(":ssid HomeNet")
                send(":server 192.168.1.5")
                send(":port 5566")
                send(":session 42")
                send(":state idle")
                send("+OK")
            elif cmd.startswith("set "):
                parts = cmd.split()
                key = parts[1] if len(parts) > 1 else ""
                if key == "session" and "999" in cmd:
                    send("-ERR session must be 1..255")
                else:
                    send("+OK")
            elif cmd == "status":
                send(":state relaying")
                send(":connected 1")
                send(":peer 1")
                send(":relayed 7")
                send("+OK")
            elif cmd == "save":
                send("+OK")
            elif cmd == "identify":
                send("+OK")
            else:
                send("-ERR unknown command")


def main() -> int:
    master, slave = os.openpty()
    slave_name = os.ttyname(slave)
    threading.Thread(target=_fake_device, args=(master,), daemon=True).start()

    fails = 0

    def check(name: str, cond: bool) -> None:
        nonlocal fails
        print(f"[{'OK' if cond else 'FAIL'}] {name}")
        if not cond:
            fails += 1

    link = DeviceLink(slave_name, timeout=1.0).open()
    try:
        check("ping handshake", link.ping())

        r = link.info()
        check("info ok", r.ok)
        check("info fw parsed", r.data.get("fw") == "0.6.0")
        check("info server parsed", r.data.get("server") == "192.168.1.5")
        check("info state parsed", r.data.get("state") == "idle")

        r = link.set("ssid", "My Home Net")  # value with spaces survives
        check("set ssid (spaces) ok", r.ok)

        r = link.command("set session 999")
        check("set out-of-range -> ERR + message", (not r.ok) and "1..255" in r.message)

        r = link.status()
        check("status relayed=7", r.ok and r.data.get("relayed") == "7")
        check("status connected=1", r.data.get("connected") == "1")

        r = link.identify()  # `identify` (fw >= 0.7.0) blinks the LED
        check("identify ok", r.ok)

        # Older firmware answers -ERR unknown command; the CLI must surface that
        # as a clean failure, not an exception.
        r = link.command("identify-nope")
        check("unknown command -> ERR", (not r.ok) and "unknown" in r.message)
    finally:
        link.close()

    print("\nSERIALCTL PROTOCOL TEST",
          "PASSED" if fails == 0 else f"FAILED ({fails})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
