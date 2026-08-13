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
port is auto-detected by handshake when a single BomberCat is attached; otherwise
pass `--port/-p` (e.g. `--port /dev/ttyACM0`).

```sh
# Discover / inspect
bombercat device list                 # serial ports, ✓ = answered the handshake
bombercat device info                 # firmware version + current config

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

### Control protocol (for reference)

One command per line; replies use a leading marker so the CLI can ignore
interleaved device logs: `:key value` (data), `+OK [msg]` (success), `-ERR msg`
(failure). Commands: `ping info get set save load clear run stop status reboot`.
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
