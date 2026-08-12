#!/usr/bin/env python3

# Electronic Cats
# `bombercat testserver` — thin CLI wrapper around the local nfcgate-server
# helpers in tools/testserver/ (run.sh + relay_smoketest.py).
# Distributed as-is; no warranty is given.

import os
import subprocess
import sys
from pathlib import Path

# External
import click

# Internal
from ..utils.output import print_info, print_error

# tools/modules/testserver/cli.py -> parents[2] == tools/
TOOLS_DIR = Path(__file__).resolve().parents[2]
TESTSERVER_DIR = TOOLS_DIR / "testserver"
RUN_SH = TESTSERVER_DIR / "run.sh"
SMOKETEST = TESTSERVER_DIR / "relay_smoketest.py"


@click.group("testserver", context_settings={"help_option_names": ["-h", "--help"]})
def testserver():
    """Local nfcgate-server for relay testing (no hardware/RF)."""


@testserver.command("run")
@click.option(
    "-p",
    "--port",
    default=5566,
    show_default=True,
    help="Host port to publish (container always listens on 5566).",
)
def run(port):
    """Build (if needed) and run the local nfcgate-server in Docker. Ctrl-C to stop."""
    if not RUN_SH.exists():
        print_error(f"Server launcher not found: {RUN_SH}")
        sys.exit(1)

    env = {**os.environ, "PORT": str(port)}
    print_info(f"Starting nfcgate-server on host port {port} (Ctrl-C to stop) …")
    try:
        rc = subprocess.run(["bash", str(RUN_SH)], env=env).returncode
    except KeyboardInterrupt:
        rc = 130
    sys.exit(rc)


@testserver.command("smoke")
@click.argument("host", default="127.0.0.1")
@click.argument("port", default=5566, type=int)
def smoke(host, port):
    """Run the relay smoke test against a running server (needs protobuf==3.20.3)."""
    if not SMOKETEST.exists():
        print_error(f"Smoke test not found: {SMOKETEST}")
        sys.exit(1)

    print_info(f"Relay smoke test → {host}:{port}")
    rc = subprocess.run([sys.executable, str(SMOKETEST), host, str(port)]).returncode
    sys.exit(rc)
