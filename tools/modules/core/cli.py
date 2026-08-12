#! /usr/bin/env python3

# Electronic Cats
# Original Creation Date: August 12, 2026
# This code is beerware; if you see me (or any other Electronic Cats
# member) at the local, and you've found our code helpful,
# please buy us a round!
# Distributed as-is; no warranty is given.

import logging
import os
import tempfile
import threading
import sys
import queue

# Internal
from ..utils._version import __version__

# External
import click
from rich.logging import RichHandler
from rich.panel import Panel
from rich.table import Table
from rich import box

from ..utils.output import (
    console,
    STYLES,
    print_success,
    print_warning,
    print_error,
    print_info,
    print_dim,
    print_empty_line,
    print_title,
    print_subtitle,
    print_example,
    print_alias_item,
)

import shutil
import subprocess
import platform
import time
from pathlib import Path

# APP Information
VERSION_NUMBER = __version__
COMPANY = "Electronic Cats - PWNLAB"
_FUNNY_PHRASES = [
    "Catching packets, not mice.",
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
    """Bombercat: All in one bombercat tools environment."""
    if verbose:
        logger.level = logging.INFO
    pass

def main_cli() -> None:
    if not os.environ.get("_bombercat_COMPLETE"):
        module = next((a for a in sys.argv[1:] if not a.startswith("-")), None)
        print_header(module)

    cli(prog_name="bombercat")
