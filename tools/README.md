# `bombercat` — control CLI & dev tooling

A `click`/`rich` command-line tool for the NFCGate relay firmware. It talks to a
BomberCat over **USB-serial** to configure it, start/stop the relay, watch it
live and capture the relayed APDUs to Wireshark.

**Control plane only.** No APDUs travel over serial — those go over WiFi/TCP to
the `nfcgate-server`. The USB link is a text, line-based control channel: it
configures, arms, starts, monitors and captures the device, nothing more
(NFCGATE_PLAN.md Fase 6). Keep this in mind everywhere below.

```mermaid
flowchart LR
    card([Card]) -->|RF| reader[BomberCat READER]
    reader -->|WiFi / TCP| server[nfcgate-server]
    server -->|WiFi / TCP| emu[BomberCat CARD]
    emu -->|RF| term([Terminal])

    cli[bombercat CLI]
    cli -. "USB-serial<br/>(control plane:<br/>config / run / status / capture)" .-> reader
    cli -. "USB-serial<br/>(control plane:<br/>config / run / status / capture)" .-> emu

    subgraph data [APDU data plane]
        reader
        server
        emu
    end
```

## Install

```sh
cd tools
python3 -m pip install -r requirements.txt   # click, rich, pyserial (+ a few extras)
python3 bombercat.py --help
```

A virtualenv is recommended. On Linux, serial access usually needs your user in
the `dialout` group — see [Troubleshooting](docs/troubleshooting.md#serial-permission-denied).

Throughout the docs the command is written as `bombercat`; if you have not set up
the [`bombercat` alias](docs/reference.md#invocation) or
[shell completion](docs/reference.md#completion), run it as
`python3 bombercat.py …` from `tools/`.

## Quick start

```sh
# 1. Discover the board(s)
bombercat device list                 # IDs + serial ports; ✓ = answered the handshake
bombercat device info                 # firmware version + current config

# 2. Configure (persisted to flash unless --no-save)
bombercat config wifi    --ssid MyNet --pass 's3cret'
bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
bombercat config show

# 3. Run & watch
bombercat run                         # associate WiFi, connect server, start relay
bombercat status                      # state / link / peer / relayed count
bombercat monitor                     # live serial stream (relay logs + APDU hex)
bombercat stop

# 4. Capture the relayed APDUs to Wireshark
bombercat capture start -ws           # live Wireshark on a FIFO (Ctrl-C to stop)
```

A relay needs **two** peers on the same `--server` and `--session`: one
`--role reader` (reads a physical card), the other `--role card` (emulates one to
a terminal). Both boards are usually plugged into the same host — address each
one by its ID with `-d/--device`. See the
[end-to-end guide](docs/usage.md).

## Documentation

| Page | What's in it |
|---|---|
| [Command reference](docs/reference.md) | Every command and subcommand: purpose, flags, examples, expected output. `device`, `config`, `run`/`stop`/`status`/`monitor`, `identify`, `capture`, `proto`, `testserver`, `completion`, and device selection with `-d`/`-p`. |
| [End-to-end usage](docs/usage.md) | The real workflow on hardware — two BomberCats via `nfcgate-server` (Path A) and against the NFCGate Android app (Path B) — config → run → monitor → capture. |
| [Control protocol](docs/protocol.md) | The line-based `SerialControl` protocol (`:key value`, `+OK`, `-ERR`), the `DeviceLink` client, and how ports are discovered and numbered. For developers. |
| [Capture / Wireshark](docs/capture.md) | How `capture` taps a copy of every relayed APDU, the classic-pcap vs pcapng distinction, and the `DLT_ISO_14443` encapsulation. |
| [Troubleshooting](docs/troubleshooting.md) | Serial permissions, board not detected, old firmware without `identify`/`capture`, `run` timeouts. |

## Dev tooling

These wrap the reproducible build/test scripts (see NFCGATE_PLAN.md Fases 1–5);
full details in the [reference](docs/reference.md#dev-tooling):

```sh
bombercat proto gen                   # regenerate firmware/core/src/proto/*.pb.{c,h}
bombercat testserver run [-p 5566]    # local nfcgate-server in Docker
bombercat testserver smoke [host port]# relay smoke test (no RF)
```

See [`testserver/README.md`](testserver/README.md) for the local server fixture.

## Tests (dev-only, no hardware)

```sh
python3 tools/tests/serialctl_hosttest.py         # DeviceLink protocol parser (pty)
python3 tools/tests/capture_hosttest.py           # pcap writer + ISO 14443 vs tshark
tools/testserver/codec_hosttest/build_and_run.sh  # firmware codec vs live server
```
