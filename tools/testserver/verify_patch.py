#!/usr/bin/env python3
"""Verify that a RUNNING nfcgate-server carries the relay latency patch.

Grepping server.py proves the file on disk is patched; it does NOT prove the
server answering on the port is. With Docker they routinely disagree: the
Dockerfile COPYs server.py at build time, so patching the clone and restarting
the container leaves the old, slow code running. This checks the wire instead,
so it works against any deployment (Docker, systemd, a VPS you have no shell on).

What it measures — the observable signature of Fase E
(firmware/LATENCIA_OPTIMIZACION.md):

  Unpatched: send_to_clients() writes the 4-byte length header and the payload
             as two separate wfile.write() calls, and wfile is unbuffered
             (wbufsize=0), so they leave as TWO TCP segments. With Nagle on, the
             payload segment waits for the header's ACK -- i.e. the peer's
             ~40 ms delayed-ACK timer -- on EVERY server->client relay.

  Patched:   TCP_NODELAY is set and both parts go out in ONE coalesced write, so
             the peer's first recv() returns header+payload together, with no gap.

So we join two clients to one session, relay a frame, and look at what the
receiving peer's first recv() actually returns and how long the rest took.

Usage:
    python tools/testserver/verify_patch.py [host] [port] [-n rounds] [--json]

`--json` swaps the plain-text report for one JSON object per line, which is what
`bombercat testserver verify` consumes to draw its Rich report. The measurement
and the wording of every verdict live here either way; the CLI only styles them.

Exit codes: 0 patch active, 1 patch missing, 2 could not measure.

Needs the classic protobuf runtime (protobuf==3.20.3), same as relay_smoketest;
`bombercat testserver verify` bootstraps it for you.
"""
import argparse
import json
import os
import random
import socket
import statistics
import struct
import sys
import textwrap
import time

# Import the server's committed protobuf modules (repo ./server/plugins).
_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_REPO, "server"))
from plugins import c2c_pb2, c2s_pb2  # noqa: E402

# A frame arriving whole in the first recv() means one TCP segment: coalesced
# write, no Nagle stall. A first recv() of exactly the 4-byte header means the
# payload was still in flight -- the unpatched signature.
HEADER_LEN = 4
# Delayed-ACK timers are ~40 ms on Linux and up to 200 ms elsewhere. Anything
# above this threshold is a stall, not scheduling noise.
STALL_MS = 15.0
# Above this, the round trip is dominated by distance to the server, not by
# anything this patch can fix.
SLOW_RTT_MS = 50.0

# Every sentence the user reads about the verdict is written here, once, as a
# plain paragraph. Both renderers (this file's text mode and the CLI's Rich
# panels) wrap and style the same strings, so the wording cannot drift apart.
SPLIT_WHY = (
    "The server sent each frame as TWO TCP segments (header, then payload) — "
    "that is the unpatched send_to_clients(), so Fase E's coalesced write is not "
    "in the code answering on this port."
)
STALL_VISIBLE = (
    "The ~%.0f ms gap between the two segments is the Nagle/delayed-ACK stall "
    "itself, already costing you time on this path."
)
STALL_HIDDEN = (
    "The gap reads ~0 ms here because this path ACKs instantly (loopback / same "
    "LAN). Do not read that as harmless: over the boards' WiFi link the second "
    "segment waits for the delayed-ACK timer (~40 ms) on every one of the ~36 "
    "relays per transaction."
)
SPLIT_FIX = [
    "Apply tools/testserver/latency-fixes.patch on the server "
    "(docs/SERVIDOR_DEDICADO_NFCGATE.md section 2.2).",
    "With Docker you must REBUILD the image and recreate the container — a "
    "restart reuses the server.py baked in at build time.",
]
PATCH_OK = (
    "Every frame arrived as a single TCP segment — Fase E (TCP_NODELAY + "
    "coalesced write) is live in the code answering on this port."
)
ODD_NOTE = (
    "ODD: frames are coalesced, yet the remainder took ~%.0f ms to arrive. Re-run "
    "with more rounds (-n 30); if it persists, something on the path is "
    "fragmenting traffic (VPN, tunnel, low-MTU link)."
)
SLOW_NOTE = (
    "NOTE: a %.0f ms round trip is high. That is network distance, not Nagle — see "
    "docs/SERVIDOR_DEDICADO_NFCGATE.md section 5.1 for what RTT costs you per "
    "transaction (~72 one-way hops)."
)
UNREACHABLE_FIX = [
    "Check the server is up and listening on that port.",
    "Open TCP %d in the server's firewall (and in the VPS provider's).",
]


