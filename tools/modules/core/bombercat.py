import enum
import time

# Internal
from .usb_connection import ()
# Re-exported for callers that import these names from catnip
__all__ = []

# External
import serial

# Shell commands for bootloader control
SHELL_CMD_BOOT = "boot"
SHELL_CMD_EXIT = "exit"

# Shell commands for firmware update
SHELL_CMD_FW_VERSION = "fw_version"
SHELL_CMD_REBOOT = "reboot"

def bomber_get_devices():
    return find_devices()


def bomber_get_device(device_id=None):
    return find_device(device_id)


def bomber_get_port():
    return get_bridge_port()