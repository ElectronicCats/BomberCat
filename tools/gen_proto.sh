#!/usr/bin/env bash
#
# gen_proto.sh — regenerate the embedded nanopb sources from the vendored
# NFCGate .proto files.
#
# Input : firmware/core/proto/*.proto  (+ matching *.options)
# Output: firmware/core/src/proto/*.pb.c, *.pb.h
#
# The generated files are committed to the repo so a normal firmware build does
# NOT need Python/protoc. Re-run this only when the vendored .proto or .options
# change. It bootstraps a throwaway virtualenv with pinned tool versions so the
# output is reproducible across machines.
#
# Pinned upstream (see firmware/core/proto/UPSTREAM.md):
#   nfcgate/protocol @ 804fa9a  (matches nfcgate v2.6.1 / server 4d32cc1)
#
set -euo pipefail

# --- pinned tool versions --------------------------------------------------
NANOPB_VERSION="0.4.9.1"
GRPCIO_TOOLS_VERSION="1.68.1"   # provides protoc for nanopb_generator

# --- paths -----------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROTO_DIR="$REPO_ROOT/firmware/core/proto"
OUT_DIR="$REPO_ROOT/firmware/core/src/proto"
VENV_DIR="${GEN_PROTO_VENV:-$SCRIPT_DIR/.venv-proto}"

PROTOS=(c2c.proto c2s.proto)

# --- bootstrap venv --------------------------------------------------------
if [ ! -x "$VENV_DIR/bin/nanopb_generator" ]; then
    echo ">> Creating generator venv at $VENV_DIR"
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install --quiet --upgrade pip
    "$VENV_DIR/bin/pip" install --quiet \
        "nanopb==${NANOPB_VERSION}" \
        "grpcio-tools==${GRPCIO_TOOLS_VERSION}"
fi

GEN="$VENV_DIR/bin/nanopb_generator"
echo ">> Using $("$GEN" --version 2>&1 | head -1)"

# --- generate --------------------------------------------------------------
mkdir -p "$OUT_DIR"
echo ">> Generating into $OUT_DIR"
"$GEN" --output-dir="$OUT_DIR" -I "$PROTO_DIR" \
    "${PROTOS[@]/#/$PROTO_DIR/}"

echo ">> Done:"
ls -1 "$OUT_DIR"/*.pb.* | sed 's/^/     /'
