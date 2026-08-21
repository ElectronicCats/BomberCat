#!/usr/bin/env python3

# Electronic Cats
# cli_options.py — click options shared by every command that talks to a board,
# so device selection reads the same everywhere:
#
#   -p/--port <path>   raw serial port (/dev/ttyACM0, COM3)
#   -d/--device <ID>   stable device ID from `bombercat device list`
#
# With a single BomberCat attached both can be omitted (handshake auto-detect).
# The resolution rules live in core/bombercat.resolve_port().
# Distributed as-is; no warranty is given.

import click

PORT_OPTION = click.option(
    "-p", "--port", default=None, metavar="PATH",
    help="Serial port (auto-detected if omitted).")

DEVICE_OPTION = click.option(
    "-d", "--device", "device_id", default=None, type=int, metavar="ID",
    help="Device ID from `bombercat device list` (for multiple BomberCats).")


def target_options(func):
    """Apply both selectors to a command (`-p/--port` and `-d/--device`)."""
    return PORT_OPTION(DEVICE_OPTION(func))