class RelayMismatch(RuntimeError):
    """The server handed the peer something other than what we relayed."""


def make_serverdata(apdu, source):
    nfc = c2c_pb2.NFCData()
    nfc.data_source = source
    nfc.data_type = c2c_pb2.NFCData.INITIAL
    nfc.timestamp = int(time.time() * 1000)
    nfc.data = apdu
    sd = c2s_pb2.ServerData()
    sd.opcode = c2s_pb2.ServerData.OP_PSH
    sd.data = nfc.SerializeToString()
    return sd.SerializeToString()


def send_frame(sock, payload, session):
    sock.sendall(struct.pack("!IB", len(payload), session) + payload)


def connect(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((host, port))
    # Our own send path must not add a stall we then blame on the server.
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def drain(sock):
    """Discard anything already queued on `sock`, leaving it blocking again."""
    sock.setblocking(False)
    try:
        while sock.recv(65536):
            pass
    except (BlockingIOError, OSError):
        pass
    sock.setblocking(True)
    sock.settimeout(10)


def relay_once(src, dst, blob, session):
    """Relay one frame and describe how the peer received it.

    Returns (first_chunk_len, gap_ms, total_ms): how many bytes the peer's first
    recv() produced, how long the remainder took to arrive after it, and the
    whole src->server->dst time.
    """
    t0 = time.perf_counter()
    send_frame(src, blob, session)

    chunk = dst.recv(65536)
    t_first = time.perf_counter()
    if not chunk:
        raise RuntimeError("connection closed by server")

    first_chunk_len = len(chunk)
    want = HEADER_LEN + struct.unpack("!I", chunk[:HEADER_LEN])[0]
    while len(chunk) < want:
        more = dst.recv(65536)
        if not more:
            raise RuntimeError("connection closed by server mid-frame")
        chunk += more
    t_done = time.perf_counter()

    return (
        first_chunk_len,
        (t_done - t_first) * 1000.0,
        (t_done - t0) * 1000.0,
        chunk[HEADER_LEN:want],
    )


class Reporter:
    """Where the run's progress goes: JSON lines for the CLI, or plain text.

    Text mode is what you get running this script by hand; JSON mode is what
    `bombercat testserver verify` reads line by line so it can draw a live
    progress bar and Rich panels without re-deriving any of the wording.
    """

    WIDTH = 74

    def __init__(self, as_json):
        self.as_json = as_json

    def _emit(self, **event):
        print(json.dumps(event), flush=True)

    @staticmethod
    def _wrap(text, indent="  ", first=None):
        return textwrap.fill(
            text,
            Reporter.WIDTH,
            initial_indent=first if first is not None else indent,
            subsequent_indent=indent,
        )

    def start(self, host, port, session, rounds):
        if self.as_json:
            self._emit(event="start", host=host, port=port,
                       session=session, rounds=rounds)
        else:
            print("Verifying %s:%d (session 0x%02X, %d rounds)\n"
                  % (host, port, session, rounds))

    def round(self, i, rounds, first_len, whole, gap_ms, total_ms):
        if self.as_json:
            self._emit(event="round", i=i, rounds=rounds, first_len=first_len,
                       whole=whole, gap_ms=gap_ms, total_ms=total_ms)
        else:
            print("  round %d/%d  first recv: %-3d B (%s)  gap after header: %6.2f ms"
                  " | relay total: %6.2f ms"
                  % (i, rounds, first_len,
                     "whole frame" if whole else "HEADER ONLY", gap_ms, total_ms))

    def result(self, verdict, headline, split, rounds, median_gap, median_total,
               notes, fix):
        if self.as_json:
            self._emit(event="result", verdict=verdict, headline=headline,
                       split=split, rounds=rounds, median_gap_ms=median_gap,
                       median_total_ms=median_total, notes=notes, fix=fix)
            return
        print("\n  frames split across segments : %d / %d" % (split, rounds))
        print("  median gap after header      : %.2f ms" % median_gap)
        print("  median relay round trip      : %.2f ms" % median_total)
        print("\nRESULT: %s" % headline)
        for note in notes:
            print()
            print(self._wrap(note))
        if fix:
            print("\n  Fix:")
            for i, step in enumerate(fix, 1):
                print(self._wrap(step, indent="       ", first="    %d. " % i))

    def error(self, message, fix=None):
        if self.as_json:
            self._emit(event="error", message=message, fix=fix or [])
            return
        print("\nERROR: %s" % message, file=sys.stderr)
        for i, step in enumerate(fix or [], 1):
            print(self._wrap(step, indent="     ", first="  %d. " % i),
                  file=sys.stderr)


def run(args, rep):
    """Measure the server and report a verdict. Returns the process exit code."""
    # Never 42: a live BomberCat defaults to RELAY_SESSION 42 and would inject
    # its own frames into our measurement (same reasoning as relay_smoketest).
    session = random.choice([b for b in range(1, 256) if b != 42])

    # Connect before announcing the run: an unreachable server should show its
    # error, not a session banner for a measurement that never started.
    reader = connect(args.host, args.port)
    card = connect(args.host, args.port)
    rep.start(args.host, args.port, session, args.rounds)

    # Both peers must be registered in the session before anything is relayed.
    # The server forwards each registration frame to whoever is already in the
    # session, so both sockets can be holding a frame we never asked for — drain
    # them, or the first measured round reads a registration instead of its own
    # payload.
    send_frame(card, make_serverdata(b"", c2c_pb2.NFCData.CARD), session)
    send_frame(reader, make_serverdata(b"", c2c_pb2.NFCData.READER), session)
    time.sleep(0.3)
    drain(reader)
    drain(card)

    ppse = bytes.fromhex("00A404000E325041592E5359532E444446303100")
    blob = make_serverdata(ppse, c2c_pb2.NFCData.READER)

    split = 0
    gaps, totals = [], []
    for i in range(args.rounds):
        first_len, gap_ms, total_ms, payload = relay_once(reader, card, blob, session)
        if payload != blob:
            raise RelayMismatch(
                "The relayed payload differs from what was sent — this server is "
                "not relaying correctly, so its latency cannot be judged."
            )
        whole = first_len >= HEADER_LEN + len(blob)
        if not whole:
            split += 1
        gaps.append(gap_ms)
        totals.append(total_ms)
        rep.round(i + 1, args.rounds, first_len, whole, gap_ms, total_ms)

    median_gap = statistics.median(gaps)
    median_total = statistics.median(totals)

    # The verdict rests on the SPLIT, not on the timing. Frame splitting is
    # structural: it is what the two unpatched wfile.write() calls do, and it
    # shows up on any path. The delayed-ACK stall is what that splitting COSTS,
    # and it only materialises where ACKs are actually delayed -- over loopback
    # or a Docker bridge the kernel ACKs instantly and the gap reads ~0 ms even
    # on a thoroughly unpatched server. Judging by time alone would clear a slow
    # server just because you tested it from the same machine.
    if split > args.rounds // 2:
        notes = [SPLIT_WHY]
        notes.append(STALL_VISIBLE % median_gap if median_gap > STALL_MS
                     else STALL_HIDDEN)
        rep.result("missing", "PATCH MISSING (or an old build is still running)",
                   split, args.rounds, median_gap, median_total, notes, SPLIT_FIX)
        return 1

    notes = [PATCH_OK]
    if median_gap > STALL_MS:
        notes.append(ODD_NOTE % median_gap)
    if median_total > SLOW_RTT_MS:
        notes.append(SLOW_NOTE % median_total)
    rep.result("active", "PATCH ACTIVE", split, args.rounds,
               median_gap, median_total, notes, [])
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="Check that a running nfcgate-server has the latency patch."
    )
    ap.add_argument("host", nargs="?", default="127.0.0.1")
    ap.add_argument("port", nargs="?", default=5566, type=int)
    ap.add_argument("-n", "--rounds", default=8, type=int,
                    help="relayed frames to measure (default: 8)")
    ap.add_argument("--json", action="store_true",
                    help="emit one JSON object per line instead of a text report")
    args = ap.parse_args()

    rep = Reporter(args.json)
    try:
        return run(args, rep)
    except RelayMismatch as e:
        rep.error(str(e))
        return 2
    except (ConnectionRefusedError, socket.timeout, OSError) as e:
        rep.error(
            "Could not reach the server at %s:%d (%s)." % (args.host, args.port, e),
            fix=[step % args.port if "%d" in step else step
                 for step in UNREACHABLE_FIX],
        )
        return 2


if __name__ == "__main__":
    sys.exit(main())
