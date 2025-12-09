# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec file for Windows builds
# NO hiddenimports - let Analysis find everything from viz.py imports
# Just ensure viz_components dir is in the path

import sys
import os

# Add current directory to path so PyInstaller can find viz_components
sys.path.insert(0, os.path.abspath('.'))

a = Analysis(
    ['viz.py'],
    pathex=[os.path.abspath('.')],  # Add viz dir to Python path
    binaries=[],
    datas=[
        ('stream_config.py', '.'),
        ('stream_config.yaml', '.'),
        ('../decoder/decodelog.py', '.'),
        ('../decoder/conversions.py', '.'),
    ],
    hiddenimports=[],  # Let PyInstaller discover everything from viz.py
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='DataVisualizer',
    debug=True,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,  # Keep console for now to see errors
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
