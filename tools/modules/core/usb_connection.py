#!/usr/bin/env python3

# Electronic Cats
# usb_connection.py — low-level USB-serial transport for the BomberCat control
# CLI: enumerate serial ports, group them into numbered devices and open them.
# The line protocol on top lives in bombercat.py (DeviceLink).
# See docs/NFCGATE_PLAN.md Fase 6.
# Distributed as-is; no warranty is given.

import os
from dataclasses import dataclass
from typing import List, Optional, Sequence, Set, Tuple

import serial
from serial.tools import list_ports


def _env_id(name: str) -> Optional[int]:
    """Parse an env-var USB id (hex ``0x1209`` or decimal); None if unset/bad."""
    raw = os.environ.get(name)
    if not raw:
        return None
    try:
        return int(raw, 0)
    except ValueError:
        return None


# ── USB identifiers ──────────────────────────────────────────────────────────
# The BomberCat board (electroniccats:mbed_rp2040:bombercat) enumerates over USB
# with VID 0x1209 (pid.codes) and PID 0x005E in its application personality — see
# the core's boards.txt (bombercat.vid.0/pid.0 …). Matching on these lets the CLI
# single out the right port without opening every serial device to handshake it
# (opening a port can reset some MCUs). A board re-flashed with a different USB
# identity can be pointed at via the BOMBERCAT_VID / BOMBERCAT_PID environment
# variables (hex ``0x1209`` or decimal); when both are set that pair is matched
# in addition to the built-in ones.
BOMBERCAT_VID: int = _env_id("BOMBERCAT_VID") or 0x1209
BOMBERCAT_PID: int = _env_id("BOMBERCAT_PID") or 0x005E

# Every (vid, pid) a BomberCat CDC can enumerate as across its personalities
# (application / bootloader), plus any env-var override.
BOMBERCAT_USB_IDS: Set[Tuple[int, int]] = {
    (BOMBERCAT_VID, BOMBERCAT_PID),
    (0x1209, 0x005E),  # application (relay control REPL)
    (0x1209, 0x805E),  # alternate application personality
    (0x1209, 0x015E),
    (0x1209, 0x025E),
    # Sketches built against the stock Arduino Mbed RP2040 profile (rather than
    # electroniccats:mbed_rp2040:bombercat) keep Arduino's Nano RP2040 Connect
    # USB identity — that is what the boards on the bench actually report
    # (`VID:PID=2341:005E`). Listing
    # it here is what lets device numbering (`-d`) work on those builds. A real
    # Nano RP2040 Connect matches too, so this only *tags* a port as a candidate:
    # auto-detection and `device list`'s ✓ still require the control handshake.
    (0x2341, 0x005E),
}

# ── Serial defaults ───────────────────────────────────────────────────────────
DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 2.0        # seconds, per readline
DEFAULT_WRITE_TIMEOUT = 2.0  # seconds; bound writes so a firmware that stops
                             # draining its USB-OUT endpoint can't hang us forever

# Ports that are almost never a BomberCat; skipped by default when listing.
_NOISE_HINTS = ("ttyS", "Bluetooth", "debug-console")


@dataclass
class PortInfo:
    device: str          # e.g. /dev/ttyACM0
    description: str      # human label from the OS
    hwid: str            # VID:PID / serial, when known
    vid: Optional[int] = None  # USB vendor id, when the OS reports one
    pid: Optional[int] = None  # USB product id, when the OS reports one
    serial_number: Optional[str] = None  # USB iSerial, when the OS reports one
    location: Optional[str] = None       # USB topology "bus-hub.port:cfg.intf"

    @property
    def is_candidate(self) -> bool:
        """Rough filter: hide obvious non-USB-CDC ports (built-in UARTs, BT)."""
        return not any(h in self.device or h in self.description
                       for h in _NOISE_HINTS)

    @property
    def matches_bombercat(self) -> bool:
        """True if this port's USB VID/PID is a known BomberCat identity."""
        return self.vid is not None and (self.vid, self.pid) in BOMBERCAT_USB_IDS


def list_ports_info(include_all: bool = False) -> List[PortInfo]:
    """Enumerate serial ports. By default hides obvious non-candidate ports."""
    ports = [
        PortInfo(device=p.device,
                 description=p.description or "",
                 hwid=p.hwid or "",
                 vid=p.vid,
                 pid=p.pid,
                 serial_number=p.serial_number,
                 location=p.location)
        for p in list_ports.comports()
    ]
    # BomberCat-tagged ports first (stable within each group by device name), so
    # callers that probe in order hit the most likely device first.
    ports.sort(key=lambda p: (not p.matches_bombercat, p.device))
    if include_all:
        return ports
    return [p for p in ports if p.is_candidate]


