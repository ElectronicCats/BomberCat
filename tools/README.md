# `bombercat` — control CLI & dev tooling

A `click`/`rich` command-line tool for the NFCGate relay firmware. It talks to a
BomberCat over **USB-serial** to configure it, start/stop the relay and watch it
live. No APDUs travel over serial — those go over WiFi/TCP to the
`nfcgate-server`; this is the **control plane** only (NFCGATE_PLAN.md Fase 6).

## Install

```sh
cd tools
python3 -m pip install -r requirements.txt   # click, rich, pyserial
python3 bombercat.py --help
```

(You may want a virtualenv. On Linux, serial access usually needs your user in
the `dialout` group: `sudo usermod -aG dialout $USER`, then re-login.)

## Device control

The firmware boots into a small line protocol; the CLI speaks it for you. The
port is auto-detected by handshake when a single BomberCat is attached; with
several attached, pick one with `--device/-d <ID>` (see [Multiple
devices](#multiple-devices)) or name the port with `--port/-p`
(e.g. `--port /dev/ttyACM0`).

```sh
# Discover / inspect
bombercat device list                 # IDs + serial ports, ✓ = answered the handshake
bombercat device info                 # firmware version + current config
bombercat identify -d 1               # blink that board's LED to see which it is

# Configure (persisted to flash unless --no-save)
bombercat config wifi   --ssid MyNet --pass 's3cret'
bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
bombercat config show

# Run & watch
bombercat run                         # associate WiFi, connect server, start relay
bombercat status                      # state / link / peer / relayed count
bombercat monitor                     # live serial stream (relay logs + APDU hex)
bombercat stop
```

Both relay peers must share the same `--server` and `--session`; one runs
`--role reader` (reads a physical card), the other `--role card` (emulates one to
a terminal). See `firmware/NFCGate/README.md` for wiring and flashing.

### Multiple devices

A relay needs two peers, so both BomberCats are usually plugged into the same
host. The OS numbers `/dev/ttyACM*` (`COM*` on Windows) in whatever order it
enumerates them, so the CLI numbers devices itself, from their **USB identity**
(serial number first, then the USB port location) — the ID stays put across
replugs and reboots as long as the same boards are attached:

```sh
bombercat device list
#  ID  Port          BomberCat  Description  Serial#            HWID
#  #1  /dev/ttyACM1  ✓          BomberCat    36A864E62A367EA3   USB VID:PID=…
#  #2  /dev/ttyACM0  ✓          BomberCat    E6614C775B4F2A21   USB VID:PID=…
```

Every command that talks to a board takes `-d/--device <ID>`:

```sh
# Configure both ends of one relay session from a single terminal
bombercat config wifi    -d 1 --ssid MyNet --pass 's3cret'
bombercat config nfcgate -d 1 --server 192.168.1.5:5566 --session 42 --role reader
bombercat config wifi    -d 2 --ssid MyNet --pass 's3cret'
bombercat config nfcgate -d 2 --server 192.168.1.5:5566 --session 42 --role card

bombercat run -d 1 && bombercat run -d 2
bombercat status -d 2
bombercat monitor -d 1
```

Not sure which board is which? `bombercat identify -d 1` blinks that
board's LED for a couple of seconds (firmware ≥ 0.7.0).

`-d` and `-p` are mutually exclusive: `-d` resolves the port for you, `-p`
bypasses numbering entirely. Numbering never opens a port (opening one can reset
the MCU), so it stays cheap and side-effect free; the `✓` column is what costs a
handshake. If no attached port carries a BomberCat USB VID/PID, every candidate
port gets numbered instead and `device list` says so — check the table before
trusting an ID. A board with a custom USB identity can be declared with the
`BOMBERCAT_VID` / `BOMBERCAT_PID` environment variables.

### Control protocol (for reference)

One command per line; replies use a leading marker so the CLI can ignore
interleaved device logs: `:key value` (data), `+OK [msg]` (success), `-ERR msg`
(failure). Commands: `ping info get set save load clear run stop status identify
reboot`.
Mirrored on the device by `firmware/core/src/SerialControl.{h,cpp}` and on the
host by `modules/core/bombercat.py` (`DeviceLink`).

## Dev tooling

These wrap the reproducible build/test scripts (see NFCGATE_PLAN.md Fases 1–5):

```sh
bombercat proto gen                   # regenerate firmware/core/src/proto/*.pb.{c,h}
bombercat testserver run [-p 5566]    # local nfcgate-server in Docker
bombercat testserver smoke [host port]# relay smoke test (no RF)
```

## Tests (dev-only, no hardware)

```sh
python3 tools/tests/serialctl_hosttest.py         # DeviceLink protocol parser (pty)
tools/testserver/codec_hosttest/build_and_run.sh  # firmware codec vs live server
```
