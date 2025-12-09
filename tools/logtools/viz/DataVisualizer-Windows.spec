# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec file for Windows builds
# This explicitly includes viz_components as a package

# Manually list EVERY viz_components module to ensure they're included
hiddenimports = [
    'viz_components',
    'viz_components.config',
    'viz_components.config.app_config',
    'viz_components.config.unit_converter',
    'viz_components.config.per_file_settings',
    'viz_components.data',
    'viz_components.data.data_manager',
    'viz_components.data.hdf5_loader',
    'viz_components.navigation',
    'viz_components.navigation.view_controller',
    'viz_components.navigation.view_history',
    'viz_components.rendering',
    'viz_components.rendering.decimation',
    'viz_components.rendering.normalization',
    'viz_components.ui',
    'viz_components.utils',
    'viz_components.utils.color_utils',
    'viz_components.widgets',
    'viz_components.widgets.color_checkbox',
    'viz_components.widgets.draggable_list',
    'viz_components.widgets.resizable_splitter',
    'viz_components.widgets.stream_checkbox',
    'viz_components.widgets.zoomable_graph',
]

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
