# Command reference

Complete reference for every `bombercat` command and subcommand: purpose, flags,
examples and expected output.

**Control plane only.** Every command that talks to a board does so over the
USB-serial control protocol. No APDUs travel over serial; relayed APDUs go over
WiFi/TCP to the `nfcgate-server`. `capture` streams a *copy* of them over the
control link — see [Capture / Wireshark](capture.md).

- [Command reference](#command-reference)
  - [Invocation](#invocation)
  - [Global options](#global-options)
  - [Device selection: `-d` / `-p`](#device-selection--d---p)
  - [`device`](#device)
    - [`device list`](#device-list)
    - [`device info`](#device-info)
  - [`identify`](#identify)
  - [`config`](#config)
    - [`config wifi`](#config-wifi)
    - [`config nfcgate`](#config-nfcgate)
    - [`config show`](#config-show)
  - [`run`](#run)
  - [`stop`](#stop)
  - [`status`](#status)
  - [`monitor`](#monitor)
  - [`capture`](#capture)
    - [`capture start`](#capture-start)
    - [`capture stop`](#capture-stop)
  - [Dev tooling](#dev-tooling)
    - [`proto`](#proto)
      - [`proto gen`](#proto-gen)
    - [`testserver`](#testserver)
      - [`testserver run`](#testserver-run)
        - [Requirements](#requirements)
      - [`testserver smoke`](#testserver-smoke)
  - [`completion`](#completion)
    - [`completion install`](#completion-install)
  - [Environment variables](#environment-variables)
  - [Exit codes](#exit-codes)

---

## Invocation

Run from the `tools/` directory:

```sh
python3 bombercat.py <command> [options]
```

The docs write it as `bombercat` for brevity. To get that short form, either add
a shell alias:

```sh
alias bombercat='python3 /abs/path/to/tools/bombercat.py'
```

or install [shell completion](#completion), which also lets you run
`python bombercat.py <TAB>`.

Every command and group accepts `-h` / `--help`.

## Global options

Placed before the command (`bombercat -v device list`):

| Option | Description |
|---|---|
| `-v`, `--verbose` | Raise the log level to INFO (shows the `rich` logger's info lines; off by default, which is WARNING). |
| `-h`, `--help` | Show help and exit. |

Every run prints the ASCII header panel with the CLI version and a random tagline
before the command output. (The header is suppressed while generating shell
completion.)

<a id="device-selection"></a>
## Device selection: `-d` / `-p`

Every command that talks to a board takes the same two mutually-exclusive
selectors:

| Option | Description |
|---|---|
| `-p`, `--port PATH` | Raw serial port (`/dev/ttyACM0`, `COM3`). Used as-is; no enumeration. |
| `-d`, `--device ID` | Stable device ID from `bombercat device list` (for multiple boards). |

Resolution rules (implemented in `resolve_port`, see [protocol](protocol.md#port-discovery--numbering)):

- `--port` wins and is used verbatim.
- `--device` selects one of the numbered devices without handshaking the others.
- With **neither** given and exactly **one** BomberCat attached, it is
  auto-detected by handshake.
- Zero or several attached and no selector → a clean error telling you to pass
  `-d`/`--port`.

`-d` and `-p` are mutually exclusive; passing both is an error.

---

## `device`

> Discover and inspect BomberCat devices over USB-serial.

### `device list`

List serial ports, the device ID of each BomberCat and who answered the
handshake.

| Option | Description |
|---|---|
| `-a`, `--all` | Include non-candidate ports (built-in UARTs, Bluetooth, `ttyS*`). |

```sh
bombercat device list
```

Expected output (two boards attached):

```
                             Serial ports
┏━━━━┳━━━━━━━━━━━━━━┳━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━┓
┃ ID ┃ Port         ┃ BomberCat ┃ Serial#          ┃ HWID                ┃
┡━━━━╇━━━━━━━━━━━━━━╇━━━━━━━━━━━╇━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━━━━━━━━━━━━┩
│ #1 │ /dev/ttyACM1 │ ✓         │ 36A864E62A367EA3 │ USB VID:PID=…       │
│ #2 │ /dev/ttyACM0 │ ✓         │ E6614C775B4F2A21 │ USB VID:PID=…       │
└────┴──────────────┴───────────┴──────────────────┴─────────────────────┘
Target one with:  bombercat <command> -d <ID>   (e.g. bombercat config show -d 1)
```

The **BomberCat** column:

- `✓` — the port answered the control handshake (it is running the relay firmware).
- `USB id` — its USB VID/PID says BomberCat, but it did **not** answer the
  handshake (probably not running the NFCGate relay firmware — see
  [Troubleshooting](troubleshooting.md#board-present-by-usb-id-but-no-handshake)).
- blank — not a BomberCat candidate.

IDs are derived from a **stable USB identity** (serial number first, then USB
port location), so a board keeps its number across replugs and reboots as long as
the same set of boards is attached — not from the OS `/dev/ttyACM*` order, which
is non-deterministic. Numbering never opens a port (opening one can reset the
MCU), so `device list` is cheap; only the `✓` column costs a handshake.

If no attached port carries a BomberCat USB VID/PID, every candidate port is
numbered instead and the table says so — verify the IDs before trusting `-d`.

### `device info`

Handshake with one board and show its firmware and config.

Takes the [device selectors](#device-selection).

```sh
bombercat device info            # single board, auto-detected
bombercat device info -d 2       # a specific board
```

Expected output:

```
        BomberCat @ /dev/ttyACM0
┏━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃ Field   ┃ Value                     ┃
┡━━━━━━━━━╇━━━━━━━━━━━━━━━━━━━━━━━━━━━━┩
│ fw      │ 0.9.7                      │
│ role    │ reader                     │
│ ssid    │ MyNet                      │
│ server  │ 192.168.1.5                │
│ port    │ 5566                       │
│ session │ 42                         │
│ state   │ idle                       │
└─────────┴────────────────────────────┘
```

(`config show` prints the same table.)

---

## `identify`

> Blink a device's LED so you can tell which board an ID refers to.

Takes the [device selectors](#device-selection). Requires firmware ≥ 0.7.0; on
older firmware it reports that the board predates `identify`.

```sh
bombercat identify -d 1          # blink board #1's LED for a couple of seconds
```

```
✓ /dev/ttyACM1 is blinking its LED for a couple of seconds
```

---

## `config`

> Configure the relay (WiFi + nfcgate parameters), persisted in flash.

All three subcommands take the [device selectors](#device-selection). The two
`config` setters also **blink the LED** of the board they just configured (a
non-fatal courtesy — a board on pre-0.7.0 firmware just earns a warning), so you
can match `-d 2` to a physical board on the desk.

### `config wifi`

Set the WiFi credentials.

| Option | Description |
|---|---|
| `--ssid TEXT` | WiFi network name. **Required.** |
| `--password`, `--pass TEXT` | WiFi passphrase (empty for an open network). |
| `--save` / `--no-save` | Persist to flash (default: `--save`). `--no-save` applies for this session only, lost on reboot. |

```sh
bombercat config wifi --ssid MyNet --pass 's3cret'
```

```
✓ set ssid = MyNet
✓ set pass = ••••••
✓ saved to flash
ℹ /dev/ttyACM0 is blinking its LED — that's the board you just configured
```

### `config nfcgate`

Set the `nfcgate-server`, session and role.

| Option | Description |
|---|---|
| `--server TEXT` | `nfcgate-server` as `host` or `host:port`. **Required.** |
| `--session INTEGER` | Session byte `1..255`; **both peers must match**. **Required.** |
| `--role [reader\|card]` | `reader` = read a physical card, `card` = emulate one to a terminal. **Required.** |
| `--save` / `--no-save` | Persist to flash (default: `--save`). |

`--server` may include the port (`host:port`); an out-of-range port is rejected
with a clean error. If omitted, the device keeps its stored port (default 5566).

```sh
bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
```

```
✓ set server = 192.168.1.5
✓ set port = 5566
✓ set session = 42
✓ set role = reader
✓ saved to flash
```

### `config show`

Show the device's current configuration (same table as `device info`).

```sh
bombercat config show -d 2
```

---

## `run`

> Start the relay (associate WiFi, connect the server, begin the session).

Takes the [device selectors](#device-selection).

`run` is **non-blocking on the device**: it only *accepts* the request and starts
the bring-up in the background. The CLI then polls `status` and reports progress
until the relay reaches `relaying` (success) or `error`, or a **45 s** budget
expires. A `-ERR` on acceptance means it could not even start (e.g. empty SSID,
already running).

```sh
bombercat run
```

Successful bring-up:

```
ℹ relay accepted 'run'; bringing up…
ℹ   … associating WiFi
ℹ   … connecting nfcgate-server
✓ relay started on /dev/ttyACM0
ℹ watch it with:  bombercat monitor   /   bombercat status
```

If it does not reach `relaying` in time the device is **not** wedged (the REPL
stayed live) — the CLI points you at the likely culprit (server not listening,
PN7150 not responding) and suggests `bombercat status` / `monitor`. See
[Troubleshooting](troubleshooting.md#run-times-out).

## `stop`

> Stop the relay.

Takes the [device selectors](#device-selection).

```sh
bombercat stop
```

```
✓ relay stopped on /dev/ttyACM0
```

## `status`

> Show live relay status (state, link, peer, relayed count).

Takes the [device selectors](#device-selection).

```sh
bombercat status -d 2
```

```
              Relay status @ /dev/ttyACM0
┏━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃ Field              ┃ Value                         ┃
┡━━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┩
│ state              │ relaying                       │
│ link connected     │ yes                            │
│ peer present       │ yes                            │
│ APDU pairs relayed │ 7                              │
└────────────────────┴────────────────────────────────┘
```

`state` is one of `idle`, `connecting`, `relaying`, `error` (see the
[protocol](protocol.md#status-fields)).

## `monitor`

> Stream the device's serial output live (relay logs + APDU hex). Ctrl-C to quit.

Takes the [device selectors](#device-selection).

`monitor` is read-only — it does not disturb a running relay. On entry it raises
the firmware log level to Debug (so per-APDU hex dumps appear) and restores it to
Warn on exit. Lines are colorized: APDU hex (`cmd:`/`resp:`) in cyan, errors in
red, protocol markers dimmed.

```sh
bombercat monitor -d 1
```

```
ℹ Monitoring /dev/ttyACM1 — press Ctrl-C to stop
reader: vivo, peer presente, esperando comando del peer
R<- cmd: 0x00 0xA4 0x04 0x00 0x0E 0x32 0x50 0x41 0x59 …
reader: tarjeta activada
…
```

---

## `capture`

> Capture relayed APDUs to pcap (live Wireshark and/or a file).

Full behavior and the pcap details are in [Capture / Wireshark](capture.md); this
is the flag reference.

### `capture start`

Arm the device tap and stream APDUs to Wireshark and/or a file until Ctrl-C.
Takes the [device selectors](#device-selection). Requires firmware ≥ 0.8.0 (the
`capture` control command); the live feed also needs Wireshark installed.

| Option | Description |
|---|---|
| `-o`, `--output FILE` | Also write a `.pcap` file (classic pcap, opens in Wireshark). |
| `-ws`, `--wireshark` / `-nws`, `--no-wireshark` | Launch Wireshark on a live FIFO (opt-in; default off). |
| `--profile TEXT` | Wireshark configuration profile to launch with (`-C`). |

At least one of `-ws` / `-o` is required, otherwise there is nothing to do.

```sh
bombercat capture start -ws                # live Wireshark only
bombercat capture start -ws -o emv.pcap    # live Wireshark + file
bombercat capture start -o emv.pcap        # file only
```

```
ℹ waiting for Wireshark to attach…
✓ Wireshark attached — streaming APDUs
ℹ capturing from /dev/ttyACM0 — press Ctrl-C to stop
→ card       1234 ms  00a404000e325041592e5359532e444446303100
← card       1290 ms  6f23840e325041592e5359532e4444463031a5119000
```

`→ card` is a command (terminal→card), `← card` a response (card→terminal). If
you quit Wireshark, capture continues to the file (or stops cleanly if there is
no file). The tap is disarmed automatically on exit.

### `capture stop`

Disarm the tap on a board (e.g. one left armed by an interrupted `start`).
Takes the [device selectors](#device-selection).

```sh
bombercat capture stop -d 1
```

```
✓ capture disarmed on /dev/ttyACM1
```

---

## Dev tooling

These wrap the reproducible build/test scripts under `tools/` (docs/NFCGATE_PLAN.md
Fases 1–5). They do **not** talk to a board.

### `proto`

> Nanopb protobuf sources for the NFCGate relay.

#### `proto gen`

Regenerate `firmware/core/src/proto/*.pb.{c,h}` from the vendored `.proto` files.
Wraps `tools/gen_proto.sh`, which bootstraps a pinned venv on first run.

```sh
bombercat proto gen
```

```
ℹ Running gen_proto.sh (bootstraps a pinned venv on first run) …
✓ Protobuf sources regenerated.
```

### `testserver`

> Local nfcgate-server for relay testing (no hardware/RF).

See [`testserver/README.md`](../testserver/README.md) for the fixture itself.

#### `testserver run`

Build (if needed) and run the local `nfcgate-server` in Docker. Ctrl-C to stop.
Wraps `tools/testserver/run.sh`.

| Option | Description |
|---|---|
| `-p`, `--port INTEGER` | Host port to publish (default `5566`; the container always listens on 5566). |

```sh
bombercat testserver run          # publish on host :5566
bombercat testserver run -p 6000  # publish on host :6000
```

##### Requirements

`testserver run` shells out to `tools/testserver/run.sh`, which builds and runs
the pinned `nfcgate-server` in Docker. It is pure dev tooling: it never touches
USB, serial or RF, so no board has to be plugged in — but the host must have:

| Requirement | Why | Check |
|---|---|---|
| `bash` on `PATH` | the CLI launches the script as `bash run.sh` | `bash --version` |
| Docker installed, daemon running, usable by your user | `run.sh` does `docker build` + `docker run` | `docker run --rm hello-world` |
| Your user in the `docker` group (Linux) | otherwise the socket denies the build — add it with `sudo usermod -aG docker "$USER"` and re-login | `id -nG \| grep docker` |
| The server clone at `<repo>/server` | it *is* the Docker build context (`docker build … <repo>/server`) | `ls server/server.py` |
| Network access on the **first** run | the image pulls `python:3.11-slim` and installs `protobuf==3.20.3` | — |
| The host port free (default `5566`) | it is published as `-p <port>:5566` | `ss -ltn \| grep 5566` |

All of it is pre-checked before the build starts: the CLI verifies the server
clone, Docker, the daemon, socket permissions and the host port, and on failure
prints the fix to apply instead of a raw Docker error — see
[troubleshooting](troubleshooting.md#testserver-errors) for what each one says.
When the clone is missing and you are on a terminal, it offers to fetch it.

The server is a dev-only fixture — not committed, not a submodule — so fetch it
once (needs `git`), the same step [`testserver smoke`](#testserver-smoke) needs:

```sh
tools/testserver/fetch_server.sh                            # clones ElectronicCats/nfcgate-server@fc9103d
SERVER_REPO=/path/to/clone tools/testserver/fetch_server.sh # offline / mirror
```

Good to know:

- The container **always** listens on 5566; `-p/--port` only changes the *host*
  port (the CLI passes it to `run.sh` as [`PORT`](#environment-variables)).
- The image (`bombercat-nfcgate-server`) is rebuilt on every invocation, but
  Docker's layer cache makes that a no-op after the first build — only that first
  build needs the network.
- Ctrl-C stops and removes the container (`bombercat-nfcgate-server-run`); a
  leftover container from a crashed run is force-removed at the next start.
- Nothing here needs `protobuf` on the host: that is only for
  [`testserver smoke`](#testserver-smoke), which bootstraps its own venv.
- If the relay peers live on other machines (a phone running the NFCGate app, a
  BomberCat on the WLAN), the host firewall must allow inbound TCP on that port,
  and they must target the host's LAN address — not `127.0.0.1`.

Failure modes are listed in
[Troubleshooting](troubleshooting.md#testserver-errors).

#### `testserver smoke`

Run the relay smoke test against a running server (needs `protobuf==3.20.3`,
bootstrapped into a throwaway venv if the CLI's interpreter lacks it). Wraps
`tools/testserver/relay_smoketest.py`.

| Argument | Default | Description |
|---|---|---|
| `HOST` | `127.0.0.1` | Server host. |
| `PORT` | `5566` | Server port. |

```sh
bombercat testserver smoke                 # 127.0.0.1:5566
bombercat testserver smoke 192.168.1.5 5566
```

The server must have been fetched once with `tools/testserver/fetch_server.sh`
(the smoke test imports its committed `*_pb2.py`).

---

## `completion`

> Install shell tab completion for bombercat. (Linux/macOS only.)

### `completion install`

Install tab completion for your shell, then restart your shell (or source your rc
file).

| Option | Description |
|---|---|
| `--shell [bash\|zsh\|fish]` | Shell to install completion for (auto-detected from `$SHELL` if omitted). |

```sh
bombercat completion install            # auto-detect shell
bombercat completion install --shell zsh
```

It writes an absolute-path completion script (so completion works whether or not
`bombercat` is on `PATH`, including `python bombercat.py <TAB>`), and for zsh adds
an `fpath` entry to `~/.zshrc` if one isn't there already.

---

## Environment variables

| Variable | Used by | Meaning |
|---|---|---|
| `BOMBERCAT_VID` / `BOMBERCAT_PID` | device discovery | Declare a custom USB VID/PID for a board re-flashed with a non-stock USB identity (hex `0x1209` or decimal). Both must be set to add the pair to the match list. |
| `BOMBERCAT_SMOKE_VENV` | `testserver smoke` | Path to the throwaway protobuf venv (default `tools/.venv-smoke`). |
| `SERVER_REPO` | `testserver/fetch_server.sh` | Use an existing server clone / mirror instead of cloning. |
| `PORT` | `testserver/run.sh` | Host port for the server (set for you by `testserver run -p`). |

## Exit codes

- `0` — success.
- `1` — a handled error: no board found, handshake failed, a `set`/`run`/`capture`
  rejected by the device, a missing file, etc. These are reported as a clean
  one-line message (with a leading `✗`), **never** a Python traceback. A stray
  traceback is a bug worth reporting.
