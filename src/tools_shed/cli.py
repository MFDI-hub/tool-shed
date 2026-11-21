"""Command line entry point for tools-shed."""

from .launch import TOOLS, find_binary


def main():
    """Print the tools this package installs and whether each binary is present."""
    print("tools-shed tools:")
    for name in TOOLS:
        path = find_binary(name)
        status = str(path) if path is not None else "not built"
        print("  {0}  ({1})".format(name, status))
