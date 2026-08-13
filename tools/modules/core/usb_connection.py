#!/usr/bin/env python3

# Electronic Cats
# usb_connection.py — low-level USB-serial transport for the BomberCat control
# CLI: enumerate serial ports and open them. The line protocol on top lives in
# bombercat.py (DeviceLink). See NFCGATE_PLAN.md Fase 6.
# Distributed as-is; no warranty is given.

import os
from dataclasses import dataclass
from typing import List, Optional, Set, Tuple

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
                 pid=p.pid)
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


def open_serial(port: str,
                baudrate: int = DEFAULT_BAUDRATE,
                timeout: float = DEFAULT_TIMEOUT,
                write_timeout: float = DEFAULT_WRITE_TIMEOUT) -> serial.Serial:
    """Open a serial port for the control protocol. Raises serial.SerialException.

    ``write_timeout`` bounds blocking writes so a wedged firmware (one that stops
    servicing its USB-OUT endpoint) surfaces as a clean error instead of hanging
    the CLI indefinitely — see firmware/DEBUG_serial_no_handshake.md.
    """
    return serial.Serial(port=port, baudrate=baudrate, timeout=timeout,
                         write_timeout=write_timeout)
