#! /usr/bin/env python3

# Electronic Cats
# Original Creation Date: August 12, 2026
# This code is beerware; if you see me (or any other Electronic Cats
# member) at the local, and you've found our code helpful,
# please buy us a round!
# Distributed as-is; no warranty is given.

import logging
import os
import sys

# Internal
from ..utils._version import __version__
from .bombercat import DeviceError, DeviceLink, resolve_port
from ..device.cli import device as _device
from ..nfcgate.cli import (
    config as _config,
    monitor_cmd as _monitor,
    run_cmd as _run,
    status_cmd as _status,
    stop_cmd as _stop,
)
from ..capture.cli import capture as _capture
from ..proto.cli import proto as _proto
from ..testserver.cli import testserver as _testserver
from ..utils.cli_options import target_options

# External
import click
from rich.logging import RichHandler
from rich.panel import Panel

from ..utils.output import (
    console,
    STYLES,
    print_success,
    print_error,
    print_info,
    print_dim,
    print_empty_line,
    print_example,
)

import platform
from pathlib import Path

# APP Information
VERSION_NUMBER = __version__
COMPANY = "Electronic Cats"
_FUNNY_PHRASES = [
    "Catching packets, not mice.",
    "Nine lives, infinite payloads.",
    "Sniffing NFC, purring softly.",
    "Curiosity cloned the card.",
    "Too cool for a leash, too smart for a firewall.",
    "Emulating cards, ignoring humans.",
    "Relaying taps at the speed of paws.",
    "Magspoof: because swiping is for amateurs.",
    "13.56 MHz of pure feline mischief.",
    "Landed on all four antennas.",
    "Herding bits since 2026.",
    "Replaying the tag, chasing the laser.",
    "Every scratch is a side-channel.",
    "Keep calm and clone on.",
    "Whiskers on the wire.",
    "Not a bug, a feline feature.",
    "Cloning cards, breaking hearts.",
    "The cat's out of the sandbox.",
    "Silent paws, loud packets.",
    "Hack the planet, nap after.",
]

import random as _random

FUNNY_PHRASE = _random.choice(_FUNNY_PHRASES)

logger = logging.getLogger("rich")
FORMAT = "%(message)s"
logging.basicConfig(
    level="WARNING", format=FORMAT, datefmt="[%X]", handlers=[RichHandler(markup=True)]
)


def print_header(module=None):
    """Print the ASCII art header"""
    if module:
        label = f"bombercat {module}"
    elif platform.system() != "Windows" and os.geteuid() == 0:
        label = "bombercat: (root)"
    else:
        label = "bombercat"

    ascii_art = f"""      :=--             --=-       |
      -====-         -=====       |
      :===================-       |
       ===================:       |
  -   :==--===========--==-   -   |  {label}
 -===:===-   :=====-   -==-.-=--  |  v{VERSION_NUMBER}
--    ====-   :===-   -====    -- |  {FUNNY_PHRASE}
-=:   :===================-   .=- |
 ---=-- -===============-  -=---  |
 ---       --=======--        --  |"""

    colored_ascii = f"[cyan bold]{ascii_art}[/cyan bold]"

    header_panel = Panel(
        colored_ascii,
        title=f"[cyan]{COMPANY}[/cyan]",
        border_style=STYLES["header"],
        title_align="left",
        padding=(1, 2),
    )
    console.print(header_panel)


@click.group("bombercat", context_settings={"help_option_names": ["-h", "--help"]})
@click.option("-v", "--verbose", is_flag=True, help="Show Verbose mode")
def cli(verbose):
    """BomberCat: All in one bombercat tools environment."""
    if verbose:
        logger.level = logging.INFO
    pass


@click.command("identify", context_settings={"help_option_names": ["-h", "--help"]})
@target_options
def identify_cmd(port, device_id):
    """Blink a device's LED so you can tell which board an ID refers to."""
    try:
        target = resolve_port(port, device_id)
        with DeviceLink(target) as link:
            if not link.ping():
                print_error(f"{target} did not answer the handshake.")
                raise SystemExit(1)
            r = link.identify()
    except DeviceError as e:
        print_error(str(e))
        raise SystemExit(1)
    except Exception as e:
        print_error(f"{type(e).__name__}: {e}")
        raise SystemExit(1)

    if not r.ok:
        print_error(f"identify failed: {r.message}")
        if "unknown command" in r.message:
            # Pre-0.7.0 firmware has no `identify`; the CLI is newer than the board.
            print_info("this firmware predates `identify` — reflash "
                       "firmware/NFCGate to use it.")
        raise SystemExit(1)
    print_success(f"{target} is blinking its LED for a couple of seconds")


