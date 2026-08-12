# Local nfcgate-server (relay testing)

A throwaway `nfcgate-server` for developing and testing the BomberCat relay
without the Android app. It wraps the pinned upstream server (`../../server`,
`nfcgate/server@4d32cc1`) in Docker.

The server just relays length-prefixed frames between the two clients that share
a 1-byte session number — see the wire format in
[`../../firmware/core/proto/UPSTREAM.md`](../../firmware/core/proto/UPSTREAM.md).

## Fetch the server (once)

The server is a dev-only fixture, so it is **not** committed or a submodule — it
is cloned on demand into `../../server` (which is gitignored):

```bash
tools/testserver/fetch_server.sh            # clones nfcgate/server@4d32cc1
SERVER_REPO=/path/to/clone tools/testserver/fetch_server.sh   # offline / mirror
```

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
# needs protobuf==3.20.3 available (reuse the venv above)
python tools/testserver/relay_smoketest.py [host] [port]
```

It opens two TCP clients on the same session, has a "reader" push a SELECT PPSE
APDU and a "card" push the response, and asserts each peer receives the identical
`ServerData` blob. Expected output:

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
