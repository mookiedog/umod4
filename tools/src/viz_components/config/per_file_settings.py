"""
Per-file settings management using JSON sidecar files.

Provides:
- Load/save per-file visualizer state to .viz JSON files
- Validation of loaded settings against current HDF5 file
- Migration/upgrade of older schema versions
"""

import json
import os
from pathlib import Path
from typing import Optional, Dict, List, Any
from dataclasses import dataclass, asdict, field


@dataclass
class PerFileSettings:
    """Container for per-file visualizer settings."""

    # Schema version for future compatibility
    version: int = 1

    # File identification (for validation)
    logfile: str = ""

    # Stream configuration
    enabled_streams: List[str] = field(default_factory=list)
    stream_colors: Dict[str, str] = field(default_factory=dict)
    stream_display_modes: Dict[str, str] = field(default_factory=dict)
    stream_order: List[str] = field(default_factory=list)

    # Axis assignments
    axis_owner: Optional[str] = None
    right_axis_owner: Optional[str] = None

    # View state
    view_start: Optional[float] = None
    view_end: Optional[float] = None

    # Navigation history
    view_history: Optional[Dict[str, Any]] = None


class PerFileSettingsManager:
    """Manages loading/saving of per-file settings."""

    SCHEMA_VERSION = 1

    def __init__(self, debug: bool = False):
        """
        Initialize per-file settings manager.

        Args:
            debug: Enable debug output
        """
        self.debug = debug
        self.current_settings: Optional[PerFileSettings] = None
        self.current_viz_path: Optional[str] = None
        self.has_unsaved_changes: bool = False

    def get_viz_path(self, logfile_path: str) -> str:
        """
        Get the .viz sidecar file path for a given log file.

        Args:
            logfile_path: Path to .h5 or .hdf5 log file

        Returns:
            Path to .viz file (same directory, same basename + .viz)
        """
        path = Path(logfile_path)

        # Handle both .h5 and .um4 files
        # For .um4 files, we want the .viz for the corresponding .h5 file
        if path.suffix == '.um4':
            base_path = path.with_suffix('')
            return str(base_path) + '.viz'
        else:
            # For .h5/.hdf5 files
            return str(path.with_suffix('')) + '.viz'

    def load(self, logfile_path: str) -> Optional[PerFileSettings]:
        """
        Load per-file settings from .viz file.

        Args:
            logfile_path: Path to the HDF5 log file

        Returns:
            PerFileSettings object if file exists and is valid, None otherwise
        """
        viz_path = self.get_viz_path(logfile_path)
        self.current_viz_path = viz_path

        if not os.path.exists(viz_path):
            self._debug_print(f"No .viz file found at {viz_path}")
            self.current_settings = None
            self.has_unsaved_changes = False
            return None

        try:
            with open(viz_path, 'r') as f:
                data = json.load(f)

            # Validate schema version
            version = data.get('version', 1)
            if version > self.SCHEMA_VERSION:
                print(f"WARNING: .viz file has newer schema version {version}, "
                      f"current version is {self.SCHEMA_VERSION}. Some settings may be ignored.")

            # Migrate if necessary
            if version < self.SCHEMA_VERSION:
                data = self._migrate_schema(data, version)

            # Create settings object
            settings = PerFileSettings(
                version=data.get('version', 1),
                logfile=data.get('logfile', ''),
                enabled_streams=data.get('enabled_streams', []),
                stream_colors=data.get('stream_colors', {}),
                stream_display_modes=data.get('stream_display_modes', {}),
                stream_order=data.get('stream_order', []),
                axis_owner=data.get('axis_owner'),
                right_axis_owner=data.get('right_axis_owner'),
                view_start=data.get('view_start'),
                view_end=data.get('view_end'),
                view_history=data.get('view_history')
            )

            self.current_settings = settings
            self.has_unsaved_changes = False
            self._debug_print(f"Loaded .viz file from {viz_path}")
            return settings

        except json.JSONDecodeError as e:
            print(f"ERROR: Failed to parse .viz file {viz_path}: {e}")
            print(f"       Using default settings instead.")
            self.current_settings = None
            self.has_unsaved_changes = False
            return None
        except Exception as e:
            print(f"ERROR: Failed to load .viz file {viz_path}: {e}")
            self.current_settings = None
            self.has_unsaved_changes = False
            return None

    def save(self, logfile_path: str, settings: PerFileSettings) -> bool:
        """
        Save per-file settings to .viz file.

        Args:
            logfile_path: Path to the HDF5 log file
            settings: Settings to save

        Returns:
            True if save succeeded, False otherwise
        """
        viz_path = self.get_viz_path(logfile_path)

        try:
            # Ensure directory exists
            os.makedirs(os.path.dirname(viz_path) or '.', exist_ok=True)

            # Convert to dict and save as JSON
            data = asdict(settings)
            data['version'] = self.SCHEMA_VERSION
            data['logfile'] = os.path.basename(logfile_path)

            with open(viz_path, 'w') as f:
                json.dump(data, f, indent=2)

            self.has_unsaved_changes = False
            self._debug_print(f"Saved .viz file to {viz_path}")
            return True

        except IOError as e:
            print(f"ERROR: Failed to save .viz file {viz_path}: {e}")
            print(f"       Settings will not be persisted.")
            return False
        except Exception as e:
            print(f"ERROR: Unexpected error saving .viz file {viz_path}: {e}")
            return False

    def validate_against_file(self, settings: PerFileSettings,
                             available_streams: List[str]) -> PerFileSettings:
        """
        Validate settings against the currently loaded HDF5 file.
        Filters out streams that don't exist in the file.

        Args:
            settings: Settings to validate
            available_streams: List of streams actually present in HDF5 file

        Returns:
            Validated settings with invalid streams removed
        """
        stream_set = set(available_streams)

        # Filter enabled streams
        settings.enabled_streams = [s for s in settings.enabled_streams
                                    if s in stream_set]

        # Filter stream colors
        settings.stream_colors = {k: v for k, v in settings.stream_colors.items()
                                  if k in stream_set}

        # Filter stream display modes
        settings.stream_display_modes = {k: v for k, v in settings.stream_display_modes.items()
                                         if k in stream_set}

        # Filter stream order
        settings.stream_order = [s for s in settings.stream_order
                                if s in stream_set]

        # Validate axis owners
        if settings.axis_owner and settings.axis_owner not in stream_set:
            self._debug_print(f"WARNING: axis_owner '{settings.axis_owner}' not in file, clearing")
            settings.axis_owner = None

        if settings.right_axis_owner and settings.right_axis_owner not in stream_set:
            self._debug_print(f"WARNING: right_axis_owner '{settings.right_axis_owner}' not in file, clearing")
            settings.right_axis_owner = None

        return settings

    def validate_view_bounds(self, settings: PerFileSettings,
                           time_min: float, time_max: float) -> PerFileSettings:
        """
        Validate and clamp view bounds to data time range.

        Args:
            settings: Settings to validate
            time_min: Minimum time in dataset
            time_max: Maximum time in dataset

        Returns:
            Settings with clamped view bounds
        """
        if settings.view_start is not None:
            settings.view_start = max(time_min, min(time_max, settings.view_start))

        if settings.view_end is not None:
            settings.view_end = max(time_min, min(time_max, settings.view_end))

        # Ensure start < end
        if (settings.view_start is not None and settings.view_end is not None
            and settings.view_start >= settings.view_end):
            self._debug_print(f"WARNING: view_start >= view_end, resetting to None")
            settings.view_start = None
            settings.view_end = None

        # Validate view history bounds
        if settings.view_history:
            history = settings.view_history.get('history', [])
            validated_history = []
            for entry in history:
                if len(entry) >= 2:
                    start = max(time_min, min(time_max, entry[0]))
                    end = max(time_min, min(time_max, entry[1]))
                    if start < end:
                        validated_entry = [start, end] + entry[2:]
                        validated_history.append(validated_entry)
            settings.view_history['history'] = validated_history

            # Validate current_index
            current_idx = settings.view_history.get('current_index', 0)
            if current_idx >= len(validated_history):
                settings.view_history['current_index'] = max(0, len(validated_history) - 1)

        return settings

    def capture_current_state(self, app_state: Dict[str, Any]) -> PerFileSettings:
        """
        Capture current application state as PerFileSettings.

        Args:
            app_state: Dictionary containing current application state
                Required keys:
                - 'enabled_streams': List[str]
                - 'stream_colors': Dict[str, str]
                - 'stream_display_modes': Dict[str, str]
                - 'stream_order': List[str]
                - 'axis_owner': Optional[str]
                - 'right_axis_owner': Optional[str]
                - 'view_start': float
                - 'view_end': float
                - 'view_history': ViewHistory object or None

        Returns:
            PerFileSettings with captured state
        """
        settings = PerFileSettings(
            version=self.SCHEMA_VERSION,
            enabled_streams=app_state.get('enabled_streams', []).copy(),
            stream_colors=app_state.get('stream_colors', {}).copy(),
            stream_display_modes=app_state.get('stream_display_modes', {}).copy(),
            stream_order=app_state.get('stream_order', []).copy(),
            axis_owner=app_state.get('axis_owner'),
            right_axis_owner=app_state.get('right_axis_owner'),
            view_start=app_state.get('view_start'),
            view_end=app_state.get('view_end')
        )

        # Capture view history if present
        view_history_obj = app_state.get('view_history')
        if view_history_obj:
            settings.view_history = {
                'history': [list(entry) for entry in view_history_obj.history],
                'current_index': view_history_obj.history_index
            }
            self._debug_print(f"Captured view history: {len(view_history_obj.history)} entries, index={view_history_obj.history_index}")
        else:
            self._debug_print("No view history object to capture")

        return settings

    def mark_modified(self):
        """Mark settings as modified (needs save)."""
        self.has_unsaved_changes = True

    def _migrate_schema(self, data: Dict[str, Any], from_version: int) -> Dict[str, Any]:
        """
        Migrate settings from older schema version.

        Args:
            data: Settings data to migrate
            from_version: Current version of data

        Returns:
            Migrated data
        """
        # Future: Add migration logic when schema changes
        # For now, v1 is the only version
        return data

    def _debug_print(self, *args, **kwargs):
        """Print debug message if debug flag is enabled."""
        if self.debug:
            print(*args, **kwargs)
