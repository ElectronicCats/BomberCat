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

# --------------------------------------------------------------------------- #
# Preflight: fail with an actionable message instead of a raw Docker/context
# error. These are the ways a clean machine trips over `run.sh`.
#
# `bombercat testserver run` checks the same things first and explains them at
# length (tools/modules/testserver/preflight.py); this terse copy is what backs
# the script when it is invoked directly.
# --------------------------------------------------------------------------- #
die() {
    echo "ERROR: $1" >&2
    shift
    for line in "$@"; do
        # Keep blank separators actually blank (no trailing indent).
        [ -n "$line" ] && echo "       $line" >&2 || echo >&2
    done
    exit 1
}

# 1. The server clone is the Docker *build context*, not just a `smoke` dep.
if [ ! -f "$REPO_ROOT/server/server.py" ]; then
    die "nfcgate-server not found at $REPO_ROOT/server" \
        "It is a dev-only fixture, cloned on demand. Fetch it once with:" \
        "  $SCRIPT_DIR/fetch_server.sh"
fi

# 2. Docker itself.
if ! command -v docker >/dev/null 2>&1; then
    die "docker is not installed (or not on PATH)" \
        "Install Docker Engine: https://docs.docker.com/engine/install/" \
        "Or run the server without Docker — see tools/testserver/README.md"
fi

# 3. Can we actually talk to the daemon? `docker build` connects to the socket
#    before it does anything useful, so check here and translate the failure.
if ! docker_err="$(docker info 2>&1 >/dev/null)"; then
    case "$docker_err" in
        *"permission denied"*)
            # newgrp/sg ship in shadow-utils (login on Debian); a trimmed
            # install can have Docker and neither, and then only a fresh login
            # applies the group. Only suggest what this machine actually has.
            if command -v newgrp >/dev/null 2>&1; then
                apply="Apply it to this shell:  newgrp docker"
            elif command -v sg >/dev/null 2>&1; then
                apply="Apply it to this shell:  sg docker -c \"\$SHELL\""
            else
                apply="Log out and back in — newgrp is not installed here, so"
                apply="$apply nothing can apply it in place"
            fi
            if getent group docker 2>/dev/null | grep -qw "${USER:-$(id -un)}"; then
                die "no permission to reach the Docker daemon (/var/run/docker.sock)" \
                    "You are in the 'docker' group, but this session predates it." \
                    "$apply" \
                    "" \
                    "One-off alternative: sudo -E PORT=$PORT $SCRIPT_DIR/run.sh"
            else
                die "no permission to reach the Docker daemon (/var/run/docker.sock)" \
                    "Add your user to the 'docker' group:" \
                    "  sudo usermod -aG docker \"\$USER\"" \
                    "$apply" \
                    "" \
                    "One-off alternative: sudo -E PORT=$PORT $SCRIPT_DIR/run.sh"
            fi
            ;;
        *"Cannot connect to the Docker daemon"*|*"daemon is not running"*)
            die "the Docker daemon is not running" \
                "Start it with:  sudo systemctl start docker" \
                "(on macOS/Windows: launch Docker Desktop)"
            ;;
        *)
            die "docker is not usable:" "$docker_err"
            ;;
    esac
fi

# 4. The clone is upstream code; our relay latency fixes (Fases E and H of
#    firmware/LATENCIA_OPTIMIZACION.md) are not in it. Re-assert them before the
#    build — the Dockerfile COPYs server.py, so an unpatched clone bakes a
#    correct-but-slow relay (~13.5 s/transaction instead of ~4.2 s) into the
#    image. Idempotent: a patched clone is detected and left alone.
bash "$SCRIPT_DIR/apply_patch.sh"

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
