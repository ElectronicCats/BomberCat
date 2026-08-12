#!/usr/bin/env bash
#
# run.sh — build and run the local nfcgate-server for relay testing.
#
# Listens on 0.0.0.0:5566 (the nfcgate default). Loads the "log" plugin so every
# relayed frame is decoded and printed. Ctrl-C to stop.
#
# Usage:
#   tools/testserver/run.sh              # build (if needed) + run, port 5566
#   PORT=6000 tools/testserver/run.sh    # publish on a different host port
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PORT="${PORT:-5566}"
IMAGE="bombercat-nfcgate-server"

echo ">> Building $IMAGE"
docker build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE" "$REPO_ROOT/server"

echo ">> Running $IMAGE on host port $PORT (container 5566). Ctrl-C to stop."
exec docker run --rm -p "${PORT}:5566" "$IMAGE"