def bombercat_ports() -> List[PortInfo]:
    """Ports whose USB VID/PID identify them as a BomberCat (no handshake)."""
    return [p for p in list_ports_info() if p.matches_bombercat]


# ════════════════════════════════════════════════════════════════════════════ #
# Device model — numbered devices for `--device/-d`                            #
# ════════════════════════════════════════════════════════════════════════════ #
#
# A BomberCat exposes exactly ONE USB CDC-ACM interface (the control REPL), so
# unlike the CatSniffer — three interfaces per unit that must be grouped by USB
# serial number and mapped to roles — here one port *is* one device. What we do
# need from that model is the stable numbering: with several boards attached the
# host assigns /dev/ttyACM* (or COM*) in a non-deterministic order, so the CLI
# addresses devices by an ID derived from a stable USB identity instead.


@dataclass
class BomberCatDevice:
    """One physical BomberCat, addressable by `--device/-d <id>`."""

    device_id: int                       # 1-based, stable while the set of
                                         # attached boards doesn't change
    port: str                            # serial port path (/dev/ttyACM0, COM3)
    serial_number: Optional[str] = None  # USB iSerial — the identity we sort on
    description: str = ""
    hwid: str = ""
    usb_tagged: bool = True              # False = matched only as a fallback,
                                         # its USB VID/PID isn't a BomberCat's

    @property
    def identity(self) -> str:
        """Short human label for the identity the ID is derived from."""
        return self.serial_number or self.port

    def __str__(self) -> str:
        return f"BomberCat #{self.device_id}"

    def __repr__(self) -> str:
        return (f"BomberCatDevice(id={self.device_id}, port={self.port!r}, "
                f"serial={self.serial_number!r})")


def _identity_key(port: PortInfo) -> Tuple[int, str]:
    """Sort key that keeps device IDs stable across replugs and reboots.

    Priority: USB serial number (survives re-enumeration and a changed ttyACM
    number), then the USB topology location prefix — the "bus-hub.port" part
    before the ':' — which is stable as long as the board stays in the same
    physical socket, and finally the port path as a last resort.
    """
    if port.serial_number:
        return (0, port.serial_number)
    if port.location:
        return (1, port.location.split(":")[0])
    return (2, port.device)


def find_devices(ports: Optional[Sequence[PortInfo]] = None
                 ) -> List[BomberCatDevice]:
    """Enumerate attached BomberCats and number them 1..N (no handshake).

    Numbering is USB-only — cheap, and it doesn't open any port (opening one can
    reset the MCU). Ports tagged with a BomberCat USB VID/PID are used when there
    are any; if none is tagged (e.g. a board re-flashed to a generic Arduino
    identity) every candidate port is numbered instead, so `-d` still addresses
    something and `bombercat device list` shows what each ID maps to.
    """
    all_ports = list(ports) if ports is not None else list_ports_info()
    tagged = [p for p in all_ports if p.matches_bombercat]
    pool = tagged or all_ports
    return [
        BomberCatDevice(device_id=i,
                        port=p.device,
                        serial_number=p.serial_number,
                        description=p.description,
                        hwid=p.hwid,
                        usb_tagged=p.matches_bombercat)
        for i, p in enumerate(sorted(pool, key=_identity_key), start=1)
    ]


def find_device(device_id: Optional[int] = None) -> Optional[BomberCatDevice]:
    """Return one numbered BomberCat: the given ID, or the first one attached."""
    devices = find_devices()
    if not devices:
        return None
    if device_id is None:
        return devices[0]
    return next((d for d in devices if d.device_id == device_id), None)


def describe_devices(devices: Optional[Sequence[BomberCatDevice]] = None) -> str:
    """One-line "#1 /dev/ttyACM0, #2 /dev/ttyACM1" summary for error messages."""
    devices = find_devices() if devices is None else devices
    return ", ".join(f"#{d.device_id} {d.port}" for d in devices)


def open_serial(port: str,
                baudrate: int = DEFAULT_BAUDRATE,
                timeout: float = DEFAULT_TIMEOUT,
                write_timeout: float = DEFAULT_WRITE_TIMEOUT) -> serial.Serial:
    """Open a serial port for the control protocol. Raises serial.SerialException.

    ``write_timeout`` bounds blocking writes so a wedged firmware (one that stops
    servicing its USB-OUT endpoint) surfaces as a clean error instead of hanging
    the CLI indefinitely.
    """
    return serial.Serial(port=port, baudrate=baudrate, timeout=timeout,
                         write_timeout=write_timeout)
