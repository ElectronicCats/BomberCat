# NFCGate relay sketch

A role-selectable [NFCGate](https://github.com/nfcgate/nfcgate)-compatible relay
endpoint for the BomberCat, built on
[`BomberCatCore`](../core/README.md). It pairs the BomberCat with a second
NFCGate peer — another BomberCat, or the NFCGate Android app — through an
`nfcgate-server`, relaying APDUs over WiFi/TCP.

```
[ physical card ] --RF--> [ BomberCat READER ] --WiFi/TCP--> nfcgate-server
                                                                   |
[ terminal/PoS ] <--RF--- [   peer  CARD/HCE ] <--WiFi/TCP--------+
```

**Both roles are implemented end to end** (NFCGATE_PLAN.md Fase 4 + Fase 5) and
the device is driven over USB-serial by the control CLI (Fase 6).

## What each role does

**READER** (reads a physical card):

1. Brings up the PN7150 in reader/writer mode.
2. Connects to `nfcgate-server` and sends `OP_SYN` — which both announces its
   presence and *registers it with the session* (the server only associates a
   client with a session once it sends a frame). It replies `OP_ACK` to the
   peer's `OP_SYN`, exactly as the NFCGate app's `NetworkManager` does.
3. For each command frame that arrives over TCP (an `NFCData` tagged `READER`),
   it activates the physical card if needed, replays the APDU to it, and sends
   the card's response back tagged `CARD`.

**CARD/HCE** (emulates a card to a terminal): reads the terminal's command over
RF, forwards it over TCP tagged `READER`, then injects the peer's `CARD`-tagged
response back to the terminal — the mirror of READER.

The data-source semantics (`READER` = command, `CARD` = response) and the
asymmetric framing are documented in
[`../core/proto/UPSTREAM.md`](../core/proto/UPSTREAM.md).

## Configuration

The device boots into a **SerialControl** REPL; configure it with the control CLI
(`tools/`, see [`../../tools/README.md`](../../tools/README.md)) over USB-serial —
values persist in flash (`ConfigStore`):

```sh
bombercat config wifi    --ssid MyNet --pass 's3cret'
bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
```

When no config is persisted, the sketch falls back to compile-time values in
[`arduino_secrets.h`](arduino_secrets.h):

| Macro | Meaning |
|---|---|
| `SECRET_SSID` / `SECRET_PASS` | WiFi credentials |
| `RELAY_SERVER` / `RELAY_PORT` | `nfcgate-server` host and TCP port (default 5566) |
| `RELAY_SESSION` | session byte (1..255) — **must match** the peer |
| `RELAY_ROLE` | `0` = READER, `1` = CARD/HCE |
| `RELAY_AUTOSTART` | `0` = boot into the control REPL and wait for the CLI's `run` (**required for CLI-driven use** — the default). `1` = start the relay on boot (standalone). With a non-empty SSID, `1` blocks setup() on a WiFi/TCP bring-up before the REPL starts, so the CLI can't reach the board; only use `1` for a standalone device you won't drive over USB. |

Persisted config (from the CLI) always takes precedence over these fallbacks.

## Wiring

The PN7150 pins are the BomberCat defaults baked into `NfcController`
(IRQ 11, VEN 13, I²C addr `0x28`); no wiring beyond a stock BomberCat is needed.
WiFi uses the on-board ESP32/NINA module via WiFiNINA, same as the legacy
`host_Relay_NFC` / `client_Relay_NFC` sketches.

## Build & flash

Board: `electroniccats:mbed_rp2040:bombercat`. Libraries: **WiFiNINA**,
**Electronic Cats PN7150**, plus the local **BomberCatCore**.

```sh
arduino-cli compile -b electroniccats:mbed_rp2040:bombercat \
  --library ../core .
arduino-cli upload  -b electroniccats:mbed_rp2040:bombercat -p /dev/ttyACM0 .
# (or use the Arduino IDE: board "Electronic Cats BomberCat", and symlink
#  ../core into ~/Arduino/libraries so #include <BomberCatCore.h> resolves.)
```

The WiFiNINA "architecture may be incompatible" warnings are expected — the
legacy relay sketches use it the same way on the BomberCat's NINA module.

The control link runs at **115200 baud**. After flashing, verify the device
answers over serial:

```sh
bombercat device info      # -> fw 0.9.7, state idle
```

Then configure it (above), point it at a running `nfcgate-server` with a card in
the field and a second peer (a `card`-role BomberCat or the NFCGate app) on the
same session byte, and start it:

```sh
bombercat run              # associate WiFi, connect server, begin session
bombercat status           # state / link / peer / relayed count
bombercat monitor          # live serial stream (relay logs + APDU hex)
```

Both ends of a relay are usually plugged into the same host. List them with
`bombercat device list` and address each one by its ID — every command above
takes `-d <ID>` (e.g. `bombercat run -d 2`); `bombercat identify -d 2`
blinks that board's LED so you can tell which is which. See `tools/README.md`
§ Multiple devices.

## Testing without hardware

The wire protocol (framing + SYN/ACK handshake + PSH loopback) is verified on a
host against a real `nfcgate-server`, no RF involved, by the codec host test:

```sh
tools/testserver/run.sh                              # terminal 1: local server
tools/testserver/codec_hosttest/build_and_run.sh     # terminal 2: PASS
```

On device, point the sketch at a running server with a card in the field and a
second peer (a `card`-role peer or the NFCGate app) on the same session byte.

## Status

**Validated end to end on real hardware** (NFCGATE_PLAN.md §15):

- **Path A** — two BomberCats through a live `nfcgate-server`: a full EMV
  transaction (SELECT PPSE → FCI → SELECT AID → GPO → READ RECORD → GET DATA)
  relays end to end, including consecutive transactions without hanging
  (2026-08-17).
- **Path B** — against the **NFCGate Android app**, both variants: B1 (BomberCat
  `reader` + phone as `card`/HCE) and B2 (BomberCat `card` + phone as `reader`)
  (2026-08-19).
- **APDU capture (Fase 8)** — the `:apdu` tap + `bombercat capture` write a
  Wireshark-openable pcap; validated on HW by capturing a real Path A EMV
  transaction (`CapturaWireshark.pcapng`).
- **Per-transaction latency** — brought down from ~12–15 s to **~4.5 s**
  (`firmware/LATENCIA_OPTIMIZACION.md`).

### Not yet done

- **Latency floor** — the remaining floor below ~4.5 s is architectural; the only
  way past it is an opt-in board-to-board *turbo* mode that breaks NFCGate
  compatibility (`firmware/REDISENO_COMUNICACION.md` §5.1). Open only if needed.
- **TLS** — later phase; the link is plain TCP for now.
- **Keepalive / WTX** — the loop reconnects on link error (including the
  half-open TCP case fixed for Path A) but does not yet send periodic keepalives
  or handle EMV WTX (a risk noted in the plan §8).
