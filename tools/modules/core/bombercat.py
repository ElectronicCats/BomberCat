#!/usr/bin/env python3

# Electronic Cats
# bombercat.py — DeviceLink: the line-based control protocol client the CLI uses
# to talk to a BomberCat over USB-serial. Mirrors firmware/core/src/SerialControl
# (see docs/NFCGATE_PLAN.md Fase 6). No APDUs travel here — this is the control plane.
# Distributed as-is; no warranty is given.

import time
from dataclasses import dataclass, field
from typing import Dict, Iterator, List, Optional

import serial

from .usb_connection import (
    DEFAULT_BAUDRATE,
    DEFAULT_TIMEOUT,
    BomberCatDevice,
    bombercat_ports,
    describe_devices,
    find_device,
    find_devices,
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

        try:
            self._ser.reset_input_buffer()  # strict req/response: drop stale noise
            self._ser.write((line.strip() + "\n").encode("ascii", "replace"))
            self._ser.flush()
        except serial.SerialTimeoutException:
            # write_timeout tripped: the device isn't draining its USB-OUT
            # endpoint (wedged firmware / wrong sketch). See usb_connection.py.
            raise DeviceError(
                f"device did not accept {line!r} (write timed out); it may be "
                "wedged or not running the relay firmware")
        except serial.SerialException as e:
            raise DeviceError(f"serial error sending {line!r}: {e}")

        data: Dict[str, str] = {}
        while time.monotonic() < deadline:
            try:
                raw = self._ser.readline()
            except serial.SerialException as e:
                raise DeviceError(f"serial error reading reply to {line!r}: {e}")
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
        # 'run' is now non-blocking on the device: it only KICKS OFF the bring-up
        # and replies `+OK accepted` (or `-ERR <reason>`) right away. Progress is
        # observed by polling `status` (see nfcgate/cli.py:run_cmd), so this no
        # longer needs a long window — a normal command timeout is plenty.
        # Keep in sync with NFCGate.ino's async `run` state machine.
        return self.command("run", read_timeout=self.timeout)

    def stop(self) -> Response:
        return self.command("stop")

    def identify(self) -> Response:
        """Blink the device's LED so the user can tell which board is which."""
        return self.command("identify")

    def stream(self) -> Iterator[str]:
        """Yield decoded serial lines forever (for `monitor`). Read-only; does
        not send anything, so it does not disturb a running relay."""
        if self._ser is None:
            raise DeviceError("link not open")
        while True:
            try:
                raw = self._ser.readline()
            except serial.SerialException as e:
                # Device unplugged (or otherwise gone) while monitoring.
                raise DeviceError(f"serial link lost: {e}")
            if not raw:
                continue
            yield raw.decode("ascii", "replace").rstrip("\r\n")


# ── Discovery helpers ─────────────────────────────────────────────────────────

def discover_devices(baudrate: int = DEFAULT_BAUDRATE,
                     timeout: float = 1.0) -> List[BomberCatDevice]:
    """Return the numbered BomberCats that answer the handshake.

    Candidates and their IDs come from find_devices(): when any port is
    identified by its USB VID/PID (see usb_connection), only those are
    handshaked — we don't open unrelated serial devices, since opening a port can
    reset some MCUs. If nothing is tagged (e.g. a board mis-flashed to a generic
    Arduino VID/PID), every candidate port is probed instead. IDs are those
    assigned by find_devices(), so a device keeps its number whether or not its
    neighbours answer.
    """
    found: List[BomberCatDevice] = []
    for dev in find_devices():
        try:
            with DeviceLink(dev.port, baudrate, timeout) as link:
                if link.ping():
                    found.append(dev)
        except (serial.SerialException, DeviceError, OSError):
            continue
    return found


def resolve_port(preferred: Optional[str] = None,
                 device_id: Optional[int] = None,
                 baudrate: int = DEFAULT_BAUDRATE) -> str:
    """
    Pick the port to talk to:

      * `preferred` (--port) wins — used as-is, no enumeration.
      * `device_id` (--device/-d) selects one of the numbered devices reported by
        `bombercat device list`, without handshaking the others.
      * otherwise auto-detect by handshake: exactly one BomberCat -> that port;
        zero or many -> DeviceError telling the user to pass -d/--port.
    """
    if preferred and device_id is not None:
        raise DeviceError("--port and --device are mutually exclusive; "
                          "pass one or the other")
    if preferred:
        return preferred

    if device_id is not None:
        dev = find_device(device_id)
        if dev is not None:
            return dev.port
        known = find_devices()
        if known:
            raise DeviceError(
                f"no BomberCat with ID {device_id}; attached: "
                f"{describe_devices(known)} (see `bombercat device list`)")
        raise DeviceError(
            f"no BomberCat with ID {device_id}: none is attached "
            "(see `bombercat device list`)")

    devices = discover_devices(baudrate)
    if len(devices) == 1:
        return devices[0].port
    if len(devices) > 1:
        raise DeviceError(
            f"multiple BomberCats found ({describe_devices(devices)}); "
            "pass --device/-d <id> (or --port)")

    # None answered. If USB still shows a BomberCat by VID/PID, the board is
    # there but its firmware isn't serving the control REPL — point the user at
    # that instead of a bare "not found".
    tagged = bombercat_ports()
    if len(tagged) == 1:
        p = tagged[0]
        raise DeviceError(
            f"a BomberCat is connected at {p.device} (USB {p.hwid}) but it did "
            "not answer the handshake — is it running the NFCGate relay "
            "firmware?")
    if len(tagged) > 1:
        raise DeviceError(
            f"BomberCats detected by USB id ({describe_devices()}) but none "
            "answered the handshake; pass --device/-d <id> and check the "
            "firmware")
    raise DeviceError(
        "no BomberCat found; pass --port (e.g. --port /dev/ttyACM0)")
