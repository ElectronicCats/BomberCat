#!/usr/bin/env python3

# Electronic Cats
# `bombercat config|run|stop|status|monitor` — configure and drive the NFCGate
# relay over the control protocol (docs/NFCGATE_PLAN.md Fase 6).
# Distributed as-is; no warranty is given.

import time
from contextlib import contextmanager
from typing import Iterator, List, Optional, Tuple

import click
import serial
from rich.table import Table

from ..core.bombercat import DeviceError, DeviceLink, resolve_port
from ..utils.cli_options import target_options
from ..utils.output import (
    console,
    print_error,
    print_info,
    print_success,
    print_warning,
)


@contextmanager
def _device_session(port: Optional[str],
                    device_id: Optional[int] = None
                    ) -> Iterator[Tuple[str, DeviceLink]]:
    """Open a verified link, yield ``(target, link)``, and always close it.

    The board is picked by ``--port`` (raw path) or ``--device/-d`` (ID from
    `bombercat device list`), falling back to handshake auto-detection when a
    single BomberCat is attached — see ``core.bombercat.resolve_port``.

    Any ``DeviceError`` or serial/OS error — whether raised while connecting
    OR while running commands inside the ``with`` block — is reported as a
    clean one-line message and exits with status 1, instead of letting a
    traceback reach the user. ``SystemExit`` raised by the body (e.g. a failed
    ``set``) passes through untouched, and the port is closed either way.
    """
    link: Optional[DeviceLink] = None
    try:
        target = resolve_port(port, device_id)
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


def _blink(link: DeviceLink, target: str) -> None:
    """Blink the LED of the board we just configured, so `-d 2` can be matched
    to a physical device on the desk without counting cables.

    Never fatal: the configuration is already applied and saved by the time we
    get here, so a board that can't blink (pre-0.7.0 firmware has no
    `identify`) only earns a warning.
    """
    try:
        r = link.identify()
    except DeviceError as e:
        print_warning(f"could not blink {target}: {e}")
        return
    if r.ok:
        print_info(f"{target} is blinking its LED — that's the board you just "
                   "configured")
    elif "unknown command" in r.message:
        print_warning("this firmware predates `identify` — reflash "
                      "firmware/NFCGate to see which board was configured")
    else:
        print_warning(f"identify failed: {r.message}")


# ── config group ──────────────────────────────────────────────────────────────

@click.group("config", context_settings={"help_option_names": ["-h", "--help"]})
def config():
    """Configure the relay (WiFi + nfcgate parameters), persisted in flash."""


@config.command("wifi")
@click.option("--ssid", required=True, help="WiFi network name.")
@click.option("--password", "--pass", "password", default="",
              help="WiFi passphrase (empty for an open network).")
@click.option("--save/--no-save", default=True, help="Persist to flash (default: save).")
@target_options
def config_wifi(ssid, password, save, port, device_id):
    """Set the WiFi credentials."""
    with _device_session(port, device_id) as (target, link):
        _apply(link, [("ssid", ssid), ("pass", password)], save)
        _blink(link, target)


@config.command("nfcgate")
@click.option("--server", required=True,
              help="nfcgate-server as host or host:port.")
@click.option("--session", type=click.IntRange(1, 255), required=True,
              help="Session byte 1..255; both peers must match.")
@click.option("--role", type=click.Choice(["reader", "card"]), required=True,
              help="reader = read a physical card, card = emulate to a terminal.")
@click.option("--save/--no-save", default=True, help="Persist to flash (default: save).")
@target_options
def config_nfcgate(server, session, role, save, port, device_id):
    """Set the nfcgate-server, session and role."""
    host, _, port_str = server.partition(":")
    pairs = [("server", host)]
    if port_str:
        if not port_str.isdigit() or not (1 <= int(port_str) <= 65535):
            print_error(f"invalid port in --server: {port_str!r}")
            raise SystemExit(1)
        pairs.append(("port", port_str))
    pairs += [("session", str(session)), ("role", role)]

    with _device_session(port, device_id) as (target, link):
        _apply(link, pairs, save)
        _blink(link, target)


@config.command("show")
@target_options
def config_show(port, device_id):
    """Show the device's current configuration."""
    with _device_session(port, device_id) as (target, link):
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

