#!/usr/bin/env python3

# Electronic Cats
# usb_connection.py — low-level USB-serial transport for the BomberCat control
# CLI: enumerate serial ports and open them. The line protocol on top lives in
# bombercat.py (DeviceLink). See NFCGATE_PLAN.md Fase 6.
# Distributed as-is; no warranty is given.

from dataclasses import dataclass
from typing import List, Optional

import serial
from serial.tools import list_ports

# ── USB identifiers ──────────────────────────────────────────────────────────
# The BomberCat enumerates as a generic Mbed RP2040 CDC device; there is no
# stable BomberCat-specific VID/PID to match on, so discovery falls back to
# handshaking every candidate port (see bombercat.discover_devices). These are
# left as hints for future filtering if a dedicated VID/PID is ever assigned.
BOMBERCAT_VID: Optional[int] = None
BOMBERCAT_PID: Optional[int] = None

# ── Serial defaults ───────────────────────────────────────────────────────────
DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 2.0  # seconds, per readline

# Ports that are almost never a BomberCat; skipped by default when listing.
_NOISE_HINTS = ("ttyS", "Bluetooth", "debug-console")


@dataclass
class PortInfo:
    device: str          # e.g. /dev/ttyACM0
    description: str      # human label from the OS
    hwid: str            # VID:PID / serial, when known

    @property
    def is_candidate(self) -> bool:
        """Rough filter: hide obvious non-USB-CDC ports (built-in UARTs, BT)."""
        return not any(h in self.device or h in self.description
                       for h in _NOISE_HINTS)


def list_ports_info(include_all: bool = False) -> List[PortInfo]:
    """Enumerate serial ports. By default hides obvious non-candidate ports."""
    ports = [
        PortInfo(device=p.device,
                 description=p.description or "",
                 hwid=p.hwid or "")
        for p in list_ports.comports()
    ]
    ports.sort(key=lambda p: p.device)
    if include_all:
        return ports
    return [p for p in ports if p.is_candidate]


def open_serial(port: str,
                baudrate: int = DEFAULT_BAUDRATE,
                timeout: float = DEFAULT_TIMEOUT) -> serial.Serial:
    """Open a serial port for the control protocol. Raises serial.SerialException."""
    return serial.Serial(port=port, baudrate=baudrate, timeout=timeout)
