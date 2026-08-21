"""
output.py - Shared Rich console and output helpers for all modules
"""

from __future__ import annotations

import re

from rich.console import Console, Group
from rich.padding import Padding
from rich.panel import Panel
from rich.style import Style
from rich.table import Table

STYLES = {
    "header": Style(color="cyan", bold=True),
    "success": Style(color="green", bold=True),
    "warning": Style(color="yellow", bold=True),
    "error": Style(color="red", bold=True),
    "info": Style(color="blue", bold=True),
    "dim": Style(dim=True),
    "prompt": Style(color="magenta", bold=True),
    "device": Style(color="cyan"),
}

console = Console()


def print_success(message: str) -> None:
    console.print(f"[green]✓[/green] {message}", style=STYLES["success"])


def print_warning(message: str) -> None:
    console.print(f"[yellow]⚠[/yellow] {message}", style=STYLES["warning"])


def print_error(message: str) -> None:
    console.print(f"[red]✗[/red] {message}", style=STYLES["error"])


def print_info(message: str) -> None:
    console.print(f"[blue]ℹ[/blue] {message}", style=STYLES["info"])


def print_dim(message: str) -> None:
    console.print(f"  {message}", style=STYLES["dim"])


def print_step(step: int, total: int, message: str) -> None:
    console.print(f"[bold]Step {step}/{total}: {message}[/bold]")


def print_section(title: str) -> None:
    sep = "═" * 51
    console.print("")
    console.print(f"[bold]{sep}[/bold]")
    console.print(f"[bold]  {title}[/bold]")
    console.print(f"[bold]{sep}[/bold]")
    console.print("")


def print_empty_line() -> None:
    console.print("")


def print_title(message: str) -> None:
    console.print(f"\n[cyan bold]{message}[/cyan bold]")


def print_subtitle(message: str) -> None:
    console.print(f"\n  [yellow]{message}[/yellow]")


def print_example(command: str, description: str = "") -> None:
    if description:
        # Align descriptions somewhat manually if needed, or just print
        console.print(f"  [green]{command}[/green] {description}")
    else:
        console.print(f"  [green]{command}[/green]")


def print_alias_item(aliases: str, description: str, pad: int = 15) -> None:
    # If multiple aliases separated by '/', split and colorize
    parts = [p.strip() for p in aliases.split("/")]
    colored_aliases = " / ".join(f"[green]{p}[/green]" for p in parts)

    # Calculate visible length for padding
    visible_len = sum(len(p) for p in parts) + 3 * (len(parts) - 1)
    padding = " " * max(0, pad - visible_len)

    console.print(f"    {colored_aliases}{padding} → {description}")


def print_error_section(title: str) -> None:
    sep = "═" * 51
    console.print("")
    console.print(f"[bold red]{sep}[/bold red]")
    console.print(f"[bold red]  ⚠  {title}[/bold red]")
    console.print(f"[bold red]{sep}[/bold red]")
    console.print("")


def print_success_section(title: str) -> None:
    sep = "═" * 39
    console.print("")
    console.print(f"[green bold]{sep}[/green bold]")
    console.print(f"[green bold]  ✓  {title}[/green bold]")
    console.print(f"[green bold]{sep}[/green bold]")
    console.print("")


def print_instruction_step(step_num: int, instruction: str) -> None:
    # Prints formatted step instruction, keeping rich markup in instruction if passed
    console.print(f"  [white]{step_num}.[/white] {instruction}")


# Test output helpers with quiet mode support
_quiet_mode = False


def set_quiet_mode(quiet: bool) -> None:
    """Set quiet mode - suppresses detailed output."""
    global _quiet_mode
    _quiet_mode = quiet


def is_quiet_mode() -> bool:
    """Check if quiet mode is enabled."""
    return _quiet_mode


def print_test_header(title: str) -> None:
    """Print a test section header with panel."""
    if not _quiet_mode:
        console.print(Panel(f"[bold cyan]{title}[/bold cyan]", border_style="cyan"))


def print_test_step(step_name: str, description: str) -> None:
    """Print a test step header."""
    if not _quiet_mode:
        console.print(f"\n[blue][{step_name.upper()}][/blue] {description}...")


def print_test_pass(details: str = "", max_length: int = 100) -> None:
    """Print a test pass result with optional details."""
    if not _quiet_mode:
        console.print("[green]  ✓ PASS[/green]")
        if details:
            if len(details) > max_length:
                console.print(f"[dim]  Response: {details[:max_length]}...[/dim]")
            else:
                console.print(f"[dim]  Response: {details}[/dim]")


