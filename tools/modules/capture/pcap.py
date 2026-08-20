#!/usr/bin/env python3

# Electronic Cats
# pcap.py — turn relayed APDUs into classic-pcap frames Wireshark opens directly.
# The link type is DLT_ISO_14443 (264): each APDU is wrapped so Wireshark's
# iso14443 dissector parses it as an ISO 14443-4 I-block with a direction tag.
# Same classic-pcap wire format catnip uses (protocol/common.py), so it streams
# straight into a live FIFO or a .pcap file. docs/NFCGATE_PLAN.md Fase 8 / §16.
# Distributed as-is; no warranty is given.

import struct

# libpcap LINKTYPE_ISO_14443 (see /usr/include/pcap/dlt.h: DLT_ISO_14443 264).
DLT_ISO_14443 = 264

# Classic pcap headers.
PCAP_MAGIC = 0xA1B2C3D4
_GLOBAL_HEADER = "<LHHIILL"   # magic, ver_major, ver_minor, thiszone, sigfigs, snaplen, network
_PACKET_HEADER = "<LLLL"      # ts_sec, ts_usec, incl_len, orig_len
SNAPLEN = 0xFFFF

# ISO 14443 pcap pseudo-header: version(1) + event(1) + len(2, big-endian) + data.
# The event values below are what Wireshark's iso14443 dissector actually parses
# (verified against tshark 4.4): 0xFE = a transfer PCD -> PICC (the terminal/
# reader talking to the card, i.e. a *command*); 0xFF = PICC -> PCD (the card
# answering, i.e. a *response*).
_ISO14443_VERSION = 0x00
EVT_PCD_TO_PICC = 0xFE  # command  (terminal -> card)
EVT_PICC_TO_PCD = 0xFF  # response (card -> terminal)

# ISO 14443-4 I-block prologue byte (PCB). Wrapping the bare APDU in an I-block
# makes Wireshark parse it as a proper ISO 14443-4 block instead of flagging an
# "unknown command"; the low bit is the block number, toggled per command so a
# capture reads like a real -4 exchange. 0b000000_1_0 = I-block, block number 0.
_IBLOCK_BASE = 0x02


def global_header(dlt: int = DLT_ISO_14443) -> bytes:
    """The one-time pcap file/stream header. Write it once before any packet."""
    return struct.pack(
        _GLOBAL_HEADER, PCAP_MAGIC, 2, 4, 0, 0, SNAPLEN, dlt
    )


def iso14443_payload(is_command: bool, apdu: bytes, block_no: int = 0) -> bytes:
    """Wrap one APDU as an ISO 14443 frame body (pseudo-header + I-block)."""
    pcb = _IBLOCK_BASE | (block_no & 1)
    iblock = bytes([pcb]) + apdu
    event = EVT_PCD_TO_PICC if is_command else EVT_PICC_TO_PCD
    return bytes([_ISO14443_VERSION, event]) + struct.pack(">H", len(iblock)) + iblock


def record(payload: bytes, ts_seconds: float) -> bytes:
    """A full pcap record: per-packet header + payload. `ts_seconds` is a float
    epoch time (integer seconds + microseconds)."""
    sec = int(ts_seconds)
    usec = int(round((ts_seconds - sec) * 1_000_000))
    if usec >= 1_000_000:  # rounding can push it over; carry into seconds
        sec += 1
        usec -= 1_000_000
    return struct.pack(_PACKET_HEADER, sec, usec, len(payload), len(payload)) + payload


class PcapBuilder:
    """Stateful frame factory: keeps the alternating I-block number so a whole
    transaction reads correctly, and maps the firmware's ``cmd``/``resp``
    direction to the ISO 14443 command/response events.

    A command starts a new I-block (toggles the block number); the response that
    follows echoes that block number, as ISO 14443-4 does.
    """

    def __init__(self) -> None:
        self._block_no = 0

    def frame(self, direction: str, apdu: bytes, ts_seconds: float) -> bytes:
        """Build one pcap record for a ``cmd`` or ``resp`` APDU."""
        is_command = direction == "cmd"
        if is_command:
            self._block_no ^= 1  # new exchange
        payload = iso14443_payload(is_command, apdu, self._block_no)
        return record(payload, ts_seconds)
