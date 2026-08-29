"""Console UI for the local SeedFinder server (exe / start.bat).

Draws an ASCII banner plus key endpoints before Flask starts, and enables
ANSI colors on the Windows console (Windows 10+ ships VT processing off).
"""

import ctypes
import os
import sys

_RESET = "\x1b[0m"
_GREEN = "\x1b[32m"
_YELLOW = "\x1b[33m"
_CYAN = "\x1b[36m"
_DIM = "\x1b[90m"

_BANNER = r"""
 _____           _ _____ _       _         
|   __|___ ___ _| |   __|_|___ _| |___ ___ 
|__   | -_| -_| . |   __| |   | . | -_|  _|
|_____|___|___|___|__|  |_|_|_|___|___|_|  """


def _enable_ansi() -> None:
    if os.name != "nt":
        return
    kernel32 = ctypes.windll.kernel32
    stdout = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
    mode = ctypes.c_uint32()
    if kernel32.GetConsoleMode(stdout, ctypes.byref(mode)):
        # VT processing + UTF-8 codepage so the box art prints on a real console
        # (the frozen exe otherwise uses the ANSI codepage, e.g. cp1252).
        kernel32.SetConsoleMode(stdout, mode.value | 0x0004)
        kernel32.SetConsoleOutputCP(65001)
    # Redirected output (logs, piped) bypasses the console: fall back to a UTF-8
    # text stream with replacement chars instead of crashing on encode.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


def print_banner(host: str, port: int, lib_path: str, lib_ok: bool) -> None:
    """Print the startup banner. Runs before Flask's own log lines."""
    _enable_ansi()
    lib_name = os.path.basename(lib_path)
    print()
    print(f"{_GREEN}{_BANNER}{_RESET}")
    print(f"{_DIM}  {'─' * 58}{_RESET}")
    print(f"{_DIM}  engine{_RESET}  {lib_name}  "
          f"{'(' + 'cubiomes + Bfinders + SeedCrackerX' + ')' if lib_ok else _YELLOW + 'failed to load!' + _RESET}")
    print()
    print(f"{_DIM}  api    {_RESET}  {_CYAN}http://{host}:{port}{_RESET}    {_DIM}/status · /scan · /seedcracker{_RESET}")
    print(f"{_DIM}  quit   {_RESET}  Ctrl+C")
    print()
    if host not in ("127.0.0.1", "localhost"):
        print(f"{_YELLOW}  WARNING: bound to {host} — anyone on your LAN can reach this API.{_RESET}")
    print(f"{_DIM}  {'─' * 58}{_RESET}")
    print()