#!/usr/bin/env python3
"""Relay smoke test for the local nfcgate-server.

Validates both the running server and the BomberCat wire protocol
(nfcgate protocol @804fa9a, ElectronicCats/nfcgate-server @fc9103d) without any
RF/hardware:

    frame  client -> server : [4B big-endian length][1B session][payload]
    frame  server -> client : [4B big-endian length][payload]
    payload = ServerData{ opcode, data = NFCData{...}.SerializeToString() }

Two TCP clients join the same 1-byte session; a "reader" pushes an APDU and we
assert the "card" peer receives the identical ServerData blob, then the reverse.

Requires the classic protobuf runtime for the committed *_pb2.py:
    pip install 'protobuf==3.20.3'   (see requirements.txt)

Usage:
    # terminal 1
    tools/testserver/run.sh
    # terminal 2
    python tools/testserver/relay_smoketest.py [host] [port]
"""
import os
import random
import socket
import struct
import sys
import time

# Import the server's committed protobuf modules (repo ./server/plugins).
_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_REPO, "server"))
from plugins import c2c_pb2, c2s_pb2  # noqa: E402

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 5566
# Any non-zero byte; both peers must match. Picked at random rather than fixed
# because the server relays every frame to *all* clients in the session: a real
# BomberCat on the same server (firmware default RELAY_SESSION 42 == 0x2A) would
# join the session and its OP_SYN would arrive here, breaking the byte-identity
# assertions below. Excluding 42 keeps this test isolated from a live device.
SESSION = random.choice([b for b in range(1, 256) if b != 42])


def recvn(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RuntimeError("connection closed by server")
        buf += chunk
    return buf


def send_frame(sock, payload, session):
    sock.sendall(struct.pack("!IB", len(payload), session) + payload)


def recv_frame(sock):
    length = struct.unpack("!I", recvn(sock, 4))[0]
    return recvn(sock, length)


def make_serverdata(apdu, source):
    nfc = c2c_pb2.NFCData()
    nfc.data_source = source
    nfc.data_type = c2c_pb2.NFCData.INITIAL
    nfc.timestamp = int(time.time() * 1000)
    nfc.data = apdu
    sd = c2s_pb2.ServerData()
    sd.opcode = c2s_pb2.ServerData.OP_PSH
    sd.data = nfc.SerializeToString()
    return sd.SerializeToString()


def decode(blob):
    sd = c2s_pb2.ServerData(); sd.ParseFromString(blob)
    nfc = c2c_pb2.NFCData(); nfc.ParseFromString(sd.data)
    return sd, nfc


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    return s


def main():
    print("Connecting to %s:%d (session 0x%02X)" % (HOST, PORT, SESSION))
    reader = connect()
    card = connect()

    # Card associates with the session first (empty NFCData placeholder).
    send_frame(card, make_serverdata(b"", c2c_pb2.NFCData.CARD), SESSION)
    time.sleep(0.2)

    # reader -> card : SELECT PPSE
    ppse = bytes.fromhex("00A404000E325041592E5359532E444446303100")
    reader_blob = make_serverdata(ppse, c2c_pb2.NFCData.READER)
    send_frame(reader, reader_blob, SESSION)

    got = recv_frame(card)
    assert got == reader_blob, "card did not receive identical blob"
    sd, nfc = decode(got)
    print("[OK] reader->card  opcode=%s source=%s apdu=%s" % (
        c2s_pb2.ServerData.Opcode.Name(sd.opcode),
        c2c_pb2.NFCData.DataSource.Name(nfc.data_source),
        bytes(nfc.data).hex()))
    assert bytes(nfc.data) == ppse

    # card -> reader : FCI response
    resp = bytes.fromhex("6F23840E325041592E5359532E4444463031A5119000")
    card_blob = make_serverdata(resp, c2c_pb2.NFCData.CARD)
    send_frame(card, card_blob, SESSION)

    got2 = recv_frame(reader)
    assert got2 == card_blob, "reader did not receive identical blob"
    sd2, nfc2 = decode(got2)
    print("[OK] card->reader  opcode=%s source=%s apdu=%s" % (
        c2s_pb2.ServerData.Opcode.Name(sd2.opcode),
        c2c_pb2.NFCData.DataSource.Name(nfc2.data_source),
        bytes(nfc2.data).hex()))
    assert bytes(nfc2.data) == resp

    reader.close(); card.close()
    print("\nRELAY SMOKE TEST PASSED")


if __name__ == "__main__":
    try:
        main()
    except (ConnectionRefusedError, OSError) as e:
        sys.exit("Could not reach server at %s:%d (%s). Start it with "
                 "tools/testserver/run.sh" % (HOST, PORT, e))
