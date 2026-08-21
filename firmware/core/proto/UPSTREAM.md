# Vendored NFCGate protocol — pinned upstream & wire format

These `.proto` files are vendored (copied) from the NFCGate `protocol` repository
so the firmware build is self-contained and reproducible. Do **not** edit them to
change the protocol; edit only to re-sync with a newer pinned upstream.

## Pinned versions

| Repo | URL | Commit | Tag / note |
|---|---|---|---|
| `protocol` | https://github.com/nfcgate/protocol | `804fa9a` | source of `c2c.proto`, `c2s.proto` |
| `nfcgate` (app) | https://github.com/nfcgate/nfcgate | `35f73ee` | `v2.6.1-2-g35f73ee` |
| `server` | https://github.com/ElectronicCats/nfcgate-server | `fc9103d` | local test server (`../../server`); our fork = `nfcgate/server@4d32cc1` + `tools/testserver/latency-fixes.patch` |

Both `nfcgate` and `server` reference `protocol` as the submodule
`protobuf/src/main/proto/protocol` / `protocol` respectively.

## ⚠️ This is the *current* (simple) protocol — not the one in the old docs

The `protocol/README.md` and the server's `test.py` describe an **older** scheme
(`Wrapper`/`metaMessage.proto`, `Session` create/join handshake, `Data`,
`Anticol`, `Status`). That version is **obsolete** and does not match the pinned
`server.py`. The BomberCat plan (`docs/NFCGATE_PLAN.md` §4) was written against those
stale docs; the reality we target is what is documented here.

At the pinned commit there are only **two** messages and **no** Wrapper/Session/
Anticol/Status:

- `c2c.proto` → `NFCData` — the actual NFC payload (APDU + metadata).
- `c2s.proto` → `ServerData` — the envelope the server routes between peers.

## Wire format (validated against the pinned server, no RF)

The framing is **asymmetric**:

```
client -> server :  [ 4B length big-endian ][ 1B session ][ payload ]
server -> client :  [ 4B length big-endian ][ payload ]
```

- `length` counts only `payload` bytes (the session byte is NOT included).
- `session` is a single byte (1..255) the client chooses. The server groups
  clients by this byte and forwards each frame to the *other* client(s) sharing
  it. There is **no** create/join handshake — associating with a session is
  implicit in sending a frame with that session byte. `session == 0` with no
  prior session is treated as a disconnect (see `server.py:71`).
- `payload` is a serialized `ServerData` whose `.data` field is, in turn, a
  serialized `NFCData`:

```
payload = ServerData {
    opcode = OP_PSH            # OP_PSH/OP_SYN/OP_ACK/OP_FIN
    data   = NFCData {         # (serialized bytes)
        data_source = READER | CARD
        data_type   = INITIAL | CONTINUATION
        timestamp   = <unix millis>
        data        = <raw APDU bytes>
    }.SerializeToString()
}.SerializeToString()
```

The server treats `ServerData.data` as opaque and simply relays the whole
`ServerData` blob unchanged to the peer; the `log` plugin decodes it for display.

## Message ↔ BomberCat role mapping

| Direction | `NFCData.data_source` | BomberCat role that *produces* it | Role that *consumes* it |
|---|---|---|---|
| Terminal → card, over RF read by reader side | `READER` | `host_Relay_NFC` (reader) | `client_Relay_NFC` (emulation/HCE) |
| Card → terminal (the response) | `CARD` | `client_Relay_NFC` (emulation/HCE) | `host_Relay_NFC` (reader) |

`OP_SYN`/`OP_ACK`/`OP_FIN` are available for session lifecycle signalling; the
minimal relay only needs `OP_PSH`.

## nanopb `.options`

`c2c.options` / `c2s.options` bound the variable `bytes` fields to fixed-size
buffers so nanopb uses static allocation (bounded RAM):

| Field | max_size | Rationale |
|---|---|---|
| `NFCData.data` | 512 | raw APDU; EMV short APDU ≤ 256, headroom for extended |
| `ServerData.data` | 600 | must hold a serialized `NFCData` (~530 B) |

Generated `NFCData_size = 530`, `ServerData_size = 605` (see the `*.pb.h`).

## Regenerating

The generated `src/proto/*.pb.c` / `*.pb.h` are committed. Regenerate only when
these `.proto`/`.options` change:

```
tools/gen_proto.sh
```

It bootstraps a pinned nanopb toolchain in a throwaway venv; a normal firmware
build needs no Python.
