#!/usr/bin/env python3

# Electronic Cats
# `bombercat device` — enumerate serial ports, number the attached BomberCats
# (the IDs `--device/-d` takes) and query one over the control protocol
# (docs/NFCGATE_PLAN.md Fase 6).
# Distributed as-is; no warranty is given.

import click
from rich.table import Table

from ..core.bombercat import DeviceError, DeviceLink, discover_devices, resolve_port
from ..core.usb_connection import find_devices, list_ports_info
from ..utils.cli_options import target_options
from ..utils.output import (
    console,
    print_error,
    print_info,
    print_warning,
)


@click.group("device", context_settings={"help_option_names": ["-h", "--help"]})
def device():
    """Discover and inspect BomberCat devices over USB-serial."""


@device.command("list")
@click.option("-a", "--all", "show_all", is_flag=True,
              help="Include non-candidate ports (built-in UARTs, Bluetooth).")
def list_cmd(show_all):
    """List serial ports, the device ID of each BomberCat and who answers."""
    ports = list_ports_info(include_all=show_all)
    if not ports:
        print_warning("No serial ports found.")
        return

    devices = find_devices()
    ids = {d.port: d.device_id for d in devices}
    responders = {d.port for d in discover_devices()}
    untagged = bool(devices) and not any(d.usb_tagged for d in devices)

    table = Table(title="Serial ports", header_style="cyan bold")
    table.add_column("ID")
    table.add_column("Port")
    table.add_column("BomberCat")
    table.add_column("Serial#", style="dim")
    table.add_column("HWID", style="dim")
    usb_only = False
    for p in ports:
        dev_id = ids.get(p.device)
        if p.device in responders:
            mark = "[green]✓[/green]"
        elif p.matches_bombercat:
            # USB VID/PID says BomberCat, but it didn't answer the handshake.
            mark = "[yellow]USB id[/yellow]"
            usb_only = True
        else:
            mark = ""
        table.add_row(f"#{dev_id}" if dev_id else "",
                      p.device, mark,
                      p.serial_number or "", p.hwid)
    console.print(table)

    if devices:
        print_info("Target one with:  bombercat <command> -d <ID>   "
                   "(e.g. bombercat config show -d 1)")
    if untagged:
        print_warning("No port carries a BomberCat USB id, so every candidate "
                      "port was numbered — check the IDs above before using -d.")

    if not responders and usb_only:
        print_info("A BomberCat is present by USB id but did not answer the "
                   "handshake — it may not be running the NFCGate relay "
                   "firmware.")
    elif not responders:
        print_info("No BomberCat answered the handshake. Is one connected and "
                   "flashed with the NFCGate firmware?")


@device.command("info")
@target_options
def info_cmd(port, device_id):
    """Handshake with a BomberCat and show its firmware/config."""
    try:
        target = resolve_port(port, device_id)
        with DeviceLink(target) as link:
            if not link.ping():
                print_error(f"{target} did not answer the handshake.")
                raise SystemExit(1)
            r = link.info()
    except DeviceError as e:
        print_error(str(e))
        raise SystemExit(1)
    except Exception as e:  # serial errors, permission, etc.
        print_error(f"{type(e).__name__}: {e}")
        raise SystemExit(1)

    if not r.ok:
        print_error(f"info failed: {r.message}")
        raise SystemExit(1)

    table = Table(title=f"BomberCat @ {target}", header_style="cyan bold")
    table.add_column("Field")
    table.add_column("Value")
    for key in ("fw", "role", "ssid", "server", "port", "session", "state"):
        table.add_row(key, r.data.get(key, "[dim]—[/dim]"))
    console.print(table)