# Overall wall-clock budget for the relay to reach 'relaying' after `run` is
# accepted. Covers the firmware worst case (keep in sync with NFCGate.ino):
#   WiFi associate (20 s) + NFC bring-up + TCP connect (8 s) + SYN + margin.
_RUN_BRINGUP_TIMEOUT = 45.0
_RUN_POLL_INTERVAL = 0.5  # seconds between `status` polls


@click.command("run", context_settings={"help_option_names": ["-h", "--help"]})
@target_options
def run_cmd(port, device_id):
    """Start the relay (associate WiFi, connect the server, begin the session)."""
    with _device_session(port, device_id) as (target, link):
        # Phase 1: `run` is non-blocking on the device — it only ACCEPTS the
        # request and starts the bring-up in the background. A -ERR here means it
        # couldn't even start (e.g. empty SSID, already running).
        try:
            r = link.run()
        except DeviceError as e:
            print_error(f"device did not accept 'run': {e}")
            print_info("is it still plugged in and running the relay firmware?")
            raise SystemExit(1)
        if not r.ok:
            print_error(f"relay rejected 'run': {r.message}")
            print_info("check the configuration with:  bombercat config show")
            raise SystemExit(1)

        # Phase 2: poll `status` and report progress until the relay reaches
        # 'relaying' (success), 'error' (clean failure), or our budget runs out.
        print_info("relay accepted 'run'; bringing up…")
        deadline = time.monotonic() + _RUN_BRINGUP_TIMEOUT
        last_detail = None
        while time.monotonic() < deadline:
            try:
                s = link.status()
            except DeviceError:
                # A single bring-up phase (NFC init, TCP connect) can briefly
                # occupy the firmware and let a status poll time out. That's
                # expected — keep polling until our own deadline.
                time.sleep(_RUN_POLL_INTERVAL)
                continue

            state = s.data.get("state", "")
            detail = s.data.get("detail", "")
            if detail and detail != last_detail:
                print_info(f"  … {detail}")
                last_detail = detail

            if state == "relaying":
                print_success(f"relay started on {target}")
                print_info("watch it with:  bombercat monitor   /   bombercat status")
                return
            if state == "error":
                print_error(f"relay failed to start: {detail or 'bring-up error'}")
                print_info("check WiFi credentials and the nfcgate-server host/port "
                           "(bombercat config show)")
                raise SystemExit(1)
            time.sleep(_RUN_POLL_INTERVAL)

        # Budget exhausted without reaching relaying/error: still connecting or
        # stuck in a phase. Unlike before, the device is NOT wedged — the REPL
        # stayed live — so status keeps working and points at the real culprit.
        print_error("relay did not reach 'relaying' in time.")
        print_info(
            f"still '{last_detail or 'connecting'}' after {int(_RUN_BRINGUP_TIMEOUT)}s"
            " — the bring-up is slow or stuck (the device is still responsive).\n"
            "  • keep watching:  bombercat status   /   bombercat monitor\n"
            "  • is the nfcgate-server listening?  (nc -vz <host> <port>)\n"
            "  • is the PN7150 responding?  watch:  bombercat monitor\n"
            "  • confirm host/port with:  bombercat config show"
        )
        raise SystemExit(1)


@click.command("stop", context_settings={"help_option_names": ["-h", "--help"]})
@target_options
def stop_cmd(port, device_id):
    """Stop the relay."""
    with _device_session(port, device_id) as (target, link):
        r = link.stop()
    print_success(f"relay stopped on {target}") if r.ok else \
        print_error(f"stop failed: {r.message}")


@click.command("status", context_settings={"help_option_names": ["-h", "--help"]})
@target_options
def status_cmd(port, device_id):
    """Show live relay status (state, link, peer, relayed count)."""
    with _device_session(port, device_id) as (target, link):
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
@target_options
def monitor_cmd(port, device_id):
    """Stream the device's serial output live (relay logs + APDU hex). Ctrl-C to quit."""
    with _device_session(port, device_id) as (target, link):
        print_info(f"Monitoring {target} — press Ctrl-C to stop")
        # The relay runs silent by default (firmware log level Warn); raise it to
        # Debug so the per-APDU hex dumps this view highlights are actually
        # emitted. Restore Warn on exit so we don't leave the hot path chatty.
        try:
            link.command("loglevel 4")
        except Exception:
            pass  # older firmware without `loglevel`: stream whatever it prints
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
        finally:
            try:
                link.command("loglevel 2")  # back to silent (Warn)
            except Exception:
                pass
