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
The tool is developed and tested on Linux; read
[Current limitations](#current-limitations) before running it elsewhere.

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

## Current limitations

Where the tool stands today (`VERSION` 1.1.0.0) — known constraints, not bugs.

### Platform

Everything here is developed and validated on **Linux**; that is the only OS the
whole tool is exercised on. The control plane is plain `pyserial` with nothing
Linux-specific in it, but several commands shell out to `bash` scripts or to
POSIX-only APIs, so support thins out elsewhere:

| Feature | Linux | macOS | Windows |
|---|---|---|---|
| `device`, `config`, `run`/`stop`/`status`/`monitor` | tested | should work, untested | should work, untested |
| `capture start -o file.pcap` | tested | should work, untested | should work, untested |
| `capture start -ws` (live Wireshark) | tested | FIFO path, untested | needs `pywin32`, untested |
| `completion install` | bash/zsh/fish | bash/zsh/fish | not offered |
| `proto gen` | tested | should work | needs `bash` (WSL / Git Bash) |
| `testserver run` | tested | needs Docker Desktop | needs `bash` + Docker |
| `tools/tests/` host tests | tested | should work | `serialctl_hosttest.py` needs `os.openpty()` |
| `testserver/codec_hosttest` | tested | needs `g++` | needs `bash` + `g++` |

Concretely, the non-Linux gaps are:

- `bombercat completion` is only registered on Linux/macOS
  ([modules/core/cli.py](modules/core/cli.py)); on Windows it is absent from
  `--help` and refuses to install.
- `proto gen` and `testserver run` are wrappers that run `bash gen_proto.sh` /
  `bash testserver/run.sh`, so they need a POSIX shell.
- `testserver run` needs Docker **and** a user who can reach its socket. The
  preflight diagnoses `docker`-group membership through the `grp` module, which
  only exists on Unix; elsewhere it can only report "unknown" and give a generic
  hint.
- The Wireshark launcher knows install paths for Windows/Linux/Darwin only; any
  other OS gets *"We don't support this OS yet"*.
- Serial access on Linux needs the `dialout` group, and the firmware flasher
  ([scripts/flash_bombercat.sh](../scripts/flash_bombercat.sh)) auto-installs
  its dependencies through `apt` — on non-Debian distros you install
  `arduino-cli` yourself.

### Host requirements it cannot work around

- **Live capture needs Wireshark installed locally**, in one of the usual
  install paths or on `PATH`. A flatpak/snap install that exports no `wireshark`
  wrapper is not detected. There is no remote capture (no SSH, no extcap).
- **The pipe name is fixed** (`/tmp/fbombercat`, `\\.\pipe\fbombercat`) with no
  flag to change it, so there can be **only one live capture per host**.
  Capturing both boards live at once collides on that FIFO — capture one side
  with `-o file.pcap` and the other with `-ws`, or do them one after the other.
- **Classic pcap only.** The writer emits classic pcap with `DLT_ISO_14443`; no
  pcapng, no per-packet comments
  ([capture.md](docs/capture.md#classic-pcap-vs-pcapng)).
- `tools/tests/capture_hosttest.py` checks the dissection only when `tshark` is
  installed; without it that half of the test is skipped.
- `testserver smoke` needs the classic protobuf 3.x runtime. If the interpreter
  running the CLI lacks it, a throwaway venv is bootstrapped in
  `tools/.venv-smoke` (same idea as `.venv-proto` for `proto gen`).

### Devices and serial

- **One command per board at a time.** The port is not opened exclusively, so a
  second command against the same board steals bytes from the first — `monitor`
  and `capture start` cannot share one BomberCat. Two *different* boards in
  parallel are fine.
- **`monitor` raises the firmware log level to Debug** while it runs (restored
  on exit), which makes the relay's hot path chattier — worth remembering before
  measuring latency with it open.
- **Device IDs are stable only while the set of attached boards is unchanged.**
  They are derived from the USB iSerial (or the USB topology location) and then
  numbered in order, so plugging in or removing a board renumbers the rest.
  Re-check with `bombercat device list` before reusing a `-d` from an earlier
  session.
- **VID/PID matching is a hint, not proof.** Sketches built against the stock
  Arduino Mbed profile enumerate as `2341:005E`, so a real Arduino Nano RP2040
  Connect on the same host is tagged as a candidate too. Only the ✓ in
  `device list` (the control handshake) confirms a board is a BomberCat.
- **Firmware floors.** `capture` needs firmware ≥ v0.8.0, and `identify` /
  `loglevel` need a recent build; against older firmware those commands fail
  with `-ERR unknown command`
  ([troubleshooting.md](docs/troubleshooting.md#old-firmware-without-identify--capture)).
- **The CLI does not flash firmware.** That is
  [scripts/flash_bombercat.sh](../scripts/flash_bombercat.sh)'s job (`bash` +
  `arduino-cli`).

### Relay scope

- **Exactly two peers per session** — one `--role reader` and one `--role card`,
  sharing a `--server` and a `--session` (1–255). No multi-peer sessions, and no
  more than one relay session per board.
- **`run` has a fixed 45 s bring-up budget**, not adjustable by flag. A timeout
  does not wedge the board: the REPL stays live, so `status`/`monitor` keep
  answering ([troubleshooting.md](docs/troubleshooting.md#run-times-out)).
- **Nothing in the chain is authenticated or encrypted.** The serial control
  channel is plaintext (the WiFi passphrase travels in the clear and is
  persisted to the board's flash unless you pass `--no-save`), and the
  peer↔server link is plain TCP, exactly as upstream NFCGate. Use it on a
  network you control.
- **Path B, variant B1** (BomberCat as `reader` against the NFCGate Android app
  emulating the card) requires a **rooted** phone with Xposed and NFCGate's
  native hook — it is not possible on a stock phone. Variant B2 (BomberCat as
  `card`, phone as reader) works on a stock device. See
  [HARDWARE_TESTING.md](../HARDWARE_TESTING.md).
