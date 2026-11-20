"""uv-scripts: Portable C tool for uv scripts, built for every platform.."""

from __future__ import annotations

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("uv-scripts")
except PackageNotFoundError:  # pragma: no cover
    __version__ = "0.0.0"

__all__ = ["__version__"]

# Re-export your public API from here (e.g. `from .core import Client`) and add
# each name to __all__. Avoid wildcard imports: they break static analysis.
