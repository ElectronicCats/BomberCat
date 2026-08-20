#!/usr/bin/env python3

# Electronic Cats
# `bombercat capture` — arm the device APDU tap and turn its ":apdu" events into
# a classic-pcap stream, fed live into Wireshark (over a FIFO) and/or written to
# a .pcap file. The relay itself is untouched: APDUs still travel over WiFi/TCP,
# and this only consumes a copy over the control serial. docs/NFCGATE_PLAN.md Fase 8.
# Distributed as-is; no warranty is given.

import _thread
import platform
import re
import threading
import time
from typing import Optional

import click
import serial

from ..core.bombercat import DeviceError, DeviceLink
from ..core.pipes import UnixPipe, WindowsPipe, Wireshark, find_wireshark_path
from ..nfcgate.cli import _device_session
from ..utils.cli_options import target_options
from ..utils.output import (
    console,
    print_dim,
    print_error,
    print_info,
    print_success,
    print_warning,
)
from .pcap import PcapBuilder, global_header

# Matches the firmware event:  :apdu <cmd|resp> <ts_ms> <hex...>
_APDU_RE = re.compile(r"^:apdu\s+(cmd|resp)\s+(\d+)\s+([0-9a-fA-F]*)\s*$")

# How long to wait for Wireshark to attach to the FIFO before giving up.
_WIRESHARK_PIPE_TIMEOUT = 30


class _CaptureSink:
    """Fan pcap frames out to a live Wireshark FIFO and/or a .pcap file."""

    def __init__(self, pipe=None, fileobj=None):
        self.pipe = pipe
        self.fileobj = fileobj

    def _emit(self, data: bytes) -> None:
        if self.fileobj is not None:
            self.fileobj.write(data)
            self.fileobj.flush()
        if self.pipe is not None:
            self.pipe.write_packet(data)

    def file_header(self) -> None:
        """Write the pcap global header to the FILE only (the FIFO gets it later,
        once Wireshark has attached — writing to a FIFO with no reader blocks)."""
        if self.fileobj is not None:
            self.fileobj.write(global_header())
            self.fileobj.flush()

    def pipe_header(self) -> None:
        if self.pipe is not None:
            self.pipe.write_packet(global_header())

    def frame(self, data: bytes) -> None:
        self._emit(data)


def _new_pipe():
    """A fresh OS-appropriate FIFO/named pipe for the live Wireshark feed."""
    return WindowsPipe() if platform.system() == "Windows" else UnixPipe()


def _watch_wireshark(ws: Wireshark, sink: "_CaptureSink",
                     stop_event: threading.Event) -> None:
    """Notice when the user quits Wireshark and react without waiting for APDUs.

    The FIFO write end only breaks on the *next* write, so a capture that is
    idle would keep waiting on a pipe with no reader. Poll the process instead:
    detach the pipe from the sink and either carry on with the file, or — with
    no file to fall back to — interrupt the main thread so `start`'s normal
    Ctrl-C path disarms the tap and cleans up.
    """
    while not stop_event.wait(0.5):
        if not ws.has_exited():
            continue
        # Detach only; `start`'s finally block owns removing the pipe.
        sink.pipe = None
        if sink.fileobj is not None:
            print_warning("Wireshark closed — continuing to the file only.")
        else:
            print_warning("Wireshark closed — stopping capture.")
            _thread.interrupt_main()
        return


# ── capture group ─────────────────────────────────────────────────────────────

@click.group("capture", context_settings={"help_option_names": ["-h", "--help"]})
def capture():
    """Capture relayed APDUs to pcap (live Wireshark and/or a file)."""


@capture.command("start")
@click.option("-o", "--output", "output", type=click.Path(dir_okay=False),
              default=None, help="Also write a .pcap file (opens in Wireshark)."
)
@click.option(
    "--wireshark/--no-wireshark",
    "-ws/-nws",
    "wireshark",
    default=False,
    help="Launch Wireshark on a live FIFO (opt-in, like catnip's -ws).",
)
@click.option("--profile", default=None,
              help="Wireshark configuration profile to launch with.")
