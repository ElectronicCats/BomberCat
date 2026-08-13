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
CONTAINER="bombercat-nfcgate-server-run"

echo ">> Building $IMAGE"
docker build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE" "$REPO_ROOT/server"

# A crashed/detached previous run can leave the name taken; clear it first so
# --name below always succeeds.
docker rm -f "$CONTAINER" >/dev/null 2>&1 || true

# Stop the container cleanly on Ctrl-C. `docker stop` sends SIGTERM, which the
# server process exits on silently — unlike SIGINT, which the upstream server.py
# turns into an uncaught KeyboardInterrupt and dumps a traceback.
stop_server() {
    echo
    echo ">> Stopping $CONTAINER …"
    docker stop "$CONTAINER" >/dev/null 2>&1 || true
    docker rm   "$CONTAINER" >/dev/null 2>&1 || true
}
trap stop_server INT TERM

echo ">> Running $IMAGE on host port $PORT (container 5566). Ctrl-C to stop."
# --sig-proxy=false keeps our terminal's Ctrl-C (SIGINT) from being forwarded
# into the container; we handle shutdown via the trap above instead. Run in the
# background so the trap fires while we wait, rather than during `exec`.
docker run --rm --sig-proxy=false --name "$CONTAINER" \
    -p "${PORT}:5566" "$IMAGE" &
run_pid=$!

rc=0
wait "$run_pid" || rc=$?
exit "$rc"
