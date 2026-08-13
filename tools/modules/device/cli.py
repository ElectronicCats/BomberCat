#!/usr/bin/env python3

# Electronic Cats
# `bombercat device` — enumerate serial ports and query a BomberCat over the
# control protocol (NFCGATE_PLAN.md Fase 6).
# Distributed as-is; no warranty is given.

import click
from rich.table import Table

from ..core.bombercat import DeviceError, DeviceLink, discover_devices, resolve_port
from ..core.usb_connection import list_ports_info
from ..utils.output import console, print_error, print_info, print_warning


@click.group("device", context_settings={"help_option_names": ["-h", "--help"]})
def device():
    """Discover and inspect BomberCat devices over USB-serial."""


@device.command("list")
@click.option("-a", "--all", "show_all", is_flag=True,
              help="Include non-candidate ports (built-in UARTs, Bluetooth).")
def list_cmd(show_all):
    """List serial ports and flag which ones answer as a BomberCat."""
    ports = list_ports_info(include_all=show_all)
    if not ports:
        print_warning("No serial ports found.")
        return

    responders = {p.device for p in discover_devices()}

    table = Table(title="Serial ports", header_style="cyan bold")
    table.add_column("Port")
    table.add_column("BomberCat")
    table.add_column("Description")
    table.add_column("HWID", style="dim")
    for p in ports:
        mark = "[green]✓[/green]" if p.device in responders else ""
        table.add_row(p.device, mark, p.description, p.hwid)
    console.print(table)

    if not responders:
        print_info("No BomberCat answered the handshake. Is one connected and "
                   "flashed with the NFCGate firmware?")


@device.command("info")
@click.option("-p", "--port", default=None,
              help="Serial port (auto-detected if omitted).")
def info_cmd(port):
    """Handshake with a BomberCat and show its firmware/config."""
    try:
        target = resolve_port(port)
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
