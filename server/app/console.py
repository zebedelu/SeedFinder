"""Console UI for the local SeedFinder server (exe / start.bat).

Draws an ASCII banner plus key endpoints before Flask starts, enables
ANSI colors on the Windows console (Windows 10+ ships VT processing off),
and prints the new-version notice for the frozen exe.
"""

import ctypes
import json
import os
import re
import sys
import urllib.request

_RESET = "\x1b[0m"
_BOLD = "\x1b[1m"
_GREEN = "\x1b[32m"
_YELLOW = "\x1b[33m"
_CYAN = "\x1b[36m"
_DIM = "\x1b[90m"

# Bump when cutting a release (tag vX.Y.Z must match). Compare against the
# latest GitHub release; older exes just print a highlighted releases link.
APP_VERSION = "1.5.0"
_RELEASE_API = "https://api.github.com/repos/zebedelu/SeedFinder/releases/latest"
_RELEASES_PAGE = "https://github.com/zebedelu/SeedFinder/releases"
_UA = "SeedFinder.exe"

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
    print(f"{_DIM}  version{_RESET}  v{APP_VERSION}")
    print(f"{_DIM}  engine{_RESET}  {lib_name}  "
          f"{'(' + 'cubiomes + Bfinders + SeedCracker' + ')' if lib_ok else _YELLOW + 'failed to load!' + _RESET}")
    print()
    print(f"{_DIM}  api    {_RESET}  {_CYAN}http://{host}:{port}{_RESET}    {_DIM}/status · /scan · /seedcracker{_RESET}")
    print(f"{_DIM}  quit   {_RESET}  Ctrl+C")
    print()
    if host not in ("127.0.0.1", "localhost"):
        print(f"{_YELLOW}  WARNING: bound to {host} — anyone on your LAN can reach this API.{_RESET}")
    print(f"{_DIM}  {'─' * 58}{_RESET}")
    print()


def _parse_ver(text):
    """'v1.4.0' / '1.4.0' -> (1, 4, 0); anything else -> None."""
    text = str(text).strip().lstrip("vV")
    if not re.fullmatch(r"\d+(\.\d+)*", text):
        return None
    return tuple(int(p) for p in text.split("."))


def _fetch_latest_release():
    req = urllib.request.Request(
        _RELEASE_API,
        headers={"User-Agent": _UA, "Accept": "application/vnd.github+json"},
    )
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.load(resp)


def check_for_update():
    """Startup update notice (frozen exe, or --check-update).

    Never raises, never prompts and never blocks the server: offline,
    rate-limited or broken payloads just skip the check. A newer release
    only prints a highlighted link to the releases page.
    """
    _enable_ansi()  # box-drawing chars need the UTF-8 console (banner may not have run)
    try:
        release = _fetch_latest_release()
    except Exception:
        print(f"{_DIM}  Não foi possível verificar atualizações agora.{_RESET}")
        return
    tag = release.get("tag_name") or ""
    latest, current = _parse_ver(tag), _parse_ver(APP_VERSION)
    if latest is None or current is None or latest <= current:
        print(f"{_GREEN}  Você está na última versão (v{APP_VERSION}).{_RESET}")
        return
    _print_update_box(tag)


def _print_update_box(tag):
    """Highlighted, non-blocking banner pointing at the releases page."""
    width = 54
    rows = [
        f"ATUALIZAÇÃO DISPONÍVEL!   v{APP_VERSION} → {tag}",
        "",
        f"em {_RELEASES_PAGE}",
    ]
    print(f"{_YELLOW}  ┌{'─' * width}┐{_RESET}")
    for row in rows:
        print(f"{_YELLOW}  │{_RESET} {_BOLD}{row:<{width - 2}}{_RESET}{_YELLOW} │{_RESET}")
    print(f"{_YELLOW}  └{'─' * width}┘{_RESET}")