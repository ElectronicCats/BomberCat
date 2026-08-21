#!/usr/bin/env python3

# Electronic Cats
# Preflight checks for `bombercat testserver run`.
# Distributed as-is; no warranty is given.

"""Everything that must be true before `run.sh` can build and start the server.

On a clean machine `docker build` fails in ways whose native error text says
almost nothing about what to do next: a raw `EACCES` on the socket, or a "path
not found" for a build context the user never knew existed. We check the same
things here, *before* shelling out, so the CLI explains each one in its own UI.

`run.sh` keeps a terse copy of these checks for people who run it directly.
"""

from __future__ import annotations

import os
import platform
import shutil
import socket
import subprocess
import sys
from pathlib import Path

# Internal
from ..utils.output import (
    fmt_command,
    print_error_panel,
    print_info,
    print_success,
)

# External
import click

TITLE = "Cannot start the test server"


def short_path(path: Path) -> str:
    """The shortest readable spelling of `path`: relative to the CWD, or absolute.

    Keeps the printed commands both short *and* runnable from where the user
    actually is — `testserver/fetch_server.sh` from tools/, the full path from
    anywhere else.
    """
    try:
        rel = os.path.relpath(path, Path.cwd())
    except ValueError:  # different drive on Windows
        return str(path)
    return rel if len(rel) <= len(str(path)) else str(path)


# --------------------------------------------------------------------------- #
# 1. The nfcgate-server sources (also the Docker build context)
# --------------------------------------------------------------------------- #


def check_server_sources(server_dir: Path, fetch_script: Path) -> None:
    """Ensure the pinned nfcgate-server clone exists; offer to fetch it if not."""
    if (server_dir / "server.py").is_file():
        return

    fetch = short_path(fetch_script)
    print_error_panel(
        TITLE,
        "The nfcgate-server sources are missing.",
        "The test server is not part of this repo — it is the\n"
        "[bold]ElectronicCats/nfcgate-server[/bold] fork, used here as a throwaway test\n"
        "fixture, so it is neither committed nor a submodule. It has to be cloned\n"
        f"once, and that clone is also what Docker builds the image from.\n\n"
        f"Expected it at: [bold]{server_dir}/server.py[/bold]",
        fix=[
            f"{fmt_command(fetch)}\n"
            "     clones ElectronicCats/nfcgate-server@fc9103d — needs git and network, once",
            f"{fmt_command('bombercat testserver run')}\n"
            "     re-run this command; it will build and start the server",
        ],
        notes=[
            f"Offline or behind a mirror: SERVER_REPO=/path/to/clone {fetch}",
            "The clone is gitignored — deleting it only costs you a re-fetch.",
        ],
    )

    if not (sys.stdin.isatty() and sys.stdout.isatty()):
        sys.exit(1)
    if not click.confirm("  Fetch it now?", default=True):
        sys.exit(1)

    print_info(f"Running {fetch} …")
    if subprocess.run(["bash", str(fetch_script)]).returncode != 0:
        print_error_panel(
            TITLE,
            "Fetching the nfcgate-server failed.",
            "The clone did not complete — see the git output above.",
            fix=[
                "Check that [bold]git[/bold] is installed and you have network access",
                f"Re-run {fmt_command(fetch)} once the cause is fixed",
            ],
            notes=[f"Using an existing clone instead: SERVER_REPO=/path/to/clone {fetch}"],
        )
        sys.exit(1)
    print_success("nfcgate-server fetched.")


# --------------------------------------------------------------------------- #
# 2. Docker: installed, reachable, and usable by this user
# --------------------------------------------------------------------------- #


def _docker_group_state() -> str:
    """Where this user stands with the `docker` group.

    Returns one of:
      "active"        — the group is in effect for *this* process
      "pending-login" — the user is a member, but the session predates it
      "absent"        — the user is not a member
      "unknown"       — no docker group here (rootless, macOS, Windows)
    """
    try:
        # `grp` is Unix-only, and the group need not exist even there.
        import getpass
        import grp

        group = grp.getgrnam("docker")
    except (ImportError, KeyError, OSError):
        return "unknown"

    if group.gr_gid in os.getgroups():
        return "active"
    try:
        user = getpass.getuser()
    except Exception:
        return "unknown"
    return "pending-login" if user in group.gr_mem else "absent"


