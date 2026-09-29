"""Build SeedFinder.exe — a single-file executable with embedded Python + DLL.

Usage:
    cd SeedFinder
    python server/build_exe.py

Output:
    server/dist/SeedFinder.exe

Code signing (optional — unsigned, no-reputation downloads get flagged by
SmartScreen/antiviruses). Set ONE of:

    SEEDFINDER_SIGN_THUMBPRINT=<SHA1 thumbprint>   cert in the Windows store
                                                    (USB token / HSM, typical
                                                    for OV/EV certs since 2023)
    SEEDFINDER_SIGN_PFX=<path.pfx>                 file-based cert
    SEEDFINDER_SIGN_PFX_PASSWORD=<password>        (only for PFX)

Signing needs signtool.exe (Windows 10/11 SDK, "Signing Tools" component).
With neither variable set the build still succeeds, unsigned.
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SERVER_DIR = os.path.dirname(__file__)
DLL_PATH = os.path.join(ROOT, 'build_server', 'seedfinder_lib.dll')
DIST_DIR = os.path.join(SERVER_DIR, 'dist')
SPEC_DIR = os.path.join(SERVER_DIR, 'build_spec')
INDEX_FILE = os.path.join(SERVER_DIR, 'index.py')

_TIMESTAMP = 'http://timestamp.digicert.com'


def _find_signtool():
    """signtool.exe from PATH, else the newest Windows Kits install."""
    exe = shutil.which('signtool')
    if exe:
        return exe
    for var in ('ProgramFiles(x86)', 'ProgramFiles'):
        base = os.environ.get(var)
        kits = os.path.join(base, 'Windows Kits', '10', 'bin') if base else ''
        if not (kits and os.path.isdir(kits)):
            continue
        versions = []
        for entry in os.listdir(kits):
            if not entry[0].isdigit():
                continue
            for sub in (os.path.join('x64', 'signtool.exe'),
                        os.path.join('x86', 'signtool.exe'),
                        'signtool.exe'):
                path = os.path.join(kits, entry, sub)
                if os.path.isfile(path):
                    versions.append((tuple(int(p) for p in entry.split('.') if p.isdigit()), path))
                    break
        if versions:
            return max(versions)[1]
    return None


def _sign_exe(exe_path):
    """Authenticode-sign exe_path when configured; no-op otherwise.

    Fails the build if signing was requested but cannot run, so an unsigned
    binary never reaches a release by accident.
    """
    thumb = os.environ.get('SEEDFINDER_SIGN_THUMBPRINT', '').strip()
    pfx = os.environ.get('SEEDFINDER_SIGN_PFX', '').strip()
    if not thumb and not pfx:
        print('Signing: skipped (set SEEDFINDER_SIGN_THUMBPRINT or SEEDFINDER_SIGN_PFX)')
        return
    signtool = _find_signtool()
    if not signtool:
        print('ERROR: signing requested but signtool.exe not found — install the '
              'Windows 10/11 SDK (Signing Tools component).', file=sys.stderr)
        sys.exit(1)
    cmd = [signtool, 'sign', '/fd', 'SHA256', '/td', 'SHA256',
           '/tr', _TIMESTAMP]
    if thumb:
        cmd += ['/sha1', thumb]
    else:
        cmd += ['/f', pfx]
        password = os.environ.get('SEEDFINDER_SIGN_PFX_PASSWORD')
        if password:
            cmd += ['/p', password]
    cmd.append(exe_path)
    print(f'Signing {exe_path}...')
    if subprocess.run(cmd).returncode != 0:
        print('ERROR: signtool sign failed!', file=sys.stderr)
        sys.exit(1)
    if subprocess.run([signtool, 'verify', '/pa', exe_path]).returncode != 0:
        print('ERROR: signature verification failed!', file=sys.stderr)
        sys.exit(1)


def main():
    if not os.path.isfile(DLL_PATH):
        print(f'ERROR: DLL not found at {DLL_PATH}', file=sys.stderr)
        print('Build it first with: server\\start.bat', file=sys.stderr)
        sys.exit(1)

    # Clean previous build
    if os.path.isdir(DIST_DIR):
        shutil.rmtree(DIST_DIR)
    if os.path.isdir(SPEC_DIR):
        shutil.rmtree(SPEC_DIR)

    cmd = [
        sys.executable, '-m', 'PyInstaller',
        '--onefile',
        '--name', 'SeedFinder',
        '--distpath', DIST_DIR,
        '--workpath', SPEC_DIR,
        '--specpath', SPEC_DIR,
        '--add-binary', f'{DLL_PATH};.',
        '--hidden-import', 'flask',
        '--hidden-import', 'flask_cors',
        '--noupx',  # Avoid UPX compression issues with DLLs
        '--console',  # Keep console window visible for logs
        f'--icon={os.path.join(SERVER_DIR, "logo", "logo.ico")}',
        INDEX_FILE,
    ]
    # Bundle the logo icon: extracted to _MEIPASS/ at runtime.
    cmd += [
        '--add-data',
        f'{os.path.join(SERVER_DIR, "logo")}{os.pathsep}logo',
    ]

    print('Building SeedFinder.exe with PyInstaller...')
    print(' '.join(cmd))
    print()

    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        print('\nERROR: PyInstaller build failed!', file=sys.stderr)
        sys.exit(1)

    exe_path = os.path.join(DIST_DIR, 'SeedFinder.exe')
    _sign_exe(exe_path)
    size_mb = os.path.getsize(exe_path) / (1024 * 1024)
    print(f'\nBuild successful!')
    print(f'  Output: {exe_path}')
    print(f'  Size:   {size_mb:.1f} MB')
    print(f'\nUsers can now run SeedFinder.exe directly — no Python needed.')


if __name__ == '__main__':
    main()
