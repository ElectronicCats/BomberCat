#!/usr/bin/env python3

# Electronic Cats
# `bombercat testserver` — thin CLI wrapper around the local nfcgate-server
# helpers in tools/testserver/ (run.sh + relay_smoketest.py).
# Distributed as-is; no warranty is given.

import json
import os
import subprocess
import sys
from pathlib import Path

# External
import click
from rich import box
from rich.progress import (
    BarColumn,
    Progress,
    SpinnerColumn,
    TextColumn,
)
from rich.table import Table

# Internal
from . import preflight
from ..utils.output import (
    console,
    fmt_command,
    print_dim,
    print_error,
    print_error_panel,
    print_info,
    print_success_panel,
)

# tools/modules/testserver/cli.py -> parents[2] == tools/
TOOLS_DIR = Path(__file__).resolve().parents[2]
TESTSERVER_DIR = TOOLS_DIR / "testserver"
RUN_SH = TESTSERVER_DIR / "run.sh"
SMOKETEST = TESTSERVER_DIR / "relay_smoketest.py"
VERIFY = TESTSERVER_DIR / "verify_patch.py"
SMOKE_REQS = TESTSERVER_DIR / "requirements.txt"
SERVER_DIR = TOOLS_DIR.parent / "server"
# Kept in sync with run.sh, which names the container it starts.
CONTAINER = "bombercat-nfcgate-server-run"
FETCH_SH = TESTSERVER_DIR / "fetch_server.sh"

# The smoke test imports the server's committed *_pb2.py, which need the classic
# protobuf 3.x runtime (see requirements.txt). We do not want that pin leaking
# into the interpreter running the CLI — and on most distros it cannot be
# installed there anyway (PEP 668 marks the system Python externally managed) —
# so we bootstrap a throwaway venv for it, same as tools/gen_proto.sh does.
SMOKE_VENV = Path(os.environ.get("BOMBERCAT_SMOKE_VENV", TOOLS_DIR / ".venv-smoke"))


