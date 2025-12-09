# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['tools/logtools/viz/viz.py'],
    pathex=['tools/logtools/viz'],
    binaries=[],
    datas=[('tools/logtools/viz/viz_components', 'viz_components'), ('tools/logtools/viz/stream_config.py', '.'), ('tools/logtools/viz/stream_config.yaml', '.'), ('tools/logtools/decoder/decodelog.py', '.'), ('tools/logtools/decoder/conversions.py', '.')],
    hiddenimports=[],
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
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
