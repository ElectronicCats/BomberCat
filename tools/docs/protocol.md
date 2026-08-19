# Control protocol (for developers)

The CLI talks to a BomberCat over a small **line-based ASCII protocol** on the
USB-serial link. This page documents that protocol, the `DeviceLink` client that
speaks it, and how ports are discovered and numbered.

**Control plane only.** This link carries *control* commands and *events* — never
the relayed APDUs. Those travel over WiFi/TCP between peers through the
`nfcgate-server`. The one exception in the *text* sense is `capture`: while armed,
the firmware echoes a **copy** of each relayed APDU here as a `:apdu` event so the
host can build a pcap — the relay hot path is untouched.

- Firmware side: [`firmware/core/src/SerialControl.h` / `.cpp`](../../firmware/core/src/SerialControl.h)
- Host side: `tools/modules/core/bombercat.py` (`DeviceLink`),
  `tools/modules/core/usb_connection.py` (transport & discovery)

---

## Wire format

One command per line, `\n`-terminated:

```
CLI  -> device :  <cmd> [args...]\n
device -> CLI  :  one or more reply lines, each with a leading marker
```

Reply markers let the CLI tell replies apart from human log output:

| Marker | Meaning |
|---|---|
| `:<key> <value>` | A datum (an `info`/`status` field, or a `:apdu` capture event). |
| `+OK [text]` | Command succeeded. **Terminates** a reply. |
| `-ERR <text>` | Command failed. **Terminates** a reply. |

Any line **not** starting with `:`, `+` or `-` is device log noise and is
ignored. Every command yields exactly one terminating `+OK`/`-ERR` line. The
link runs at **115200 baud**.

Example — `info`:

```
info
:fw 0.9.7
:role reader
:ssid MyNet
:server 192.168.1.5
:port 5566
:session 42
:state idle
+OK
```

## Commands