@target_options
def capture_start(output, wireshark, profile, port, device_id):
    """Arm the tap and stream APDUs to Wireshark and/or a file until Ctrl-C.

    \b
        bombercat capture start -ws                 # live Wireshark only
        bombercat capture start -ws -o emv.pcap     # live Wireshark + file
        bombercat capture start -o emv.pcap         # file only

    Run the relay (`bombercat run`) and tap a terminal on the card so APDUs
    flow; each command/response pair appears as an ISO 14443 frame. Capture the
    reader side for the pre-mutation APDU, the card side for the post-mutation
    one.
    """
    if not wireshark and not output:
        print_error("nothing to do: pass -o FILE to record, -ws to open Wireshark.")
        raise SystemExit(1)

    # A Wireshark binary is required only for the live feed; a file-only capture
    # never needs it. Bail early with a clear message rather than hanging on a
    # FIFO no one will read.
    ws_thread: Optional[Wireshark] = None
    ws_stop = threading.Event()
    if wireshark and find_wireshark_path() is None:
        if output:
            print_warning("Wireshark not found; capturing to the file only.")
            wireshark = False
        else:
            print_error(
                "Wireshark not found. Install it, or use -o FILE to capture "
                "to a file.")
            raise SystemExit(1)

    with _device_session(port, device_id) as (target, link):
        # Arm the device tap. -ERR here means the firmware predates capture.
        r = link.command("capture on")
        if not r.ok:
            print_error(f"could not arm capture: {r.message}")
            if "unknown command" in r.message:
                print_info("this firmware predates `capture` — reflash "
                           "firmware/NFCGate (>= v0.8.0) to use it.")
            raise SystemExit(1)

        pipe = None
        fileobj = None
        try:
            if output:
                fileobj = open(output, "wb")
                print_success(f"writing pcap to {output}")

            sink = _CaptureSink(pipe=None, fileobj=fileobj)
            sink.file_header()

            if wireshark:
                pipe = _new_pipe()
                ws_thread = Wireshark(pipe.pipe_path, profile=profile)
                ws_thread.start()
                # Opening the FIFO for write blocks until Wireshark opens the
                # read end, so do it in the background and wait on ready_event.
                threading.Thread(target=pipe.open, daemon=True).start()
                print_info("waiting for Wireshark to attach…")
                if not pipe.ready_event.wait(timeout=_WIRESHARK_PIPE_TIMEOUT):
                    print_error("Wireshark did not attach to the pipe in time.")
                    if not fileobj:
                        raise SystemExit(1)
                    print_warning("continuing with the file only.")
                    pipe = None
                else:
                    sink.pipe = pipe
                    sink.pipe_header()
                    print_success("Wireshark attached — streaming APDUs")
                    threading.Thread(
                        target=_watch_wireshark,
                        args=(ws_thread, sink, ws_stop),
                        daemon=True,
                    ).start()

            print_info(f"capturing from {target} — press Ctrl-C to stop")
            _pump(link, sink)

        except KeyboardInterrupt:
            console.print("\n[dim]stopping capture…[/dim]")
        finally:
            # Best-effort: disarm the tap and tear down the sinks. The port is
            # closed by _device_session; we just clean up our own resources.
            ws_stop.set()
            try:
                link.command("capture off")
            except DeviceError:
                pass
            if fileobj is not None:
                fileobj.close()
            if pipe is not None:
                pipe.remove()


def _pump(link: DeviceLink, sink: _CaptureSink) -> None:
    """Read the device's serial, convert each ":apdu" event to a pcap frame, and
    write it to the sink. Blocks until the link drops or the caller interrupts."""
    builder = PcapBuilder()
    count = 0
    anchor_wall: Optional[float] = None
    anchor_dev: Optional[int] = None

    for line in link.stream():
        m = _APDU_RE.match(line.strip())
        if not m:
            continue
        direction, ts_ms_str, hexstr = m.group(1), m.group(2), m.group(3)
        try:
            apdu = bytes.fromhex(hexstr)
        except ValueError:
            print_warning(f"skipping malformed APDU hex: {hexstr!r}")
            continue
        if not apdu:
            continue

        # Anchor the device's millis() clock to host wall-clock at the first
        # event, so the pcap carries real timestamps whose *deltas* are the
        # device's ground-truth timing.
        ts_ms = int(ts_ms_str)
        if anchor_wall is None:
            anchor_wall = time.time()
            anchor_dev = ts_ms
        ts_seconds = anchor_wall + (ts_ms - anchor_dev) / 1000.0

        try:
            sink.frame(builder.frame(direction, apdu, ts_seconds))
        except BrokenPipeError:
            sink.pipe = None
            if sink.fileobj is None:
                print_warning("Wireshark closed the pipe; stopping capture.")
                return
            print_warning("Wireshark closed the pipe; continuing to the file only.")
        count += 1
        arrow = "→ card " if direction == "cmd" else "← card "
        console.print(f"[cyan]{arrow}[/cyan] [dim]{ts_ms:>8} ms[/dim]  {apdu.hex()}")

    print_dim(f"link ended after {count} APDU frame(s)")


# ── capture stop (disarm a board left armed) ──────────────────────────────────

@capture.command("stop")
@target_options
def capture_stop(port, device_id):
    """Disarm the tap on a board (e.g. one left armed by an interrupted start)."""
    with _device_session(port, device_id) as (target, link):
        r = link.command("capture off")
    if r.ok:
        print_success(f"capture disarmed on {target}")
    else:
        print_error(f"capture off failed: {r.message}")
        raise SystemExit(1)
