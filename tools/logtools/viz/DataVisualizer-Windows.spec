# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec file for Windows builds
# This explicitly includes viz_components as a package

from PyInstaller.utils.hooks import collect_submodules

# Collect all viz_components submodules
hiddenimports = collect_submodules('viz_components')

a = Analysis(
    ['viz.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('viz_components', 'viz_components'),
        ('stream_config.py', '.'),
        ('stream_config.yaml', '.'),
        ('../decoder/decodelog.py', '.'),
        ('../decoder/conversions.py', '.'),
    ],
    hiddenimports=hiddenimports,
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
