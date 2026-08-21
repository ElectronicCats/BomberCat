#!/usr/bin/env python3

# Electronic Cats
# `bombercat proto` — thin CLI wrapper around tools/gen_proto.sh.
# Distributed as-is; no warranty is given.

import subprocess
import sys
from pathlib import Path

# External
import click

# Internal
from ..utils.output import print_info, print_error, print_success

# tools/modules/proto/cli.py -> parents[2] == tools/
TOOLS_DIR = Path(__file__).resolve().parents[2]
GEN_PROTO = TOOLS_DIR / "gen_proto.sh"


@click.group("proto", context_settings={"help_option_names": ["-h", "--help"]})
def proto():
    """Nanopb protobuf sources for the NFCGate relay."""


@proto.command("gen")
def gen():
    """Regenerate firmware/core/src/proto/*.pb.{c,h} from the vendored .proto files."""
    if not GEN_PROTO.exists():
        print_error(f"Generator script not found: {GEN_PROTO}")
        sys.exit(1)

    print_info(f"Running {GEN_PROTO.name} (bootstraps a pinned venv on first run) …")
    rc = subprocess.run(["bash", str(GEN_PROTO)]).returncode
    if rc == 0:
        print_success("Protobuf sources regenerated.")
    sys.exit(rc)
