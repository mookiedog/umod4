#!/usr/bin/env python3
"""
Bundle ES6 modules into a single HTML file for distribution.

This allows the modular viewer to work with file:// protocol on phones/tablets.

Usage:
    python3 bundle.py

Output:
    viewer_bundle.html - Single self-contained file
"""

import re
from pathlib import Path

def read_file(path):
    """Read file contents as text."""
    return Path(path).read_text(encoding='utf-8')

def resolve_imports(js_code, base_dir):
    """
    Recursively resolve all ES6 imports and inline the code.

    Handles:
    - import { x, y } from './module.js'
    - import * as name from './module.js'
    - import defaultExport from './module.js'
    """
    import_pattern = r'import\s+(?:{[^}]+}|\*\s+as\s+\w+|\w+)\s+from\s+[\'"]([^\'"]+)[\'"];?'

    processed_modules = set()
    result = []

    def process_module(code, current_dir):
        lines = []
        for line in code.split('\n'):
            match = re.search(import_pattern, line)
            if match:
                module_path = match.group(1)
                # Resolve relative path
                if module_path.startswith('./'):
                    module_path = module_path[2:]

                full_path = current_dir / module_path

                # Avoid circular imports
                if str(full_path) not in processed_modules:
                    processed_modules.add(str(full_path))

                    # Read and process the imported module
                    module_code = read_file(full_path)
                    module_dir = full_path.parent

                    # Recursively process imports in this module
                    processed_code = process_module(module_code, module_dir)

                    # Remove export statements from module (not needed in bundled code)
                    processed_code = re.sub(r'\bexport\s+', '', processed_code)

                    lines.append(f'// ===== Bundled: {module_path} =====')
                    lines.append(processed_code)
                    lines.append(f'// ===== End: {module_path} =====\n')
            else:
                lines.append(line)

        return '\n'.join(lines)

    return process_module(js_code, base_dir)

def bundle_viewer():
    """Bundle the modular viewer into a single HTML file."""

    base_dir = Path(__file__).parent
    index_html = read_file(base_dir / 'index.html')

    # Extract the module script
    module_script_pattern = r'<script type="module">(.*?)</script>'
    match = re.search(module_script_pattern, index_html, re.DOTALL)

    if not match:
        print("Error: Could not find <script type='module'> in index.html")
        return False

    module_script = match.group(1)

    # Resolve all imports and inline the code
    print("Resolving ES6 imports...")
    bundled_script = resolve_imports(module_script, base_dir)

    # Replace module script with bundled version (no type="module")
    # IMPORTANT: re.sub() interprets backslash escapes in the replacement string,
    # so we must escape all backslashes in bundled_script to preserve them literally
    escaped_script = bundled_script.replace('\\', '\\\\')
    replacement = '<script>\n' + escaped_script + '\n    </script>'
    bundled_html = re.sub(
        module_script_pattern,
        replacement,
        index_html,
        flags=re.DOTALL
    )

    # Update title to indicate bundled version
    bundled_html = bundled_html.replace(
        'v2.1 Virtual Scrolling',
        'v2.1 Bundled (for phones/tablets)'
    )
    bundled_html = bundled_html.replace(
        '<!-- Version 2.1 - Virtual scrolling with timestamp fix -->',
        '<!-- Version 2.1 - Bundled with virtual scrolling -->'
    )

    # Write output
    output_path = base_dir / 'viewer_bundle.html'
    output_path.write_text(bundled_html, encoding='utf-8')

    print(f"✅ Bundle created: {output_path}")
    print(f"   Size: {output_path.stat().st_size:,} bytes")
    print(f"   This file works with file:// protocol on phones/tablets")

    return True

if __name__ == '__main__':
    import sys
    success = bundle_viewer()
    sys.exit(0 if success else 1)
