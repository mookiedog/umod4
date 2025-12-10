from PyInstaller.utils.hooks import collect_submodules, collect_data_files

hiddenimports = collect_submodules('viz_components')
datas = collect_data_files('viz_components')
