# Troubleshooting

Common failure modes when driving a BomberCat from the CLI, and how to fix them.
Every handled error is a clean one-line message (leading `✗`) with exit code 1 —
you should never see a Python traceback. If you do, that's a bug worth reporting.

- [Serial permission denied](#serial-permission-denied)
- [No BomberCat found / board not detected](#board-not-detected)
- [Board present by USB id but no handshake](#board-present-but-no-handshake)
- [Wrong board answers to `-d`](#wrong-board)
- [Old firmware without `identify` / `capture`](#old-firmware)
- [`run` times out](#run-times-out)
- [`peer present` stays `no`](#peer-stays-no)
- [Capture: Wireshark doesn't open / no frames](#capture-issues)
- [`testserver` errors](#testserver-issues)

---

<a id="serial-permission-denied"></a>
## Serial permission denied

Symptom: a `PermissionError` / `SerialException` opening `/dev/ttyACM*`.

On Linux, serial access needs your user in the `dialout` group:

```sh
sudo usermod -aG dialout $USER
# then log out and back in (group membership is applied at login)
```

Verify with `groups | grep dialout`. A quick one-off without re-login:
`sudo chmod a+rw /dev/ttyACM0` (resets on replug).

<a id="board-not-detected"></a>
## No BomberCat found / board not detected

Symptom:

```
✗ no BomberCat found; pass --port (e.g. --port /dev/ttyACM0)
```

Check, in order:

1. **Is it enumerated at all?** `bombercat device list -a` shows every serial
   port (including non-candidates). If your board isn't there, it's a cable /
   power / driver problem, not a CLI one. On Linux, `ls /dev/ttyACM*`.
2. **Is it a candidate?** In `bombercat device list`, a BomberCat should show a
   `✓` (answered) or `USB id` (recognized but silent). Neither → its USB
   VID/PID isn't recognized. If you re-flashed it with a custom USB identity,
   declare it:
   ```sh
   BOMBERCAT_VID=0x1209 BOMBERCAT_PID=0x005E bombercat device list
   ```
3. **Bypass discovery** by naming the port directly: `bombercat device info -p /dev/ttyACM0`.

<a id="board-present-but-no-handshake"></a>
## Board present by USB id but no handshake

Symptom: `device list` shows `USB id` (yellow) instead of `✓`, or:

```
✗ a BomberCat is connected at /dev/ttyACM0 (USB …) but it did not answer the
  handshake — is it running the NFCGate relay firmware?
```

The board is there, but its firmware isn't serving the control REPL. Usually one
of:

- **It isn't running the NFCGate relay firmware.** Flash it — see
  [`firmware/NFCGate/README.md`](../../firmware/NFCGate/README.md). After
  flashing, `bombercat device info` should show a `fw` version and `state idle`.
- **`RELAY_AUTOSTART = 1` with a non-empty SSID.** The sketch then blocks on the
  WiFi/TCP bring-up in `setup()` before the REPL starts, so the CLI can't reach
  it. Set `RELAY_AUTOSTART = 0` (the default, required for CLI-driven use) and
  reflash.
- **Wrong sketch / a wedged firmware** that stops draining its USB-OUT endpoint —
  the CLI bounds writes with a timeout and reports:
  ```
  ✗ device did not accept 'ping' (write timed out); it may be wedged or not
    running the relay firmware
  ```
  Power-cycle the board and reflash the relay firmware.

<a id="wrong-board"></a>
## Wrong board answers to `-d`

If a command hits the wrong physical board, confirm the mapping — blink each ID:

```sh
bombercat identify -d 1
bombercat identify -d 2
```

IDs are derived from a stable USB identity and survive replugs/reboots **as long
as the same set of boards is attached**. Adding or removing a board can renumber
the rest. Also: if **no** port carries a BomberCat USB id, every candidate port
gets numbered and `device list` warns you — the IDs are then just "whatever
serial ports exist", so verify before trusting `-d`.

<a id="old-firmware"></a>
## Old firmware without `identify` / `capture`

The CLI can be newer than the board's firmware. You'll see:

- `identify` → `✗ identify failed: unknown command` and a hint that the firmware
  predates `identify` (needs ≥ 0.7.0). `config` still works — the LED-blink after
  a successful config is skipped with a warning.
- `capture start` → `✗ could not arm capture: unknown command` and a hint to
  reflash `firmware/NFCGate` (needs ≥ 0.8.0).

Fix: reflash the current NFCGate firmware
([`firmware/NFCGate/README.md`](../../firmware/NFCGate/README.md)). Check the
version with `bombercat device info` (the `fw` field).

<a id="run-times-out"></a>
## `run` times out

`run` waits up to **45 s** for the relay to reach `relaying`. If it doesn't:

```
✗ relay did not reach 'relaying' in time.
ℹ still 'connecting nfcgate-server' after 45s — the bring-up is slow or stuck
  (the device is still responsive).
```

The device is **not** wedged — the REPL stayed live, so keep diagnosing:

- **WiFi**: wrong SSID/password? `bombercat config show` and re-`config wifi`.
- **Server reachable?** From the same network: `nc -vz <host> <port>`. Is the
  `nfcgate-server` actually listening? For a bench server, `bombercat testserver run`.
- **PN7150 / NFC bring-up**: `bombercat monitor` shows where it's stuck.
- If `run` was **rejected** outright (`✗ relay rejected 'run': …`), the config is
  incomplete (e.g. empty SSID) or it's already running — check `config show` /
  `status`.

<a id="peer-stays-no"></a>
## `peer present` stays `no`

Both peers must share the **same `--server` and the same `--session`**, with
**opposite roles** (`reader` / `card`). A mismatched session byte means each peer
joins a different session and they never see each other. `session == 0` is treated
as a disconnect — use `1..255`. Confirm both with `bombercat config show` on each
board (or check the app's session/role for Path B).

<a id="capture-issues"></a>
## Capture: Wireshark doesn't open / no frames

- **"Wireshark not found"** — install Wireshark, or capture to a file only with
  `-o file.pcap` (no Wireshark needed). The CLI probes the usual install
  locations and `PATH`.
- **"nothing to do"** — you passed neither `-ws` nor `-o`. Pass at least one.
- **Wireshark opens but no packets** — no APDUs are flowing. Capture only shows
  frames while the relay is actually relaying: `bombercat status` should be
  `relaying` with a peer present, and a real transaction must be happening (card
  on the reader, terminal on the card board).
- **"Wireshark did not attach to the pipe in time"** — it took longer than 30 s
  to open the FIFO; with `-o` also given, capture falls back to the file.
- A **classic pcap** written by the CLI vs a **pcapng** saved from Wireshark is
  expected — see [Capture / Wireshark](capture.md#classic-pcap-vs-pcapng).

<a id="testserver-issues"></a>
## `testserver` errors

- **`testserver run`**: needs Docker on `PATH` **and** the server fetched once
  (`tools/testserver/fetch_server.sh`) — the clone at `<repo>/server` is the
  Docker *build context*, not just a dependency of `smoke`. Full list in the
  [reference](reference.md#requirements).
  The CLI pre-checks all of it *before* building and prints a framed panel with
  the numbered commands that fix it, so you should never see a raw `docker
  build` error. What each panel means:
  - **The nfcgate-server sources are missing** — the server was never fetched.
    On a terminal the CLI offers to run `tools/testserver/fetch_server.sh` for
    you; answer `n` to do it yourself. The clone at `<repo>/server` is the
    Docker *build context*, not just a dependency of `smoke`.
  - **Docker is not installed, or not on your PATH** — install Docker Engine,
    or run the server without Docker (see
    [`testserver/README.md`](../testserver/README.md)).
  - **Docker refused the connection: permission denied on its socket** — the
    panel tells you which of the three cases you are in, because the fix
    differs: not in the `docker` group (`sudo usermod -aG docker "$USER"`), a
    member but in a session that predates it (`newgrp docker` — membership is
    only applied at login), or the group is already active and the socket
    refuses anyway (unusual ownership, or rootless Docker).
    If `newgrp` is not installed (it comes from `shadow-utils`, `login` on
    Debian/Ubuntu — trimmed installs and container images often lack it), the
    panel offers `sg docker -c "$SHELL"` instead, and when that is missing too
    it says so and points at the only remaining options: log out and back in
    (or reboot), install the package, or run this once under `sudo -E` (the
    panel prints the exact command, with absolute paths, since `sudo` resets
    `PATH`).
  - **The Docker daemon is not running** — `sudo systemctl start docker`, or
    launch Docker Desktop on macOS/Windows.
  - **Host port N is already in use** — the panel distinguishes a test server
    you left running (`docker rm -f bombercat-nfcgate-server-run`) from any
    other program holding the port (`testserver run -p <port>`).
  - `bash is not installed` — the CLI launches the server through
    `tools/testserver/run.sh`, so it needs a shell to do it.

  Running `tools/testserver/run.sh` directly gets the same checks in terse
  form, without the panels.
- **`testserver smoke`**: needs the server fetched once
  (`tools/testserver/fetch_server.sh`) and the `protobuf==3.20.3` runtime. The CLI
  bootstraps a throwaway venv (`tools/.venv-smoke`, override with
  `BOMBERCAT_SMOKE_VENV`) if its own interpreter lacks protobuf. See
  [`testserver/README.md`](../testserver/README.md).