def print_test_fail(details: str = "") -> None:
    """Print a test fail result. Always printed, with optional details."""
    console.print("[red]  ✗ FAIL[/red]")
    if details:
        if len(details) > 100:
            console.print(f"[red]  Got: {details[:100]}[/red]")
        else:
            console.print(f"[red]  Got: {details}[/red]")


def print_test_summary(passed: int, total: int, test_type: str = "") -> None:
    """Print test summary line."""
    msg = f"{passed}/{total}"
    if test_type:
        msg += f" {test_type}"
    msg += " tests passed"
    console.print(f"\n[bold]Summary:[/bold] [green]{msg}[/green]")


def print_instruction_block(title: str, items: list[str]) -> None:
    """Print an instruction block with title and numbered items."""
    console.print(f"[yellow]  {title}[/yellow]")
    for item in items:
        console.print(f"    {item}")


def print_detail_message(message: str, indent: int = 2) -> None:
    """Print a detail message with optional indentation."""
    indent_str = " " * indent
    console.print(f"{indent_str}{message}")


def print_separator(char: str = "=", width: int = 60) -> None:
    """Print a separator line."""
    console.print(char * width)


def print_raw(text: str) -> None:
    """Print raw text without additional formatting."""
    console.print(text)


def fmt_command(text: str) -> str:
    """Mark up `text` as a copy-pasteable command (for use inside messages)."""
    return f"[green]{text}[/green]"


def numbered_steps(steps: list[str]) -> Padding:
    """Ordered steps laid out so wrapped lines hang under the text, not the number.

    A long step used to wrap back to the panel's left edge, which read as a new
    step. The grid keeps every continuation line — wrapped or explicit — in the
    text column.
    """
    grid = Table.grid(padding=(0, 1))
    grid.add_column(justify="right", style="white", no_wrap=True)
    grid.add_column(overflow="fold")
    for i, step in enumerate(steps, 1):
        # Callers hand-indent their own line breaks for the old flat layout;
        # the grid owns that alignment now, so collapse it back out.
        grid.add_row(f"{i}.", re.sub(r"\n[ \t]+", "\n", step))
    return Padding(grid, (0, 0, 0, 2))


def print_error_panel(
    title: str,
    problem: str,
    why: str = "",
    fix: list[str] | None = None,
    fix_title: str = "How to fix it",
    notes: list[str] | None = None,
) -> None:
    """A framed error: what failed, why, and the numbered steps that fix it.

    For failures the user has to *act* on — a missing dependency, a permission
    that must be granted — where a one-line `print_error` would leave them to
    guess the next move. Ordinary failures should stay on `print_error`.

    `problem` is the one-line summary, `why` the paragraph explaining it, `fix`
    the ordered steps (mark commands with `fmt_command` so they stand out), and
    `notes` the trailing asides. All accept Rich markup.
    """
    body: list = [f"[red bold]✗  {problem}[/red bold]"]

    if why:
        body += ["", why]

    if fix:
        body += ["", f"[yellow bold]{fix_title}[/yellow bold]", "", numbered_steps(fix)]

    notes = [n for n in (notes or []) if n]
    if notes:
        body += [""] + [f"[dim]{note}[/dim]" for note in notes]

    console.print("")
    console.print(
        Panel(
            Group(*body),
            title=f"[red bold]{title}[/red bold]",
            title_align="left",
            border_style=STYLES["error"],
            padding=(1, 2),
            expand=False,
        )
    )
    console.print("")


def print_success_panel(
    title: str,
    summary: str,
    why: str = "",
    notes: list[str] | None = None,
) -> None:
    """The mirror of `print_error_panel` for a check that came back clean.

    Same frame, same reading order — headline, then the paragraph explaining
    what was proven — so a pass and a fail of the same command look like two
    outcomes of one report instead of two unrelated screens.
    """
    body: list = [f"[green bold]✓  {summary}[/green bold]"]

    if why:
        body += ["", why]

    for note in [n for n in (notes or []) if n]:
        body += ["", f"[dim]{note}[/dim]"]

    console.print("")
    console.print(
        Panel(
            Group(*body),
            title=f"[green bold]{title}[/green bold]",
            title_align="left",
            border_style=STYLES["success"],
            padding=(1, 2),
            expand=False,
        )
    )
    console.print("")