def _newgrp_install_hint() -> str | None:
    """How to install `newgrp` on this distro, when we recognise it.

    It lives in the shadow/login package, which minimal images and trimmed
    installs routinely leave out — the machine still has Docker, just no way to
    re-evaluate group membership without logging in again.
    """
    try:
        release = Path("/etc/os-release").read_text()
    except OSError:
        return None

    ids = " ".join(
        line.split("=", 1)[1].strip().strip('"').lower()
        for line in release.splitlines()
        if line.startswith(("ID=", "ID_LIKE="))
    )
    if "debian" in ids or "ubuntu" in ids:
        return "sudo apt install login"
    if "fedora" in ids or "rhel" in ids or "centos" in ids:
        return "sudo dnf install shadow-utils"
    if "arch" in ids:
        return "sudo pacman -S shadow"
    if "alpine" in ids:
        return "sudo apk add shadow"
    if "suse" in ids:
        return "sudo zypper install shadow"
    return None


def _activate_group_step() -> tuple[str, list[str]]:
    """(step, notes) for making the docker group effective in this session.

    `newgrp docker` is the usual answer, but it is not always there: it ships
    in shadow/login, so trimmed installs and container images have Docker and
    no `newgrp`. `sg` is the same package's sibling and is missing whenever
    `newgrp` is, so when neither exists only a fresh login can pick the group
    up — and we say so instead of printing a command that does not run.
    """
    if shutil.which("newgrp"):
        return (
            f"{fmt_command('newgrp docker')}\n"
            "     start a shell that has the group — or log out and back in",
            [],
        )

    if shutil.which("sg"):
        sg = fmt_command('sg docker -c "$SHELL"')
        return (
            f"{sg}\n"
            "     start a shell that has the group — newgrp is not installed\n"
            "     here, but sg does the same thing",
            [],
        )

    notes = [
        "`newgrp` is not installed on this machine, so nothing can apply the "
        "group to the session you are in — only a new login can.",
    ]
    if (install := _newgrp_install_hint()) is not None:
        notes.append(f"To get it: {install} — then `newgrp docker` works too.")
    # Spelled out with absolute paths: sudo resets PATH, and the CLI may well
    # be running from a venv that root's PATH knows nothing about.
    notes.append(
        f"One-off, without logging out: sudo -E {sys.executable} "
        f"{Path(sys.argv[0]).resolve()} testserver run"
    )
    return (
        "[bold]Log out and back in[/bold], or reboot\n"
        "     a fresh login is what reads your groups again",
        notes,
    )


def _permission_fix() -> tuple[str, list[str], list[str]]:
    """(why, fix steps, notes) for a socket that refuses this user."""
    state = _docker_group_state()

    activate, activate_notes = _activate_group_step()

    if state == "absent":
        return (
            "Docker runs as a privileged daemon and only members of the\n"
            "[bold]docker[/bold] group may talk to its socket. Your user is not one yet.",
            [
                f"{fmt_command('sudo usermod -aG docker \"$USER\"')}\n"
                "     add yourself to the group",
                activate,
                f"{fmt_command('bombercat testserver run')}\n     re-run this command",
            ],
            [
                "Group membership is applied at login: adding yourself is not "
                "enough on its own, the session has to pick it up.",
                *activate_notes,
                "Adding a user to the docker group grants root-equivalent access to "
                "the host. Prefer rootless Docker if that is a concern.",
            ],
        )

    if state == "pending-login":
        return (
            "Your user [bold]is[/bold] in the docker group, but this shell was started\n"
            "before that — group membership is only applied at login, so the\n"
            "session is still running without it.",
            [
                activate,
                f"{fmt_command('bombercat testserver run')}\n     re-run this command",
            ],
            [*activate_notes, "Verify with: id -nG | grep docker"],
        )

    # The group is already in effect (or there is none) and it still refuses:
    # unusual socket ownership, SELinux/AppArmor, or a stale DOCKER_HOST.
    notes = ["Inspect the socket with: ls -l /var/run/docker.sock"]
    if os.environ.get("DOCKER_HOST"):
        notes.insert(0, f"DOCKER_HOST is set to {os.environ['DOCKER_HOST']} — is it correct?")
    return (
        "The docker group is already in effect for this shell, yet the daemon\n"
        "still refuses the connection. That usually means the socket has\n"
        "unusual ownership, or Docker is running rootless under another user.",
        [
            f"{fmt_command('docker run --rm hello-world')}\n"
            "     confirm the problem is Docker itself, not this CLI",
            "Check the daemon's install mode (rootful vs rootless) and who owns\n"
            "     /var/run/docker.sock",
        ],
        notes,
    )


