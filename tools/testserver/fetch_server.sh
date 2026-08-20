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
# Source: the ElectronicCats/nfcgate-server fork, branch `v2`, pinned to
# (see firmware/core/proto/UPSTREAM.md):
#   ElectronicCats/nfcgate-server @ fc9103d
#     == nfcgate/server @ 4d32cc1 + tools/testserver/latency-fixes.patch
#
# The fork carries our relay latency fixes (Fases E and H of
# firmware/LATENCIA_OPTIMIZACION.md) as a real commit, so a fresh clone is
# already fast; apply_patch.sh below only re-asserts them and is a no-op.
#
# The nested `protocol` submodule is intentionally NOT initialized: server.py
# only needs plugins/ and the committed *_pb2.py at runtime.
#
# Override the source (e.g. an existing local clone, or offline mirror) with:
#   SERVER_REPO=/path/or/url tools/testserver/fetch_server.sh
#
# Fetch the PRISTINE upstream instead, to A/B a phase against its baseline the
# way firmware/LATENCIA_OPTIMIZACION.md measures one:
#   rm -rf server
#   SERVER_COMMIT=4d32cc1 BOMBERCAT_SKIP_LATENCY_PATCH=1 tools/testserver/fetch_server.sh
#
set -euo pipefail

SERVER_COMMIT="${SERVER_COMMIT:-fc9103d}"
SERVER_REPO="${SERVER_REPO:-https://github.com/ElectronicCats/nfcgate-server.git}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEST="$REPO_ROOT/server"

if [ -f "$DEST/server.py" ]; then
    have="$(git -C "$DEST" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    if [ "$have" = "$SERVER_COMMIT" ]; then
        echo ">> server already present at $DEST (@$have)"
        # Still re-assert the latency patch: an existing clone may predate it,
        # or have been reset with `git checkout`.
        bash "$SCRIPT_DIR/apply_patch.sh"
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

# The pinned fork commit already carries the latency fixes, but a clone can
# drift from it (a hand edit, or SERVER_COMMIT pointing at pristine upstream),
# so re-assert them here. Idempotent; run.sh re-asserts them too.
bash "$SCRIPT_DIR/apply_patch.sh" "$DEST"

echo ">> Done. Server pinned at $(git -C "$DEST" rev-parse --short HEAD) with the latency fixes."
echo "   Next: tools/testserver/run.sh   (then relay_smoketest.py)"
