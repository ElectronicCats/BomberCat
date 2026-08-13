#!/usr/bin/env python3

# Electronic Cats
# `bombercat config|run|stop|status|monitor` — configure and drive the NFCGate
# relay over the control protocol (NFCGATE_PLAN.md Fase 6).
# Distributed as-is; no warranty is given.

from contextlib import contextmanager
from typing import Iterator, List, Optional, Tuple

import click
import serial
from rich.table import Table

from ..core.bombercat import DeviceError, DeviceLink, resolve_port
from ..utils.output import (
    console,
    print_error,
    print_info,
    print_success,
    print_warning,
)

_PORT_OPTION = click.option("-p", "--port", default=None,
                            help="Serial port (auto-detected if omitted).")


@contextmanager
def _device_session(port: Optional[str]) -> Iterator[Tuple[str, DeviceLink]]:
    """Open a verified link, yield ``(target, link)``, and always close it.

    Any ``DeviceError`` or serial/OS error — whether raised while connecting
    OR while running commands inside the ``with`` block — is reported as a
    clean one-line message and exits with status 1, instead of letting a
    traceback reach the user. ``SystemExit`` raised by the body (e.g. a failed
    ``set``) passes through untouched, and the port is closed either way.
    """
    link: Optional[DeviceLink] = None
    try:
        target = resolve_port(port)
        link = DeviceLink(target).open()
        if not link.ping():
            print_error(
                f"{target} did not answer the handshake. "
                "Is it plugged in and running the relay firmware?"
            )
            raise SystemExit(1)
        yield target, link
    except DeviceError as e:
        print_error(str(e))
        raise SystemExit(1)
    except (serial.SerialException, OSError) as e:
        print_error(f"{type(e).__name__}: {e}")
        raise SystemExit(1)
    finally:
        if link is not None:
            link.close()


def _apply(link: DeviceLink, pairs: List[Tuple[str, str]], save: bool) -> None:
    """Run `set key value` for each pair, then optionally `save`. Exits on error."""
    for key, value in pairs:
        r = link.set(key, value)
        if r.ok:
            print_success(f"set {key} = {value if key != 'pass' else '••••••'}")
        else:
            print_error(f"set {key} failed: {r.message}")
            raise SystemExit(1)
    if save:
        r = link.save()
        if r.ok:
            print_success("saved to flash")
        else:
            print_error(f"save failed: {r.message}")
            raise SystemExit(1)
    else:
        print_info("not persisted (--no-save); lost on reboot")


# ── config group ──────────────────────────────────────────────────────────────

@click.group("config", context_settings={"help_option_names": ["-h", "--help"]})
def config():
    """Configure the relay (WiFi + nfcgate parameters), persisted in flash."""


@config.command("wifi")
@click.option("--ssid", required=True, help="WiFi network name.")
@click.option("--password", "--pass", "password", default="",
              help="WiFi passphrase (empty for an open network).")
@click.option("--save/--no-save", default=True, help="Persist to flash (default: save).")
@_PORT_OPTION
def config_wifi(ssid, password, save, port):
    """Set the WiFi credentials."""
    with _device_session(port) as (_, link):
        _apply(link, [("ssid", ssid), ("pass", password)], save)


@config.command("nfcgate")
@click.option("--server", required=True,
              help="nfcgate-server as host or host:port.")
@click.option("--session", type=click.IntRange(1, 255), required=True,
              help="Session byte 1..255; both peers must match.")
@click.option("--role", type=click.Choice(["reader", "card"]), required=True,
              help="reader = read a physical card, card = emulate to a terminal.")
@click.option("--save/--no-save", default=True, help="Persist to flash (default: save).")
@_PORT_OPTION
def config_nfcgate(server, session, role, save, port):
    """Set the nfcgate-server, session and role."""
    host, _, port_str = server.partition(":")
    pairs = [("server", host)]
    if port_str:
        if not port_str.isdigit() or not (1 <= int(port_str) <= 65535):
            print_error(f"invalid port in --server: {port_str!r}")
            raise SystemExit(1)
        pairs.append(("port", port_str))
    pairs += [("session", str(session)), ("role", role)]

    with _device_session(port) as (_, link):
        _apply(link, pairs, save)