def check_docker() -> None:
    """Ensure the docker CLI exists and its daemon answers for this user."""
    if shutil.which("docker") is None:
        print_error_panel(
            TITLE,
            "Docker is not installed, or not on your PATH.",
            "The test server runs in a container: the CLI builds the image and\n"
            "starts it with Docker. Nothing else here needs it.",
            fix=[
                "Install Docker Engine: [underline]https://docs.docker.com/engine/install/[/underline]",
                f"{fmt_command('docker run --rm hello-world')}\n     check the install works",
                f"{fmt_command('bombercat testserver run')}\n     re-run this command",
            ],
            notes=[
                "You can also run the server without Docker — see the "
                "'Without Docker' section of tools/testserver/README.md.",
            ],
        )
        sys.exit(1)

    # `docker info` touches the daemon exactly like `docker build` will, so its
    # failure is the one the build would have hit — only earlier and quieter.
    probe = subprocess.run(
        ["docker", "info"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if probe.returncode == 0:
        return

    err = (probe.stderr or "").strip()

    if "permission denied" in err:
        why, fix, notes = _permission_fix()
        print_error_panel(
            TITLE,
            "Docker refused the connection: permission denied on its socket.",
            why,
            fix=fix,
            notes=notes,
        )
        sys.exit(1)

    if "Cannot connect to the Docker daemon" in err or "daemon is not running" in err:
        if platform.system() == "Darwin":
            start = ["Start [bold]Docker Desktop[/bold] and wait for it to report 'running'"]
        elif platform.system() == "Windows":
            start = ["Start [bold]Docker Desktop[/bold] and wait for it to report 'running'"]
        else:
            start = [
                f"{fmt_command('sudo systemctl start docker')}\n"
                "     start it now — add [bold]enable[/bold] instead of start to persist it",
            ]
        print_error_panel(
            TITLE,
            "The Docker daemon is not running.",
            "The docker command is installed, but nothing is listening on the\n"
            "other end of its socket.",
            fix=start + [f"{fmt_command('bombercat testserver run')}\n     re-run this command"],
            notes=[f"Daemon said: {err.splitlines()[0]}" if err else ""],
        )
        sys.exit(1)

    print_error_panel(
        TITLE,
        "Docker is installed but not usable.",
        f"`docker info` failed with:\n\n[dim]{err or 'no output'}[/dim]",
        fix=[
            f"{fmt_command('docker run --rm hello-world')}\n"
            "     reproduce it outside this CLI, then fix what it reports",
        ],
    )
    sys.exit(1)


# --------------------------------------------------------------------------- #
# 3. The host port we are about to publish
# --------------------------------------------------------------------------- #


def check_port(port: int, container_name: str) -> None:
    """Warn early if the host port is taken — `docker run` fails late and cryptically."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind(("0.0.0.0", port))
            return
        except OSError:
            pass

    # A container we started and never cleaned up is the likeliest culprit, and
    # the only one we can name precisely.
    ours = subprocess.run(
        ["docker", "ps", "--filter", f"name={container_name}", "--format", "{{.Names}}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    ).stdout.strip()

    if ours:
        print_error_panel(
            TITLE,
            f"Host port {port} is already used by a test server you left running.",
            f"The container [bold]{ours}[/bold] is still up and holds the port.",
            fix=[
                f"{fmt_command(f'docker rm -f {ours}')}\n     stop the old one",
                f"{fmt_command('bombercat testserver run')}\n     start a fresh one",
            ],
            notes=["Or leave it running and use it — it is the same server."],
        )
    else:
        print_error_panel(
            TITLE,
            f"Host port {port} is already in use by another program.",
            "Docker cannot publish the container on a port something else holds.",
            fix=[
                f"{fmt_command(f'bombercat testserver run -p {port + 1}')}\n"
                "     publish on a free port instead",
                f"{fmt_command(f'ss -ltnp | grep {port}')}\n"
                "     or find out what is holding it and stop that",
            ],
            notes=[
                "The container always listens on 5566 internally; -p only changes "
                "the host side, so point the boards at the host port you pick.",
            ],
        )
    sys.exit(1)
