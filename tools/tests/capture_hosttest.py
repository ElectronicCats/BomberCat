#!/usr/bin/env python3

# Electronic Cats
# capture_hosttest.py — verify the pcap capture path (tools/modules/capture)
# without hardware: feed a synthetic ":apdu" transcript through the same regex
# and PcapBuilder the CLI uses, write a .pcap, and (when tshark is available)
# confirm Wireshark disssects it as ISO 14443 with the right per-frame direction.
#
# This is the host-side verification for docs/NFCGATE_PLAN.md Fase 8 / §16: the pcap
# writer + DLT_ISO_14443 encapsulation. It complements codec_hosttest (RF wire
# format) and serialctl_hosttest (control protocol).
#
# Run:  python3 tools/tests/capture_hosttest.py

import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# tools/tests/capture_hosttest.py -> tools/ on sys.path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from modules.capture.cli import _APDU_RE  # noqa: E402
from modules.capture.pcap import PcapBuilder, global_header  # noqa: E402

# A trimmed EMV contactless transcript (SELECT PPSE -> ... -> a response),
# rendered as the firmware would emit it over serial while capture is armed.
TRANSCRIPT = [
    ":apdu cmd 1000 00a404000e325041592e5359532e444446303100",
    ":apdu resp 1180 6f1a840e325041592e5359532e4444463031a5089000",
    ":apdu cmd 1600 00a4040007a000000004101000",
    ":apdu resp 1850 6f2a840e325041592e5359532e4444463031a5089000",
    "RelayEngine: reader vivo, esperando comando del peer",  # log noise: ignored
    ":apdu cmd 2200 80a8000002830000",
    ":apdu resp 2500 770e82021980940a0801010010010301009000",
]


def build_pcap(path: Path) -> int:
    """Parse the transcript exactly as the CLI's _pump does and write a pcap.
    Returns the number of APDU frames written."""
    builder = PcapBuilder()
    anchor_wall = None
    anchor_dev = None
    n = 0
    with open(path, "wb") as f:
        f.write(global_header())
        for line in TRANSCRIPT:
            m = _APDU_RE.match(line.strip())
            if not m:
                continue
            direction, ts_ms_str, hexstr = m.group(1), m.group(2), m.group(3)
            apdu = bytes.fromhex(hexstr)
            ts_ms = int(ts_ms_str)
            if anchor_wall is None:
                anchor_wall = time.time()
                anchor_dev = ts_ms
            ts_seconds = anchor_wall + (ts_ms - anchor_dev) / 1000.0
            f.write(builder.frame(direction, apdu, ts_seconds))
            n += 1
    return n


def expected_events() -> list:
    """The ISO 14443 event byte each APDU should carry: cmd -> 0xfe, resp ->
    0xff (per pcap.py / verified against Wireshark)."""
    out = []
    for line in TRANSCRIPT:
        m = _APDU_RE.match(line.strip())
        if m:
            out.append("0xfe" if m.group(1) == "cmd" else "0xff")
    return out


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        pcap = Path(d) / "capture.pcap"
        n = build_pcap(pcap)
        want = len(expected_events())
        assert n == want, f"wrote {n} frames, expected {want}"
        print(f"[*] wrote {n} APDU frames to a pcap ({pcap.stat().st_size} bytes)")

        tshark = shutil.which("tshark")
        if not tshark:
            print("[!] tshark not found — skipping Wireshark dissection check.")
            print("    (install Wireshark/tshark to fully verify §16.)")
            print("CAPTURE HOST TEST PASSED (writer only)")
            return 0

        # Dissect and check: one row per frame, all ISO 14443, correct direction.
        out = subprocess.run(
            [tshark, "-r", str(pcap), "-T", "fields",
             "-e", "frame.protocols", "-e", "iso14443.event"],
            capture_output=True, text=True, check=True,
        ).stdout.strip().splitlines()
        assert len(out) == n, f"tshark saw {len(out)} frames, expected {n}"

        want_events = expected_events()
        for i, (row, ev) in enumerate(zip(out, want_events), start=1):
            protocols, _, event = row.partition("\t")
            assert "iso14443" in protocols, f"frame {i}: not iso14443 ({protocols!r})"
            assert event == ev, f"frame {i}: event {event!r}, expected {ev!r}"

        # No packet may be flagged malformed.
        malformed = subprocess.run(
            [tshark, "-r", str(pcap), "-Y", "_ws.malformed"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        assert not malformed, f"tshark flagged malformed frames:\n{malformed}"

        print(f"[*] tshark dissected all {n} frames as ISO 14443, directions OK, "
              "none malformed")
        print("CAPTURE HOST TEST PASSED")
        return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as e:
        print(f"CAPTURE HOST TEST FAILED: {e}")
        sys.exit(1)