| Command | Reply | Notes |
|---|---|---|
| `ping` | `+OK bombercat` | Handshake / discovery. |
| `info` | `:fw :role :ssid :server :port :session :state` then `+OK` | Full snapshot. |
| `get <key>` | `:<key> <value>` `+OK` | Keys as in `set`. |
| `set <key> <value…>` | `+OK` / `-ERR` | Keys: `ssid pass server port session role`. `value` is the rest of the line; `role` is `reader\|card`, `port`/`session` numeric. |
| `save` | `+OK` / `-ERR` | Persist current config to flash. |
| `load` | `+OK` | Reload config from flash. |
| `clear` | `+OK` / `-ERR` | Erase persisted config. |
| `run` | `+OK accepted` / `-ERR <reason>` | **Non-blocking**: only kicks off the bring-up. Poll `status` for progress. |
| `stop` | `+OK` | Stop the relay. |
| `status` | `:state :detail :connected :peer :relayed` then `+OK` | See [below](#status). |
| `identify` | `+OK` | Blink the LED a couple of seconds (firmware ≥ 0.7.0). Returns at once; blinking runs from the sketch loop. |
| `capture <on\|off>` | `+OK capture on\|off` | Arm/disarm the APDU tap (firmware ≥ 0.8.0). Bare `capture` / `capture status` → `:capture <0\|1>` `+OK`. |
| `loglevel <n>` | `+OK` | Set firmware log verbosity (2=Warn, 3=Info, 4=Debug). `loglevel` / `loglevel status` reports the current level. Used by `monitor`. |
| `reboot` | `+OK`, then resets the MCU | |

The relay actions (`run`/`stop`/`reboot`) are provided to `SerialControl` by the
sketch as callbacks, so `core/` stays free of any WiFiNINA dependency.

<a id="status"></a>
### `status` fields

```
:state connecting
:detail associating WiFi
:connected 0
:peer 0
:relayed 0
+OK
```

- `state` — `idle` → `connecting` → `relaying` → `error`.
- `detail` — current bring-up phase or last error, as human text (omitted when
  empty).
- `connected` — `1` if the TCP link to the `nfcgate-server` is up.
- `peer` — `1` if the other peer has joined the session.
- `relayed` — count of APDU pairs relayed so far.

The `run` command's async state machine (`idle → connecting → relaying|error`) is
what the CLI polls after `run` is accepted — see
[`nfcgate/cli.py`](../modules/nfcgate/cli.py) `run_cmd`.

### `:apdu` capture events

While `capture on`, each relayed APDU is echoed as:

```
:apdu <dir> <ts_ms> <hex>
```

- `dir` — `cmd` (terminal→card / command) or `resp` (card→terminal / response).
- `ts_ms` — the device's `millis()` timestamp (ground-truth timing).
- `hex` — the raw APDU bytes.

The host turns these into pcap frames — see [Capture / Wireshark](capture.md).

---

## The `DeviceLink` client

`DeviceLink` (in `modules/core/bombercat.py`) is the host-side implementation of
the protocol above. It mirrors `SerialControl` on the firmware.

```python
from modules.core.bombercat import DeviceLink, resolve_port

target = resolve_port(port=None, device_id=1)   # or a raw "/dev/ttyACM0"
with DeviceLink(target) as link:
    if not link.ping():
        raise SystemExit("no handshake")
    print(link.info().data)          # {'fw': '0.9.7', 'role': 'reader', ...}
    link.set("session", "42")
    link.save()
    link.run()
    for line in link.stream():       # raw serial lines (monitor / capture)
        ...
```

Key points:

- **`command(line, read_timeout=None)`** sends one command and parses the reply
  into a `Response(ok, message, data)`. It flushes stale input first (strict
  request/response), bounds the write with a `write_timeout` (a wedged firmware
  surfaces as a clean `DeviceError`, not a hang), and reads reply lines until a
  `+OK`/`-ERR` terminator or the deadline. A timeout raises `DeviceError`.
- **`Response`** — `.ok` (bool), `.message` (the `+OK`/`-ERR` text), `.data`
  (the `:key value` lines by key). It is truthy when `ok`.
- Convenience wrappers: `ping()`, `info()`, `status()`, `set(k, v)`, `save()`,
  `run()`, `stop()`, `identify()`.
- **`stream()`** yields decoded serial lines forever, read-only — it sends
  nothing, so it doesn't disturb a running relay. `monitor` and `capture` consume
  it.
- On `open()` it sleeps briefly and flushes the input buffer so the first command
  reads its own reply, not a boot banner.
- `DeviceError` is raised on timeout, a closed port, or a serial error.

---

<a id="port-discovery"></a>
## Port discovery & numbering

Implemented in `modules/core/usb_connection.py` and the discovery helpers in
`bombercat.py`. Two ideas:

### USB identity → candidate ports

A BomberCat enumerates over USB with **VID `0x1209` / PID `0x005E`** (pid.codes)
in its application personality. `usb_connection` matches a small set of known
(VID, PID) pairs — including the stock Arduino Mbed RP2040 identity
(`2341:005E`) some builds report — to *tag* a port as a candidate **without
opening it** (opening a port can reset the MCU). A custom identity can be added
with the `BOMBERCAT_VID` / `BOMBERCAT_PID` environment variables (both required).

Matching on USB id only **tags** a port; the `✓` in `device list` and
auto-detection still require the control handshake.

### Stable numbering (`-d`)

With several boards attached the OS assigns `/dev/ttyACM*` (`COM*`) in a
non-deterministic order, so the CLI numbers devices itself from a **stable USB
identity**:

1. USB serial number (survives re-enumeration and a changed `ttyACM` number),
2. else the USB topology location prefix (stable while the board stays in the
   same physical socket),
3. else the port path as a last resort.

So `-d 1` refers to the same physical board across replugs and reboots, as long
as the same set of boards is attached. If **no** port carries a BomberCat USB id
(e.g. a board re-flashed to a generic identity), every candidate port is numbered
instead — `device list` warns you when that happens.

### `resolve_port(preferred, device_id)`

The single entry point every command uses to pick a target:

- `preferred` (`--port`) wins and is returned as-is.
- `device_id` (`--device`) selects a numbered device (no handshake of the
  others).
- neither → auto-detect by handshake: exactly one BomberCat answers → that port;
  zero or many → a `DeviceError` telling you to pass `-d`/`--port`.
- passing both `--port` and `--device` is an error (they are mutually exclusive).

When nothing answers but a BomberCat is still present by USB id, the error points
you at that — the board is there, its firmware just isn't serving the control
REPL.