# ===================== Shell Completion Commands =====================


@click.group(context_settings={"help_option_names": ["-h", "--help"]})
def completion():
    """Install shell tab completion for bombercat."""
    pass


@completion.command("install")
@click.option(
    "--shell",
    type=click.Choice(["bash", "zsh", "fish"]),
    default=None,
    help="Shell to install completion for (auto-detected if omitted)",
)
def completion_install(shell):
    """Install tab completion for your shell.

    Run this once, then restart your shell (or source your rc file).

    \b
        bombercat completion install          # auto-detect shell
        bombercat completion install --shell zsh
    """
    if platform.system() == "Windows":
        print_error("Shell completion is not supported on Windows.")
        sys.exit(1)

    import subprocess as _sp

    # Auto-detect shell
    if shell is None:
        shell_env = os.environ.get("SHELL", "")
        if "zsh" in shell_env:
            shell = "zsh"
        elif "fish" in shell_env:
            shell = "fish"
        elif "bash" in shell_env:
            shell = "bash"
        else:
            print_error("Could not detect shell. Use --shell bash|zsh|fish.")
            sys.exit(1)
        print_info(f"Detected shell: {shell}")

    env_var = "_BOMBERCAT_COMPLETE"

    # Absolute path to this script and the Python interpreter running it.
    # We always want completions to call "python /abs/path/to/bombercat.py" so
    # that they work regardless of whether bombercat is on PATH.
    script_abs = str(Path(sys.argv[0]).resolve())
    python_abs = str(Path(sys.executable).resolve())
    # The full command string that the completion script will execute
    cmd_to_call = f"{python_abs} {script_abs}"

    if shell == "bash":
        target = (
            Path.home()
            / ".local"
            / "share"
            / "bash-completion"
            / "completions"
            / "bombercat"
        )
        source_flag = "bash_source"
        rc_note = None
    elif shell == "zsh":
        target = Path.home() / ".zfunc" / "_bombercat"
        source_flag = "zsh_source"
        rc_note = "fpath=(~/.zfunc $fpath)\nautoload -Uz compinit && compinit"
    elif shell == "fish":
        target = Path.home() / ".config" / "fish" / "completions" / "bombercat.fish"
        source_flag = "fish_source"
        rc_note = None

    try:
        result = _sp.run(
            [python_abs, script_abs],
            env={**os.environ, env_var: source_flag},
            capture_output=True,
            text=True,
        )
        script = result.stdout
    except Exception as e:
        print_error(f"Failed to generate completion script: {e}")
        sys.exit(1)

    if not script.strip():
        print_error(
            "Empty completion script generated.\n"
            "Make sure you are running this command via:\n"
            f"  python {script_abs} completion install"
        )
        sys.exit(1)

    # ------------------------------------------------------------------ #
    # Post-process: replace the bare 'bombercat' program name that Click  #
    # embeds in the script with the full "python /abs/path/bombercat.py"  #
    # invocation.  We handle every pattern Click 7.x / 8.x can emit.      #
    # ------------------------------------------------------------------ #
    if shell == "zsh":
        # 1. #compdef directive — register for all the names a user might type
        script = script.replace(
            "#compdef bombercat", "#compdef bombercat bombercat.py ./bombercat.py"
        )
        # 2. The guard that aborts when the command is not found in $commands[].
        #    We neutralise it because we use an absolute path, not a PATH entry.
        script = script.replace(
            "(( ! $+commands[bombercat] ))",
            "false",  # 'false' evaluates to 1 so the (( )) block never returns
        )
        # 3. The line that actually calls the program to obtain completions.
        #    Click 8 emits:  _BOMBERCAT_COMPLETE=zsh_complete bombercat
        script = script.replace(
            f"{env_var}=zsh_complete bombercat", f"{env_var}=zsh_complete {cmd_to_call}"
        )
        # 4. The compdef registration at the bottom of the script
        script = script.replace(
            "compdef _bombercat_completion bombercat",
            "compdef _bombercat_completion bombercat bombercat.py ./bombercat.py",
        )

        # 5. Append an explicit wrapper so that "python bombercat.py <TAB>" and
        #    "./bombercat.py <TAB>" also trigger completion.  zsh matches on the
        #    last component of $words[1], so we register a catch-all that
        #    delegates to our function.
        extra = (
            "\n"
            "# Enable completion when invoked as 'python bombercat.py' or './bombercat.py'\n"
            "_bombercat_completion_python_wrapper() {\n"
            "  local script_name=${words[2]:t}  # basename of the script argument\n"
            "  if [[ $script_name == bombercat.py ]]; then\n"
            f"    (( ! $+functions[_bombercat_completion] )) && source {target}\n"
            '    words=(bombercat "${words[@]:2}")\n'
            "    (( CURRENT-- ))\n"
            "    _bombercat_completion\n"
            "  else\n"
            "    _files\n"
            "  fi\n"
            "}\n"
            "compdef _bombercat_completion_python_wrapper python python3\n"
        )
        script += extra

    elif shell == "bash":
        # Click <=8.0 emits:  _BOMBERCAT_COMPLETE=bash_complete bombercat
        # Click >=8.1 emits:  _BOMBERCAT_COMPLETE=bash_complete $1
        script = script.replace(
            f"{env_var}=bash_complete bombercat",
            f"{env_var}=bash_complete {cmd_to_call}",
        )
        script = script.replace(
            f"{env_var}=bash_complete $1",
            f"{env_var}=bash_complete {cmd_to_call}",
        )
        # Register for both 'bombercat' and 'bombercat.py' (Click 8.1 adds -o nosort)
        script = script.replace(
            "complete -F _bombercat_completion bombercat",
            "complete -F _bombercat_completion bombercat bombercat.py",
        )
        script = script.replace(
            "complete -o nosort -F _bombercat_completion bombercat",
            "complete -o nosort -F _bombercat_completion bombercat bombercat.py",
        )
        # Append a wrapper that intercepts 'python bombercat.py <TAB>'
        extra = (
            "\n"
            "# Enable completion when invoked as 'python bombercat.py'\n"
            "_bombercat_completion_python_wrapper() {\n"
            "    local cur script_arg\n"
            '    cur="${COMP_WORDS[COMP_CWORD]}"\n'
            '    script_arg="${COMP_WORDS[1]}"\n'
            '    if [[ "$(basename "$script_arg")" == "bombercat.py" ]]; then\n'
            "        # Rebuild COMP_WORDS without the leading 'python' / path\n"
            '        local new_words=(bombercat "${COMP_WORDS[@]:2}")\n'
            '        COMP_WORDS=("${new_words[@]}")\n'
            "        COMP_CWORD=$(( COMP_CWORD - 1 ))\n"
            "        _bombercat_completion\n"
            "    fi\n"
            "}\n"
            "complete -F _bombercat_completion_python_wrapper python python3\n"
        )
        script += extra

    elif shell == "fish":
        # Fish uses a different mechanism; just replace the bare program name.
        # Click <=8.0 puts it right after the env var, >=8.1 after COMP_CWORD.
        script = script.replace(
            f"{env_var}=fish_complete bombercat",
            f"{env_var}=fish_complete {cmd_to_call}",
        )
        script = script.replace(
            "COMP_CWORD=(commandline -t) bombercat)",
            f"COMP_CWORD=(commandline -t) {cmd_to_call})",
        )
        # Also complete when invoked as './bombercat.py'
        script += (
            "\ncomplete --no-files --command bombercat.py "
            '--arguments "(_bombercat_completion)"\n'
        )

    # Write script
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(script)
    print_success(f"Completion script written to: {target}")

    # zsh needs fpath entry in .zshrc
    if rc_note:
        zshrc = Path.home() / ".zshrc"
        existing = zshrc.read_text() if zshrc.exists() else ""
        if "~/.zfunc" not in existing and ".zfunc" not in existing:
            with zshrc.open("a") as f:
                f.write(f"\n# bombercat tab completion\n{rc_note}\n")
            print_success(f"Added fpath entry to {zshrc}")
        else:
            print_dim("~/.zfunc already in fpath — skipping .zshrc edit")

    print_empty_line()
    if shell == "bash":
        print_info("Restart your shell or run:")
        print_example(f"source {target}")
    elif shell == "zsh":
        print_info("Restart your shell or run:")
        print_example("source ~/.zshrc && compinit -u")
    elif shell == "fish":
        print_info("Completion is active immediately in new fish sessions.")


def main_cli() -> None:
    if not os.environ.get("_BOMBERCAT_COMPLETE"):
        module = next((a for a in sys.argv[1:] if not a.startswith("-")), None)
        print_header(module)

    # Device control plane (Fase 6): talk to a BomberCat over USB-serial.
    cli.add_command(_device)
    cli.add_command(identify_cmd)
    cli.add_command(_config)
    cli.add_command(_run)
    cli.add_command(_stop)
    cli.add_command(_status)
    cli.add_command(_monitor)
    cli.add_command(_capture)

    # Dev tooling under tools/
    cli.add_command(_proto)
    cli.add_command(_testserver)

    if platform.system() in ["Linux", "Darwin"]:
        cli.add_command(completion)

    cli(prog_name="bombercat")
