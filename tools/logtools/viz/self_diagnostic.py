"""
Self-diagnostic module for the visualizer
Can be imported and run at startup to verify all dependencies
"""
import sys
import os
import importlib.util
from typing import List, Tuple, Optional


class DiagnosticResult:
    """Results from a diagnostic check"""
    def __init__(self):
        self.python_version = sys.version
        self.executable = sys.executable
        self.frozen = getattr(sys, 'frozen', False)
        self.meipass = getattr(sys, '_MEIPASS', None) if self.frozen else None
        self.missing_modules: List[str] = []
        self.found_modules: List[Tuple[str, str]] = []
        self.warnings: List[str] = []

    @property
    def success(self) -> bool:
        """Check passed if no missing modules"""
        return len(self.missing_modules) == 0

    def generate_report(self) -> str:
        """Generate a human-readable report"""
        lines = []
        lines.append("=" * 70)
        lines.append("VISUALIZER SELF-DIAGNOSTIC REPORT")
        lines.append("=" * 70)
        lines.append(f"Python: {self.python_version.split()[0]}")
        lines.append(f"Executable: {self.executable}")
        lines.append(f"Frozen: {self.frozen}")
        if self.frozen and self.meipass:
            lines.append(f"Bundle dir: {self.meipass}")
        lines.append("")

        if self.success:
            lines.append("STATUS: ✓ ALL CHECKS PASSED")
            lines.append(f"Found {len(self.found_modules)} required modules")
        else:
            lines.append("STATUS: ✗ PROBLEMS DETECTED")
            lines.append(f"Missing {len(self.missing_modules)} required modules:")
            for mod in self.missing_modules:
                lines.append(f"  - {mod}")

        if self.warnings:
            lines.append("")
            lines.append(f"WARNINGS ({len(self.warnings)}):")
            for warning in self.warnings:
                lines.append(f"  - {warning}")

        if not self.success:
            lines.append("")
            lines.append("TROUBLESHOOTING:")
            lines.append("1. If running from source: Check that all files are present")
            lines.append("2. If running frozen app: This is a packaging bug")
            lines.append("3. Please report this issue with this diagnostic output")

        lines.append("=" * 70)
        return "\n".join(lines)


def check_module(module_name: str) -> Tuple[bool, Optional[str]]:
    """
    Check if a module can be imported
    Returns (success, location)
    """
    try:
        spec = importlib.util.find_spec(module_name)
        if spec and spec.origin:
            return (True, spec.origin)
        else:
            return (False, None)
    except (ImportError, ModuleNotFoundError, ValueError) as e:
        return (False, str(e))


def run_diagnostic(verbose: bool = False) -> DiagnosticResult:
    """
    Run comprehensive diagnostic checks

    Args:
        verbose: If True, print progress during checks

    Returns:
        DiagnosticResult object with findings
    """
    result = DiagnosticResult()

    # Define all modules that must be present
    required_modules = [
        # Standard library (should always work)
        ('sys', 'critical'),
        ('os', 'critical'),
        ('argparse', 'critical'),

        # Third-party dependencies
        ('PyQt6', 'critical'),
        ('pyqtgraph', 'critical'),
        ('numpy', 'critical'),
        ('pandas', 'critical'),
        ('h5py', 'critical'),

        # Local modules - decoder
        ('decodelog', 'critical'),
        ('conversions', 'critical'),

        # Local modules - viz
        ('stream_config', 'critical'),

        # Local modules - viz_components
        ('viz_components', 'critical'),
        ('viz_components.config', 'critical'),
        ('viz_components.widgets', 'critical'),
        ('viz_components.utils', 'critical'),
        ('viz_components.rendering', 'critical'),
        ('viz_components.data', 'critical'),
        ('viz_components.navigation', 'critical'),
    ]

    if verbose:
        print(f"Checking {len(required_modules)} required modules...")

    for module_name, importance in required_modules:
        success, location = check_module(module_name)

        if success:
            result.found_modules.append((module_name, location))
            if verbose:
                print(f"  ✓ {module_name}")
        else:
            if importance == 'critical':
                result.missing_modules.append(module_name)
                if verbose:
                    print(f"  ✗ {module_name} - {location}")
            else:
                result.warnings.append(f"Optional module '{module_name}' not found")
                if verbose:
                    print(f"  ⚠ {module_name} (optional)")

    # Additional checks
    if result.frozen:
        # Check if bundle directory exists and is readable
        if result.meipass:
            if not os.path.isdir(result.meipass):
                result.warnings.append(f"Bundle directory not accessible: {result.meipass}")
            elif verbose:
                try:
                    items = os.listdir(result.meipass)
                    print(f"\nBundle contains {len(items)} items")
                except Exception as e:
                    result.warnings.append(f"Cannot list bundle directory: {e}")

    return result


def check_and_report(exit_on_failure: bool = False, verbose: bool = False) -> bool:
    """
    Convenience function to run diagnostic and print report

    Args:
        exit_on_failure: If True, sys.exit(1) if checks fail
        verbose: If True, print detailed progress

    Returns:
        True if all checks passed, False otherwise
    """
    result = run_diagnostic(verbose=verbose)

    print(result.generate_report())

    if not result.success and exit_on_failure:
        sys.exit(1)

    return result.success


def quick_check() -> bool:
    """
    Quick silent check - returns True if OK, False if problems
    Useful for conditional startup behavior
    """
    result = run_diagnostic(verbose=False)
    return result.success


if __name__ == '__main__':
    # When run as a script, do full diagnostic with verbose output
    import argparse

    parser = argparse.ArgumentParser(description='Run visualizer self-diagnostic')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Show detailed progress')
    parser.add_argument('--no-exit', action='store_true',
                       help='Do not exit with error code on failure')

    args = parser.parse_args()

    success = check_and_report(exit_on_failure=not args.no_exit,
                               verbose=args.verbose)

    if success:
        print("\nDiagnostic check completed successfully!")
        sys.exit(0)
    else:
        print("\nDiagnostic check FAILED - see report above")
        sys.exit(0 if args.no_exit else 1)
