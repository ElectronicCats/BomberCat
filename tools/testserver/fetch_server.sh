#!/usr/bin/env bash
#
# fetch_server.sh — clone the pinned nfcgate-server used as a local test
# fixture for the BomberCat relay smoke test.
#
# The server is a *dev-only, throwaway* dependency (it is not part of the
# firmware or the CLI), so it is NOT vendored or added as a submodule: it is
# cloned on demand into ./server, which is gitignored. Run this once before
# tools/testserver/run.sh or relay_smoketest.py.
#
# Pinned upstream (see firmware/core/proto/UPSTREAM.md):
#   nfcgate/server @ 4d32cc1
#
# The nested `protocol` submodule is intentionally NOT initialized: server.py
# only needs plugins/ and the committed *_pb2.py at runtime.
#
# Override the source (e.g. an existing local clone, or offline mirror) with:
#   SERVER_REPO=/path/or/url tools/testserver/fetch_server.sh
#
set -euo pipefail

SERVER_COMMIT="4d32cc1"
SERVER_REPO="${SERVER_REPO:-https://github.com/nfcgate/server}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEST="$REPO_ROOT/server"

if [ -f "$DEST/server.py" ]; then
    have="$(git -C "$DEST" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    if [ "$have" = "$SERVER_COMMIT" ]; then
        echo ">> server already present at $DEST (@$have) — nothing to do"
        exit 0
    fi
    echo ">> $DEST exists but is @$have (want @$SERVER_COMMIT)"
    echo "   remove it and re-run, or 'git -C server checkout $SERVER_COMMIT'"
    exit 1
fi

echo ">> Cloning $SERVER_REPO into $DEST"
git clone "$SERVER_REPO" "$DEST"
echo ">> Pinning to $SERVER_COMMIT"
git -C "$DEST" checkout --quiet "$SERVER_COMMIT"
echo ">> Done. Server pinned at $(git -C "$DEST" rev-parse --short HEAD)."
echo "   Next: tools/testserver/run.sh   (then relay_smoketest.py)"
