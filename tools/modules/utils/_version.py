from importlib.metadata import version, PackageNotFoundError
from pathlib import Path

_version_file = Path(__file__).parent.parent.parent / "VERSION"

if _version_file.exists():
    # En desarrollo, siempre usamos el archivo VERSION local si existe
    __version__ = _version_file.read_text().strip()
else:
    try:
        __version__ = version("bombercat")
    except PackageNotFoundError:
        __version__ = "unknown"
