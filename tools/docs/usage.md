# End-to-end usage

The real workflow: bring up an NFCGate relay on hardware and drive it from the
CLI. Two topologies are supported and validated on hardware (docs/NFCGATE_PLAN.md §15):

- **Path A** — two BomberCats (reader + card) joined by an `nfcgate-server`.
- **Path B** — one BomberCat against the **NFCGate Android app** as the other
  peer (both variants: BomberCat as reader or as card).

**Control plane only.** Everything the CLI does below travels over USB-serial as
text control commands. The actual APDUs never touch serial — they go over
WiFi/TCP between the two peers through the `nfcgate-server`:

```mermaid
flowchart LR
    card([Card]) -->|RF| reader[BomberCat READER]
    reader -->|WiFi / TCP| server[nfcgate-server]
    server -->|WiFi / TCP| emu[BomberCat CARD]
    emu -->|RF| term([Terminal])

    cli[bombercat CLI]
    cli -. "USB-serial<br/>(config / run / status / capture)" .-> reader
    cli -. "USB-serial<br/>(config / run / status / capture)" .-> emu

    subgraph data [APDU data plane]
        reader
        server
        emu
    end
```

## 0. Prerequisites

- Both boards flashed with the **NFCGate relay firmware** and answering the
  handshake (`bombercat device info` shows a `fw` version and `state idle`). For
  wiring, board profile and flashing, see
  [`firmware/NFCGate/README.md`](../../firmware/NFCGate/README.md).
  - The PN7150 pins are the BomberCat defaults; no extra wiring is needed.
  - The firmware must boot into the control REPL, i.e. `RELAY_AUTOSTART = 0`
    (the default). With `RELAY_AUTOSTART = 1` and a non-empty SSID the board
    blocks on WiFi bring-up before the REPL starts and the CLI can't reach it.
- A reachable `nfcgate-server`. For a bench setup, run the local one:
  ```sh
  bombercat testserver run           # Docker, host :5566
  ```
  Both peers point at this host:port. (Real deployments use any reachable
  `nfcgate-server` — the app's default public server, or your own.)
- The two boards usually share one host over USB. List and address them by ID:
  ```sh
  bombercat device list
  ```

---

## Path A — two BomberCats via `nfcgate-server`

One board reads a physical card (`reader`), the other emulates it to a terminal
(`card`). Both share the same `--server` and `--session`.

### 1. Configure both ends from one terminal

```sh
# WiFi (same network for both)
bombercat config wifi -d 1 --ssid MyNet --pass 's3cret'
bombercat config wifi -d 2 --ssid MyNet --pass 's3cret'

# nfcgate: same server + session, opposite roles
bombercat config nfcgate -d 1 --server 192.168.1.5:5566 --session 42 --role reader
bombercat config nfcgate -d 2 --server 192.168.1.5:5566 --session 42 --role card
```

Each `config` blinks the LED of the board it configured, so you can confirm which
physical board is `-d 1` vs `-d 2`. Not sure which is which beforehand?
`bombercat identify -d 1` blinks that board's LED.

Confirm:

```sh
bombercat config show -d 1
bombercat config show -d 2
```

### 2. Start the relay on both

`run` blocks until each board reaches `relaying` (or reports an error / times
out):

```sh
bombercat run -d 1        # reader
bombercat run -d 2        # card
```

A relay is live once **both** peers are up: `status` shows `state relaying`,
`link connected yes`, and `peer present yes`.

```sh
bombercat status -d 1
bombercat status -d 2
```

### 3. Watch it

Present the physical card to the reader board and a terminal to the card board.
Watch either side live:

```sh
bombercat monitor -d 1     # reader side
```

You'll see the relay logs and the per-APDU hex dumps as an EMV transaction flows
(e.g. `2PAY.SYS.DDF01` on the first `SELECT`).

### 4. Capture the APDUs

See [Capture / Wireshark](capture.md) for the full story. Quick version — capture
each side in its own terminal:

```sh
bombercat capture start -d 1 -ws           # reader side (pre-mutation APDU)
bombercat capture start -d 2 -ws -o emv.pcap  # card side (post-mutation) + file
```

Ctrl-C stops and disarms the tap.

### 5. Stop

```sh
bombercat stop -d 1
bombercat stop -d 2
```

---

## Path B — against the NFCGate Android app

Here the phone running the NFCGate app is one peer and a single BomberCat is the
other. Both variants work:

- **B1** — BomberCat `reader` (reads a physical card) + phone as `card`/HCE.
- **B2** — BomberCat `card` (emulates to a terminal) + phone as `reader`.

Setup is the same as Path A, but you only configure and run the **one**
BomberCat, and the phone provides the matching opposite role on the same server
and session:

```sh
# B1 example: BomberCat is the reader
bombercat config wifi    --ssid MyNet --pass 's3cret'
bombercat config nfcgate --server <server-host>:5566 --session 42 --role reader
bombercat run
bombercat status          # peer present yes  once the phone joins the session
bombercat monitor
```

In the NFCGate app, point it at the same `nfcgate-server`, set the same session,
and pick the opposite role (card/HCE for B1, reader for B2). The BomberCat's
`status` flips `peer present` to `yes` when the app joins the session — there is
no explicit join handshake on the wire; associating with a session is implicit in
sending the first frame (see [protocol](protocol.md) and
[`firmware/core/proto/UPSTREAM.md`](../../firmware/core/proto/UPSTREAM.md)).

---

## Cheat sheet

| Step | Single board | Two boards |
|---|---|---|
| Discover | `bombercat device list` | `bombercat device list` |
| WiFi | `bombercat config wifi --ssid … --pass …` | add `-d 1`, `-d 2` |
| nfcgate | `bombercat config nfcgate --server … --session … --role …` | opposite roles, same session |
| Start | `bombercat run` | `bombercat run -d 1 && bombercat run -d 2` |
| Watch | `bombercat status` / `bombercat monitor` | add `-d <ID>` |
| Capture | `bombercat capture start -ws` | capture each side |
| Stop | `bombercat stop` | `bombercat stop -d 1 && bombercat stop -d 2` |

For a bench with no RF at all, use `bombercat testserver smoke` to exercise the
relay path against the local server (see [reference](reference.md#testserver-smoke)).
