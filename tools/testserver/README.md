# Local nfcgate-server (relay testing)

A throwaway `nfcgate-server` for developing and testing the BomberCat relay
without the Android app. It wraps the pinned server (`../../server`,
`ElectronicCats/nfcgate-server@fc9103d`) in Docker.

The server just relays length-prefixed frames between the two clients that share
a 1-byte session number — see the wire format in
[`../../firmware/core/proto/UPSTREAM.md`](../../firmware/core/proto/UPSTREAM.md).

## Fetch the server (once)

The server is a dev-only fixture, so it is **not** committed or a submodule — it
is cloned on demand into `../../server` (which is gitignored):

```bash
tools/testserver/fetch_server.sh            # clones ElectronicCats/nfcgate-server@fc9103d
SERVER_REPO=/path/to/clone tools/testserver/fetch_server.sh   # offline / mirror
```

You can skip this: `bombercat testserver run` and `bombercat testserver smoke`
detect the missing clone and offer to fetch it for you.

## The latency patch

Upstream `server.py` is correct but **slow**: it leaves Nagle on and writes each
frame's 4-byte header and payload separately, so every server→board relay eats a
~40 ms delayed-ACK stall — and it logs every relayed frame on the lock-step hot
path. Across the ~36 relays of one EMV transaction that is the difference between
**~13.5 s and ~4.2 s** (Fases E and H of
[`../../firmware/LATENCIA_OPTIMIZACION.md`](../../firmware/LATENCIA_OPTIMIZACION.md)).

The fixes now live **in the fork**, as a real commit: `fc9103d` is exactly
upstream `4d32cc1` plus them, so the clone `fetch_server.sh` makes is already
fast. The same changes stay here as a versioned patch,
[`latency-fixes.patch`](latency-fixes.patch), which
[`apply_patch.sh`](apply_patch.sh) uses to *assert* that a clone carries them —
a no-op on the normal path, but it still catches a hand-edited clone or one
pinned to pristine upstream. You never call it by hand: `fetch_server.sh` runs it
after cloning and `run.sh` re-asserts it before every build (the Dockerfile
`COPY`s `server.py`, so an unpatched clone would bake a slow relay into the
image). It is idempotent — an already-patched clone is left alone.

```bash
tools/testserver/apply_patch.sh    # only if you want to check/repair by hand
```

To measure a phase against its pristine baseline you now need the *unpatched*
upstream commit, since the pinned fork commit is patched at the source:

```bash
rm -rf server
SERVER_COMMIT=4d32cc1 BOMBERCAT_SKIP_LATENCY_PATCH=1 tools/testserver/fetch_server.sh
BOMBERCAT_SKIP_LATENCY_PATCH=1 tools/testserver/run.sh
```

> If the clone was edited by hand the patch stops applying and `run.sh` aborts
> rather than build a slow server; the error prints how to reset the clone.
> A **dedicated server/VPS** clones the same fork, so the fixes arrive with it —
> see `docs/SERVIDOR_DEDICADO_NFCGATE.md` §2.2.

## Run the server

```bash
tools/testserver/run.sh              # builds the image, listens on :5566
PORT=6000 tools/testserver/run.sh    # different host port
```

The `log` plugin is loaded, so every relayed frame is decoded and printed, e.g.:

```
[log] ('172.17.0.1', 38336) OP_PSH R: (initial) 00a404000e325041592e5359532e444446303100
[log] ('172.17.0.1', 38330) OP_PSH C: (initial) 6f23840e325041592e5359532e4444463031a5119000
```
`R` = data from a reader, `C` = data from a card.

### Without Docker

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r tools/testserver/requirements.txt   # protobuf==3.20.3
cd server && python server.py log                   # listens on 0.0.0.0:5566
```

> The committed `server/plugins/*_pb2.py` need the classic protobuf runtime
> (`protobuf<=3.20.x`); protobuf 4+ raises *"Descriptors cannot be created
> directly"*. That's why the requirement is pinned.

## Smoke test (no hardware)

With the server running, in another terminal:

```bash
bombercat testserver smoke [host] [port]           # recommended

# or directly — needs protobuf==3.20.3 available (reuse the venv above)
python tools/testserver/relay_smoketest.py [host] [port]
```

The `bombercat` wrapper bootstraps `tools/.venv-smoke/` with the pinned protobuf
runtime the first time it runs, so the interpreter running the CLI (usually the
system Python, which PEP 668 makes read-only) needs nothing installed. Override
its location with `BOMBERCAT_SMOKE_VENV=/path/to/venv`.

It opens two TCP clients on the same session, has a "reader" push a SELECT PPSE
APDU and a "card" push the response, and asserts each peer receives the identical
`ServerData` blob. The session byte is random per run (never 42): the server
relays to *every* client in a session, so a live BomberCat using the firmware
default `RELAY_SESSION 42` would otherwise inject its `OP_SYN` into the test and
fail the byte-identity assertion. Expected output:

```
[OK] reader->card  opcode=OP_PSH source=READER apdu=00a4...
[OK] card->reader  opcode=OP_PSH source=CARD apdu=6f23...

RELAY SMOKE TEST PASSED
```

This doubles as the reference for how the firmware `NfcGateLink` must frame and
serialize messages (Phase 3).

## Next endpoints

- **Real compatibility:** point the NFCGate Android app's connection settings at
  this server's host:port and use the same session; the BomberCat is the peer.
- **End-to-end:** two BomberCats (reader + card) sharing one session number.
