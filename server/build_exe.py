"""Build SeedFinder.exe — a single-file executable with embedded Python + DLL.

Usage:
    cd SeedFinder
    python server/build_exe.py

Output:
    server/dist/SeedFinder.exe
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SERVER_DIR = os.path.join(ROOT, 'server')
DLL_PATH = os.path.join(ROOT, 'build_server', 'seedfinder_lib.dll')
DIST_DIR = os.path.join(SERVER_DIR, 'dist')
SPEC_DIR = os.path.join(SERVER_DIR, 'build_spec')

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
        '--icon=../logo/logo.ico',
        os.path.join(SERVER_DIR, 'seedfinder_server.py'),
    ]

    print('Building SeedFinder.exe with PyInstaller...')
    print(' '.join(cmd))
    print()

    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        print('\nERROR: PyInstaller build failed!', file=sys.stderr)
        sys.exit(1)

    exe_path = os.path.join(DIST_DIR, 'SeedFinder.exe')
    size_mb = os.path.getsize(exe_path) / (1024 * 1024)
    print(f'\nBuild successful!')
    print(f'  Output: {exe_path}')
    print(f'  Size:   {size_mb:.1f} MB')
    print(f'\nUsers can now run SeedFinder.exe directly — no Python needed.')


if __name__ == '__main__':
    main()
