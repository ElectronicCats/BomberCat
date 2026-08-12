#!/usr/bin/env bash
#
# build_and_run.sh — compile the firmware NfcGateCodec + vendored nanopb on the
# host and run a reader<->card loopback against a running nfcgate-server.
#
# This links the *actual* firmware codec (no re-implementation) so a green run
# proves the RP2040 wire format matches the server. RF/PN7150 are not involved.
#
# Usage:
#   tools/testserver/run.sh                                   # terminal 1
#   tools/testserver/codec_hosttest/build_and_run.sh [host] [port]   # terminal 2
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
CORE_SRC="$REPO_ROOT/firmware/core/src"
OUT="$SCRIPT_DIR/hosttest"

CXX="${CXX:-g++}"

echo ">> Compiling host codec test"
"$CXX" -std=c++11 -O1 -Wall -Wextra \
    -I "$CORE_SRC" \
    -o "$OUT" \
    "$SCRIPT_DIR/hosttest.cpp" \
    "$CORE_SRC/NfcGateCodec.cpp" \
    "$CORE_SRC/proto/c2c.pb.c" \
    "$CORE_SRC/proto/c2s.pb.c" \
    "$CORE_SRC/pb_common.c" \
    "$CORE_SRC/pb_encode.c" \
    "$CORE_SRC/pb_decode.c"

echo ">> Running"
exec "$OUT" "${1:-127.0.0.1}" "${2:-5566}"
