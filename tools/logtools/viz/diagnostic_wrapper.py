"""Diagnostic wrapper to check module availability at runtime"""
import sys
import os
import importlib.util

def check_bundled_modules():
    """Verify critical modules are accessible"""
    print("=" * 70)
    print("DIAGNOSTIC INFORMATION")
    print("=" * 70)
    print(f"Python version: {sys.version}")
    print(f"Python executable: {sys.executable}")
    print(f"sys.frozen: {getattr(sys, 'frozen', False)}")

    if getattr(sys, 'frozen', False):
        print(f"Running as frozen app")
        if hasattr(sys, '_MEIPASS'):
            print(f"_MEIPASS: {sys._MEIPASS}")
            print(f"Contents of _MEIPASS:")
            try:
                for item in sorted(os.listdir(sys._MEIPASS)):
                    item_path = os.path.join(sys._MEIPASS, item)
                    if os.path.isdir(item_path):
                        print(f"  [DIR]  {item}/")
                    else:
                        size = os.path.getsize(item_path)
                        print(f"  [FILE] {item} ({size} bytes)")
            except Exception as e:
                print(f"  Error listing _MEIPASS: {e}")

    print(f"\nsys.path:")
    for i, path in enumerate(sys.path):
        print(f"  [{i}] {path}")

    print(f"\n__file__: {__file__}")
    print(f"Current working directory: {os.getcwd()}")

    # Critical modules to check
    critical_modules = [
        'decodelog',
        'conversions',
        'stream_config',
        'viz_components',
        'viz_components.config',
        'viz_components.widgets',
        'viz_components.utils',
        'viz_components.rendering',
        'viz_components.data',
        'viz_components.navigation',
    ]

    print("\n" + "=" * 70)
    print("MODULE AVAILABILITY CHECK")
    print("=" * 70)

    missing = []
    for mod in critical_modules:
        try:
            spec = importlib.util.find_spec(mod)
            if spec:
                print(f"✓ {mod:40s} -> {spec.origin}")
            else:
                print(f"✗ {mod:40s} -> NOT FOUND (spec is None)")
                missing.append(mod)
        except (ImportError, ModuleNotFoundError) as e:
            print(f"✗ {mod:40s} -> ERROR: {e}")
            missing.append(mod)

    print("=" * 70)
    if missing:
        print(f"MISSING MODULES: {len(missing)}")
        for mod in missing:
            print(f"  - {mod}")
        print("=" * 70)
        return False
    else:
        print("ALL MODULES FOUND")
        print("=" * 70)
        return True

if __name__ == '__main__':
    success = check_bundled_modules()
    sys.exit(0 if success else 1)