@config.command("show")
@_PORT_OPTION
def config_show(port):
    """Show the device's current configuration."""
    with _device_session(port) as (target, link):
        r = link.info()
    if not r.ok:
        print_error(f"info failed: {r.message}")
        raise SystemExit(1)
    table = Table(title=f"BomberCat @ {target}", header_style="cyan bold")
    table.add_column("Field")
    table.add_column("Value")
    for key in ("fw", "role", "ssid", "server", "port", "session", "state"):
        table.add_row(key, r.data.get(key, "[dim]—[/dim]"))
    console.print(table)


# ── run / stop / status / monitor ─────────────────────────────────────────────

@click.command("run", context_settings={"help_option_names": ["-h", "--help"]})
@_PORT_OPTION
def run_cmd(port):
    """Start the relay (associate WiFi, connect the server, begin the session)."""
    with _device_session(port) as (target, link):
        try:
            r = link.run()
        except DeviceError:
            # The device took 'run' but never sent +OK/-ERR within the window.
            # Almost always WiFi association or the server connect is hanging.
            print_error("timed out waiting for the relay to start.")
            print_info(
                "the device accepted 'run' but did not confirm in time.\n"
                "  • check WiFi credentials and the nfcgate-server host/port"
                " (bombercat config show)\n"
                "  • watch the boot log live with:  bombercat monitor"
            )
            raise SystemExit(1)
        if r.ok:
            print_success(f"relay started on {target}")
            print_info("watch it with:  bombercat monitor   /   bombercat status")
        else:
            print_error(f"relay failed to start: {r.message}")
            print_info("check WiFi credentials and the nfcgate-server host/port "
                       "(bombercat config show)")
            raise SystemExit(1)


@click.command("stop", context_settings={"help_option_names": ["-h", "--help"]})
@_PORT_OPTION
def stop_cmd(port):
    """Stop the relay."""
    with _device_session(port) as (target, link):
        r = link.stop()
    print_success(f"relay stopped on {target}") if r.ok else \
        print_error(f"stop failed: {r.message}")


@click.command("status", context_settings={"help_option_names": ["-h", "--help"]})
@_PORT_OPTION
def status_cmd(port):
    """Show live relay status (state, link, peer, relayed count)."""
    with _device_session(port) as (target, link):
        r = link.status()
    if not r.ok:
        print_error(f"status failed: {r.message}")
        raise SystemExit(1)

    def yn(v):  # "1"/"0" -> yes/no
        return "[green]yes[/green]" if v == "1" else "[red]no[/red]"

    table = Table(title=f"Relay status @ {target}", header_style="cyan bold")
    table.add_column("Field")
    table.add_column("Value")
    table.add_row("state", r.data.get("state", "—"))
    table.add_row("link connected", yn(r.data.get("connected", "0")))
    table.add_row("peer present", yn(r.data.get("peer", "0")))
    table.add_row("APDU pairs relayed", r.data.get("relayed", "0"))
    console.print(table)


@click.command("monitor", context_settings={"help_option_names": ["-h", "--help"]})
@_PORT_OPTION
def monitor_cmd(port):
    """Stream the device's serial output live (relay logs + APDU hex). Ctrl-C to quit."""
    with _device_session(port) as (target, link):
        print_info(f"Monitoring {target} — press Ctrl-C to stop")
        try:
            for line in link.stream():
                if not line:
                    continue
                low = line.lower()
                if "cmd:" in low or "resp:" in low:  # RelayEngine APDU hex dumps
                    console.print(f"[cyan]{line}[/cyan]")
                elif line.startswith("-ERR") or "error" in low or "fail" in low:
                    console.print(f"[red]{line}[/red]")
                elif line.startswith((":", "+OK")):
                    console.print(f"[dim]{line}[/dim]")
                else:
                    console.print(line)
        except KeyboardInterrupt:
            console.print("\n[dim]stopped[/dim]")
