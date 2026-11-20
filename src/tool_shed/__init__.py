"""tool-shed: C command-line tools installed as Python console scripts."""

from .launch import TOOLS, find_binary

try:
    from importlib.metadata import PackageNotFoundError, version
except ImportError:  # pragma: no cover  # Python < 3.8

    class PackageNotFoundError(Exception):
        pass

    def version(_name):
        raise PackageNotFoundError()


try:
    __version__ = version("tool-shed")
except PackageNotFoundError:  # pragma: no cover
    __version__ = "0.0.0"

__all__ = ["TOOLS", "__version__", "find_binary"]