def _has_protobuf(python) -> bool:
    """True if `python` can import the protobuf runtime."""
    return (
        subprocess.run(
            [str(python), "-c", "import google.protobuf"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    )


def _smoketest_python() -> str:
    """Return an interpreter that can import the protobuf runtime.

    Prefers the one running the CLI; otherwise creates/reuses SMOKE_VENV.
    """
    if _has_protobuf(sys.executable):
        return sys.executable

    venv_python = SMOKE_VENV / "bin" / "python"
    if os.name == "nt":
        venv_python = SMOKE_VENV / "Scripts" / "python.exe"
    if venv_python.exists() and _has_protobuf(venv_python):
        return str(venv_python)

    print_info(f"Bootstrapping protobuf runtime in {SMOKE_VENV} (one time) …")
    try:
        subprocess.run(
            [sys.executable, "-m", "venv", str(SMOKE_VENV)], check=True
        )
        subprocess.run(
            [str(venv_python), "-m", "pip", "install", "-q", "-r", str(SMOKE_REQS)],
            check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print_error(
            f"could not prepare the protobuf runtime: {e}\n"
            f"Install it manually and re-run:\n"
            f"  python3 -m venv {SMOKE_VENV}\n"
            f"  {venv_python} -m pip install -r {SMOKE_REQS}"
        )
        sys.exit(1)
    return str(venv_python)


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

    # Everything the build needs, checked before we claim to be starting
    # anything — each of these exits with its own explanation. Order matters:
    # the port check asks Docker about leftover containers.
    preflight.check_server_sources(SERVER_DIR, FETCH_SH)
    preflight.check_docker()
    preflight.check_port(port, CONTAINER)

    env = {**os.environ, "PORT": str(port)}
    print_info(f"Starting nfcgate-server on host port {port} (Ctrl-C to stop) …")
    try:
        rc = subprocess.run(["bash", str(RUN_SH)], env=env).returncode
    except KeyboardInterrupt:
        # run.sh installs its own SIGINT trap to stop the container cleanly; the
        # signal reaches us too, so just swallow it and report a normal stop.
        print_info("server stopped.")
        rc = 0
    except FileNotFoundError:
        # `bash` itself is missing: Docker was already proven usable above.
        print_error_panel(
            preflight.TITLE,
            "bash is not installed, or not on your PATH.",
            "The CLI starts the server through a shell script "
            f"({preflight.short_path(RUN_SH)}),\nso it needs bash to launch it.",
            fix=[
                "Install bash with your package manager (e.g. "
                + fmt_command("sudo apt install bash"),
                f"{fmt_command('bombercat testserver run')}\n     re-run this command",
            ],
        )
        rc = 1
    sys.exit(rc)


@testserver.command("verify")
@click.argument("host", default="127.0.0.1")
@click.argument("port", default=5566, type=int)
@click.option("-n", "--rounds", default=8, show_default=True,
              help="Relayed frames to measure.")
def verify(host, port, rounds):
    """Check that a RUNNING server carries the relay latency patch.

    Grepping server.py only proves the file on disk is patched. This asks the
    server on the wire, so it also catches a Docker container still running an
    image built before the patch.
    """
    if not VERIFY.exists():
        print_error(f"Verifier not found: {VERIFY}")
        sys.exit(1)

    # Same protobuf dependency as the smoke test — it builds real ServerData
    # frames so the server's `log` plugin can decode them.
    preflight.check_server_sources(SERVER_DIR, FETCH_SH)

    python = _smoketest_python()

    print_info(f"Verifying latency patch → [bold]{host}:{port}[/bold]")
    sys.exit(_render_verify(python, host, port, rounds))


def _render_verify(python, host, port, rounds) -> int:
    """Draw verify_patch.py's JSON-line report and return its exit code.

    The verifier owns the measurement and the wording; this owns the pixels. It
    streams so the progress bar advances with the relay, which matters on a link
    slow enough that you would otherwise stare at a frozen terminal.
    """
    proc = subprocess.Popen(
        [python, str(VERIFY), host, str(port), "-n", str(rounds), "--json"],
        stdout=subprocess.PIPE,
        text=True,
    )

    table = _rounds_table()
    result = error = None
    progress = Progress(
        SpinnerColumn(style="cyan"),
        TextColumn("[cyan]relaying frames[/cyan]"),
        BarColumn(bar_width=24, complete_style="cyan", finished_style="cyan"),
        TextColumn("[dim]{task.completed}/{task.total}[/dim]"),
        TextColumn("[dim]{task.fields[last]}[/dim]"),
        console=console,
        transient=True,
    )

    with progress:
        task = progress.add_task("", total=rounds, last="")
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except ValueError:
                # Anything the child writes that is not one of our events (a
                # protobuf deprecation warning, say) is passed through as-is.
                console.print(f"[dim]{line}[/dim]")
                continue

            kind = event.get("event")
            if kind == "start":
                progress.update(task, total=event["rounds"])
                print_dim("session 0x%02X · %d rounds"
                          % (event["session"], event["rounds"]))
            elif kind == "round":
                _add_round(table, event)
                progress.update(
                    task,
                    completed=event["i"],
                    last="last %.2f ms" % event["total_ms"],
                )
            elif kind == "result":
                result = event
            elif kind == "error":
                error = event

    rc = proc.wait()

    if error:
        print_error_panel(
            "Latency patch not verified",
            error["message"],
            fix=error.get("fix") or None,
            fix_title="What to check",
        )
        return rc or 2

    if result is None:
        print_error(f"the verifier exited with code {rc} without a verdict.")
        return rc or 2

    console.print(table)
    console.print(_summary_grid(result))
    _print_verdict(result, host, port)
    return rc


def _rounds_table() -> Table:
    """One row per relayed frame: how it arrived, and what that cost."""
    table = Table(box=box.SIMPLE_HEAD, header_style="cyan bold",
                  pad_edge=False, padding=(0, 2))
    table.add_column("#", justify="right", style="dim")
    table.add_column("First recv", justify="right")
    table.add_column("Frame arrived")
    table.add_column("Gap after header", justify="right")
    table.add_column("Relay round trip", justify="right")
    return table


def _add_round(table: Table, event) -> None:
    whole = event["whole"]
    table.add_row(
        str(event["i"]),
        "%d B" % event["first_len"],
        "[green]1 segment[/green]" if whole else "[red]split in 2[/red]",
        _ms(event["gap_ms"], warn=not whole),
        _ms(event["total_ms"]),
    )


def _ms(value: float, warn: bool = False) -> str:
    """A millisecond figure, dimmed when it is noise and red when it is a stall."""
    text = "%.2f ms" % value
    if warn and value > 15.0:
        return f"[red]{text}[/red]"
    if value < 1.0:
        return f"[dim]{text}[/dim]"
    return text


def _summary_grid(result) -> Table:
    """The three numbers the verdict rests on, in the wording the docs quote."""
    split, rounds = result["split"], result["rounds"]
    split_style = "red bold" if split > rounds // 2 else "green bold"

    grid = Table.grid(padding=(0, 2))
    grid.add_column(style="dim")
    grid.add_column(justify="right")
    grid.add_row("  frames split across segments",
                 f"[{split_style}]{split} / {rounds}[/{split_style}]")
    grid.add_row("  median gap after header", "%.2f ms" % result["median_gap_ms"])
    grid.add_row("  median relay round trip", "%.2f ms" % result["median_total_ms"])
    return grid


def _print_verdict(result, host, port) -> None:
    """The verdict panel — red with numbered fix steps, or green with caveats."""
    title = f"Latency patch · {host}:{port}"
    notes = result.get("notes") or []
    headline, why, rest = result["headline"], notes[0] if notes else "", notes[1:]

    if result["verdict"] == "missing":
        print_error_panel(
            title,
            headline,
            why=f"{why}\n\n" + "\n\n".join(rest) if rest else why,
            fix=result.get("fix") or None,
        )
    else:
        print_success_panel(title, headline, why=why, notes=rest)


@testserver.command("smoke")
@click.argument("host", default="127.0.0.1")
@click.argument("port", default=5566, type=int)
def smoke(host, port):
    """Run the relay smoke test against a running server (needs protobuf==3.20.3)."""
    if not SMOKETEST.exists():
        print_error(f"Smoke test not found: {SMOKETEST}")
        sys.exit(1)

    # The test imports server/plugins/*_pb2.py from the on-demand clone.
    preflight.check_server_sources(SERVER_DIR, FETCH_SH)

    python = _smoketest_python()

    print_info(f"Relay smoke test → {host}:{port}")
    rc = subprocess.run([python, str(SMOKETEST), host, str(port)]).returncode
    sys.exit(rc)
