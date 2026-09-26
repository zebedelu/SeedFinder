"""Console UI for the local SeedFinder server (exe / start.bat).

Draws an ASCII banner plus key endpoints before Flask starts, enables
ANSI colors on the Windows console (Windows 10+ ships VT processing off),
and offers the interactive update check for the frozen exe.
"""

import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request

_RESET = "\x1b[0m"
_GREEN = "\x1b[32m"
_YELLOW = "\x1b[33m"
_CYAN = "\x1b[36m"
_DIM = "\x1b[90m"

# Bump when cutting a release (tag vX.Y.Z must match). Compare against the
# latest GitHub release; older exes offer the download on startup.
APP_VERSION = "1.4.0"
_RELEASE_API = "https://api.github.com/repos/zebedelu/SeedFinder/releases/latest"
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


def check_for_update(allow_swap=False):
    """Interactive startup update check (frozen exe, or --check-update).

    Never raises and never blocks longer than the download itself: offline,
    rate-limited or broken payloads just skip the check. allow_swap=True
    replaces the running exe and relaunches it after exit (frozen only).
    """
    print(f"{_DIM}  Verificando atualizações...{_RESET}")
    try:
        release = _fetch_latest_release()
    except Exception:
        print(f"{_YELLOW}  Não foi possível verificar agora — seguindo com v{APP_VERSION}.{_RESET}")
        return
    tag = release.get("tag_name") or ""
    latest, current = _parse_ver(tag), _parse_ver(APP_VERSION)
    if latest is None or current is None or latest <= current:
        print(f"{_GREEN}  Você está na última versão (v{APP_VERSION}).{_RESET}")
        return
    print(f"{_YELLOW}  Nova versão disponível: {tag}  (v{APP_VERSION} → {tag}){_RESET}")
    try:
        answer = input(f"{_YELLOW}  Deseja baixar a nova versão? [y/N] {_RESET}").strip().lower()
    except (EOFError, KeyboardInterrupt):
        print()
        answer = ""
    if answer not in ("y", "yes", "s", "sim"):
        print(f"{_DIM}  Ok — seguindo com v{APP_VERSION}.{_RESET}")
        return
    _download_and_swap(release, tag, allow_swap)


def _download_and_swap(release, tag, allow_swap):
    url = next(
        (a.get("browser_download_url")
         for a in release.get("assets") or []
         if a.get("name") == "SeedFinder.exe"),
        None,
    )
    if not url:
        page = release.get("html_url") or "https://github.com/zebedelu/SeedFinder/releases"
        print(f"{_YELLOW}  A release {tag} não tem SeedFinder.exe — baixe em:{_RESET}\n    {page}")
        return

    frozen = getattr(sys, "frozen", False)
    target_dir = os.path.dirname(sys.executable) if frozen else os.getcwd()
    dest = os.path.join(target_dir, "SeedFinder.new.exe")
    if not os.access(target_dir, os.W_OK):
        print(f"{_YELLOW}  Pasta sem permissão de escrita — baixe manualmente:{_RESET}\n    {url}")
        return

    print(f"{_DIM}  Baixando {tag}...{_RESET}")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": _UA})
        with urllib.request.urlopen(req, timeout=60) as resp, open(dest, "wb") as out:
            shutil.copyfileobj(resp, out)
    except Exception as e:
        if os.path.exists(dest):
            os.remove(dest)
        print(f"{_YELLOW}  Download falhou ({e}) — tente de novo mais tarde.{_RESET}")
        return
    print(f"{_GREEN}  Download concluído: {dest}{_RESET}")

    if not (allow_swap and frozen):
        print(f"{_DIM}  Feche o programa e substitua o .exe por este arquivo para atualizar.{_RESET}")
        return

    exe = sys.executable
    old = exe + ".old"
    try:
        os.rename(exe, old)  # a running image may be renamed, just not overwritten
        os.rename(dest, exe)
    except OSError as e:
        if not os.path.exists(exe) and os.path.exists(old):
            try:
                os.rename(old, exe)  # rollback so the current exe keeps its name
            except OSError:
                pass
        print(f"{_YELLOW}  Não consegui trocar o .exe ({e}). O novo está em:\n"
              f"    {dest}\n  Feche e substitua manualmente.{_RESET}")
        return

    # Detached waiter: when this process exits, drop the .old image and
    # relaunch the new exe (waiting for the PID frees port 7890 first).
    waiter = (
        f"Wait-Process -Id {os.getpid()}; "
        f"Remove-Item -Force '{old}' -ErrorAction SilentlyContinue; "
        f"Start-Process -FilePath '{exe}'"
    )
    try:
        flags = ((subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)
                 if os.name == "nt" else 0)
        subprocess.Popen(["powershell", "-NoProfile", "-Command", waiter],
                         creationflags=flags)
    except OSError:
        print(f"{_YELLOW}  Não agendei a troca automática — rode o novo manualmente ao fechar.{_RESET}")
        return
    print(f"{_GREEN}  Feche o programa para concluir — ele reabre sozinho na nova versão.{_RESET}")