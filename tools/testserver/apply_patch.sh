#!/usr/bin/env bash
#
# apply_patch.sh — make sure the on-demand nfcgate-server clone carries our relay
# latency fixes (Fases E and H of firmware/LATENCIA_OPTIMIZACION.md).
#
# Why this exists: the fixes live in `server.py`, which is server code cloned on
# demand into ./server (gitignored, see fetch_server.sh). Since the migration to
# the ElectronicCats/nfcgate-server fork they ship as a real commit (fc9103d), so
# on the normal path this script is a no-op assertion. It still earns its keep: a
# clone can be hand-edited, or pinned to pristine upstream with
# SERVER_COMMIT=4d32cc1, and the Dockerfile COPYs server.py at build time — an
# unpatched clone silently bakes a *correct but slow* relay (~13.5 s per
# transaction instead of ~4.2 s) into the image. That is a miserable bug to
# notice, because everything works; it is just slow. So the patch stays versioned
# (latency-fixes.patch) and re-asserted here before every build.
#
# Idempotent by design: safe to call on every run. An already-patched clone is
# detected and left alone.
#
# Usage:
#   tools/testserver/apply_patch.sh          # patch ./server if needed
#
# Escape hatch — run the pristine upstream instead (for A/B latency runs, the way
# LATENCIA_OPTIMIZACION.md measures a phase against its baseline). The pinned
# fork commit is patched at the source, so skipping this script is no longer
# enough — re-fetch the unpatched upstream commit too:
#   rm -rf server
#   SERVER_COMMIT=4d32cc1 BOMBERCAT_SKIP_LATENCY_PATCH=1 tools/testserver/fetch_server.sh
#   BOMBERCAT_SKIP_LATENCY_PATCH=1 tools/testserver/run.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEST="${1:-$REPO_ROOT/server}"
PATCH="$SCRIPT_DIR/latency-fixes.patch"

die() {
    echo "ERROR: $1" >&2
    shift
    for line in "$@"; do
        [ -n "$line" ] && echo "       $line" >&2 || echo >&2
    done
    exit 1
}

if [ "${BOMBERCAT_SKIP_LATENCY_PATCH:-0}" != "0" ]; then
    echo ">> BOMBERCAT_SKIP_LATENCY_PATCH set — leaving $DEST as-is."
    echo "   The relay will run whatever $DEST holds. Note the pinned fork commit"
    echo "   already contains the fixes, so this flag alone no longer gives you the"
    echo "   pristine baseline (~13.5 s/transaction instead of ~4.2 s — see"
    echo "   firmware/LATENCIA_OPTIMIZACION.md, Fases E and H). For that, re-fetch:"
    echo "     rm -rf $DEST && SERVER_COMMIT=4d32cc1 BOMBERCAT_SKIP_LATENCY_PATCH=1 \\"
    echo "       $SCRIPT_DIR/fetch_server.sh"
    exit 0
fi

[ -f "$DEST/server.py" ] || die "no server.py at $DEST" \
    "The nfcgate-server clone is missing. Fetch it once with:" \
    "  $SCRIPT_DIR/fetch_server.sh"

[ -f "$PATCH" ] || die "latency patch not found at $PATCH" \
    "It is committed in this repo; a missing file means the checkout is" \
    "incomplete. Restore it with:  git checkout -- ${PATCH#"$REPO_ROOT/"}"

# `git apply --reverse --check` succeeds only when the patch is ALREADY applied,
# which is how we stay idempotent. It works on a plain directory too (the clone
# is a git repo, but a hand-copied tree is not), so no repo is assumed.
if git -C "$DEST" apply --reverse --check "$PATCH" 2>/dev/null; then
    echo ">> Latency patch already applied to $DEST/server.py"
    exit 0
fi

if ! git -C "$DEST" apply --check "$PATCH" 2>/dev/null; then
    die "the latency patch does not apply to $DEST/server.py" \
        "It is written against nfcgate/server@4d32cc1 — the parent of the fork's" \
        "pinned fc9103d — and the clone matches neither: wrong commit, or" \
        "server.py was edited by hand." \
        "" \
        "Check the commit:  git -C $DEST rev-parse --short HEAD   (want fc9103d)" \
        "Reset the clone:   git -C $DEST checkout -- server.py" \
        "Start over:        rm -rf $DEST && $SCRIPT_DIR/fetch_server.sh" \
        "" \
        "To run the slow upstream anyway: BOMBERCAT_SKIP_LATENCY_PATCH=1"
fi

git -C "$DEST" apply "$PATCH"
echo ">> Applied the relay latency patch to $DEST/server.py (Fases E + H)"
