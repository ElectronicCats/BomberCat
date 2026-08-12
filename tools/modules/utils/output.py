"""
output.py - Shared Rich console and output helpers for all modules
"""

from rich.console import Console
from rich.style import Style
from rich.panel import Panel

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
