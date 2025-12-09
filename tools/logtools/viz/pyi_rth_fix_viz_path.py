import sys
import os

# When running as PyInstaller bundle, add bundle dir to path
if getattr(sys, 'frozen', False):
    bundle_dir = sys._MEIPASS
    if bundle_dir not in sys.path:
        sys.path.insert(0, bundle_dir)
