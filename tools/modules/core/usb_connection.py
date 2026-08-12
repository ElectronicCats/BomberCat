import re
import time
import serial
from serial.tools import list_ports
from typing import Dict, List, Optional

try:
    import usb.core  # noqa: F401
    import usb.util  # noqa: F401

    _HAS_PYUSB = True
except ImportError:
    _HAS_PYUSB = False


# ── USB identifiers ──────────────────────────────────────────────────────────

#BOMBERCAT_VID = 0x
#BOMBERCAT_PID = 0x

# ── Serial defaults ───────────────────────────────────────────────────────────

DEFAULT_BAUDRATE = 115200
DEFAULT_COMPORT = "/dev/ttyUSB0"