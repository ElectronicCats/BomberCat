#!/usr/bin/env python3

# Electronic Cats
# bombercat.py — DeviceLink: the line-based control protocol client the CLI uses
# to talk to a BomberCat over USB-serial. Mirrors firmware/core/src/SerialControl
# (see NFCGATE_PLAN.md Fase 6). No APDUs travel here — this is the control plane.
# Distributed as-is; no warranty is given.

import time
from dataclasses import dataclass, field
from typing import Dict, Iterator, List, Optional

import serial

from .usb_connection import (
    DEFAULT_BAUDRATE,
    DEFAULT_TIMEOUT,
    PortInfo,
    list_ports_info,
    open_serial,
)


@dataclass
class Response:
    """One command's reply. `data` holds the `:key value` lines by key."""
    ok: bool
    message: str = ""
    data: Dict[str, str] = field(default_factory=dict)

    def __bool__(self) -> bool:
        return self.ok


class DeviceError(Exception):
    """A device command failed or the link misbehaved (timeout, closed port)."""


class DeviceLink:
    """
    Line protocol (see SerialControl.h): send `<cmd> [args]\\n`; read reply lines
    until a `+OK`/`-ERR` terminator, collecting `:key value` data lines. Any line
    without a `:`/`+`/`-` marker is device log noise and is ignored.
    """

    def __init__(self, port: str,
                 baudrate: int = DEFAULT_BAUDRATE,
                 timeout: float = DEFAULT_TIMEOUT):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._ser: Optional[serial.Serial] = None

    # -- lifecycle -----------------------------------------------------------
    def open(self) -> "DeviceLink":
        self._ser = open_serial(self.port, self.baudrate, self.timeout)
        # Let the CDC settle and drop any boot banner / autostart logs so the
        # first command reads its own reply, not stale output.
        time.sleep(0.3)
        self._ser.reset_input_buffer()
        return self

    def close(self) -> None:
        if self._ser is not None:
            self._ser.close()
            self._ser = None

    def __enter__(self) -> "DeviceLink":
        return self.open()

    def __exit__(self, *exc) -> None:
        self.close()

    # -- protocol ------------------------------------------------------------
    def command(self, line: str, read_timeout: Optional[float] = None) -> Response:
        """Send one command and parse its reply. Raises DeviceError on timeout."""
        if self._ser is None:
            raise DeviceError("link not open")
        deadline = time.monotonic() + (read_timeout or self.timeout * 4)

        self._ser.reset_input_buffer()  # strict request/response: drop stale noise
        self._ser.write((line.strip() + "\n").encode("ascii", "replace"))
        self._ser.flush()

        data: Dict[str, str] = {}
        while time.monotonic() < deadline:
            raw = self._ser.readline()
            if not raw:
                continue  # readline timeout tick; keep waiting until deadline
            text = raw.decode("ascii", "replace").strip("\r\n")
            if not text:
                continue
            marker = text[0]
            if marker == ":":
                key, _, value = text[1:].partition(" ")
                data[key] = value
            elif marker == "+":  # +OK [message]
                _, _, msg = text.partition(" ")
                return Response(ok=True, message=msg, data=data)
            elif marker == "-":  # -ERR message
                _, _, msg = text.partition(" ")
                return Response(ok=False, message=msg, data=data)
            # else: log noise, ignore
        raise DeviceError(f"timed out waiting for a reply to {line!r}")

    def ping(self) -> bool:
        """True if the device answers the handshake (`+OK bombercat`)."""
        try:
            r = self.command("ping", read_timeout=self.timeout)
        except DeviceError:
            return False
        return r.ok and "bombercat" in r.message

    def info(self) -> Response:
        return self.command("info")

    def status(self) -> Response:
        return self.command("status")

    def set(self, key: str, value: str) -> Response:
        return self.command(f"set {key} {value}")

    def save(self) -> Response:
        return self.command("save")

    def run(self) -> Response:
        # Starting the relay associates WiFi + connects the server; allow longer.
        return self.command("run", read_timeout=30.0)

    def stop(self) -> Response:
        return self.command("stop")

    def stream(self) -> Iterator[str]:
        """Yield decoded serial lines forever (for `monitor`). Read-only; does
        not send anything, so it does not disturb a running relay."""
        if self._ser is None:
            raise DeviceError("link not open")
        while True:
            raw = self._ser.readline()
            if not raw:
                continue
            yield raw.decode("ascii", "replace").rstrip("\r\n")


# ── Discovery helpers ─────────────────────────────────────────────────────────

def discover_devices(baudrate: int = DEFAULT_BAUDRATE,
                     timeout: float = 1.0) -> List[PortInfo]:
    """Ping every candidate serial port; return those that answer as a BomberCat."""
    found: List[PortInfo] = []
    for p in list_ports_info():
        try:
            with DeviceLink(p.device, baudrate, timeout) as link:
                if link.ping():
                    found.append(p)
        except (serial.SerialException, DeviceError, OSError):
            continue
    return found


def resolve_port(preferred: Optional[str] = None,
                 baudrate: int = DEFAULT_BAUDRATE) -> str:
    """
    Pick the port to talk to. If `preferred` is given, use it as-is. Otherwise
    auto-detect by handshake: exactly one BomberCat -> that port; zero or many
    -> DeviceError telling the user to pass --port.
    """
    if preferred:
        return preferred
    devices = discover_devices(baudrate)
    if len(devices) == 1:
        return devices[0].device
    if not devices:
        raise DeviceError(
            "no BomberCat found; pass --port (e.g. --port /dev/ttyACM0)")
    ports = ", ".join(d.device for d in devices)
    raise DeviceError(f"multiple BomberCats found ({ports}); pass --port")
