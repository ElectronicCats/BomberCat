# Capture / Wireshark

`bombercat capture` taps a **copy** of every relayed APDU and turns each into an
ISO 14443 frame that Wireshark opens directly — live over a FIFO and/or written
to a `.pcap` file.

**The relay hot path is untouched.** The APDUs still travel over WiFi/TCP to the
`nfcgate-server`; capture only *consumes a copy* that the firmware echoes over the
control serial as `:apdu` events (see [protocol](protocol.md#apdu-capture-events)).
So capturing never slows or alters the relay.

Requires firmware ≥ 0.8.0 (the `capture` control command). The **live** feed also
needs Wireshark installed; a file-only capture does not. (docs/NFCGATE_PLAN.md Fase 8 / §16.)

---

## How it works

```
firmware  --":apdu cmd/resp ts hex"-->  bombercat capture  -->  ┌ live FIFO --> Wireshark (-k -i)
        (control serial, a copy)                                └ .pcap file (classic pcap)
```

1. `capture start` sends `capture on` to arm the firmware tap.
2. The firmware echoes each relayed APDU as `:apdu <dir> <ts_ms> <hex>`.
3. The host wraps each into an ISO 14443 pcap frame and fans it out to a live
   Wireshark FIFO and/or a file.
4. On Ctrl-C (or Wireshark closing with no file), it sends `capture off` and
   cleans up.

## Usage

```sh
bombercat capture start -ws                # live Wireshark only
bombercat capture start -ws -o emv.pcap    # live Wireshark + save to a file
bombercat capture start -o emv.pcap        # file only (no Wireshark needed)
bombercat capture stop                     # disarm a board left armed
```

At least one of `-ws` / `-o` is required. Press **Ctrl-C** to stop; the tap is
disarmed automatically. If you quit Wireshark while a file is also being written,
capture continues to the file; with no file, it stops cleanly.

Terminal output as APDUs flow:

```
→ card       1234 ms  00a404000e325041592e5359532e444446303100
← card       1290 ms  6f23840e325041592e5359532e4444463031a5119000
```

- `→ card` — a **command** (terminal → card), shown in Wireshark as `PCD → PICC`.
- `← card` — a **response** (card → terminal), shown as `PICC → PCD`.

Timestamps are the device's own `millis()` clock (ground-truth timing), anchored
to host wall-clock at the first APDU — so the *deltas* between frames are the
device's real timing.

### Which side to capture

In a two-board relay the APDU may be mutated in transit. Capture the **reader**
side for the pre-mutation APDU and the **card** side for the post-mutation one:

```sh
bombercat capture start -d 1 -ws              # reader side
bombercat capture start -d 2 -o post.pcap     # card side
```

---

## Classic pcap vs pcapng

This matters when you compare files:

- **The CLI writes classic pcap** (`-o file.pcap`) and feeds the live FIFO in the
  same classic-pcap format. That's what streams straight into Wireshark's
  `-k -i <fifo>`.
- **If you then Save from Wireshark**, its default format is **pcapng** — so a
  file you save from the GUI (e.g. the repo's
  [`CapturaWireshark.pcapng`](../../CapturaWireshark.pcapng) at the root) is
  pcapng, not the classic pcap the CLI wrote. Both carry the same frames; only
  the container format differs. To keep classic pcap from Wireshark, choose
  "Wireshark/tcpdump/... - pcap" in the Save-As dialog.

## Encapsulation: `DLT_ISO_14443`

Each APDU is wrapped so Wireshark's `iso14443` dissector parses it as a proper
ISO 14443-4 exchange (implemented in
[`modules/capture/pcap.py`](../modules/capture/pcap.py)):

- **Link type** `DLT_ISO_14443` (libpcap `LINKTYPE_ISO_14443`, 264) in the pcap
  global header.
- Each frame is a small **ISO 14443 pseudo-header** — `version(1) + event(1) +
  len(2, big-endian)` — followed by the APDU wrapped in an **I-block** (a PCB
  prologue byte, block number toggled per command). The I-block wrapper is what
  makes Wireshark read it as a real ISO 14443-4 block instead of flagging an
  "unknown command".
- **Event byte**: `0xFE` = PCD → PICC (command), `0xFF` = PICC → PCD (response) —
  the values Wireshark's dissector actually parses (verified against tshark 4.4).
- A command starts a new I-block (toggles the block number); the following
  response echoes it, so a capture reads like a real -4 exchange.

This is the same classic-pcap wire format catnip (CatSniffer) uses, so frames
stream identically into a live FIFO or a file.

## Verifying without hardware

The pcap writer and the ISO 14443 framing are checked against `tshark`:

```sh
python3 tools/tests/capture_hosttest.py       # pcap writer + ISO 14443 vs tshark
```
