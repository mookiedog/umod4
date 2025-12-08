"""Stream configuration system for visualization tool

This module provides a configuration-driven approach to defining how different
data streams should be visualized. Stream types include:
- graph_data: Regular time-series plots
- graph_marker: Event markers attached to graph data
- bar_data: Horizontal bars with start/duration
- time_marker: Markers on the time axis itself

Configuration cascading:
- Base config: tools/src/stream_config.yaml (shipped with application)
- Local override: ./stream_config.yaml (in current directory)
- Local overrides are deep-merged with base config
"""

import yaml
import os
from pathlib import Path
from enum import Enum
from typing import Dict, Any, Optional
from dataclasses import dataclass, field


def deep_merge(base: dict, override: dict) -> dict:
    """
    Deep merge override dictionary into base dictionary.

    Recursively merges nested dictionaries. Lists and primitive values are replaced.

    Args:
        base: Base dictionary
        override: Override dictionary (takes precedence)

    Returns:
        Merged dictionary (modifies base in-place and returns it)
    """
    for key, value in override.items():
        if key in base and isinstance(base[key], dict) and isinstance(value, dict):
            # Recursively merge nested dicts
            deep_merge(base[key], value)
        else:
            # Replace value (works for primitives, lists, or new keys)
            base[key] = value
    return base


class StreamType(Enum):
    """Types of visualization streams"""
    GRAPH_DATA = "graph_data"
    GRAPH_MARKER = "graph_marker"
    BAR_DATA = "bar_data"
    TIME_MARKER = "time_marker"


@dataclass
class StreamConfig:
    """Configuration for a single stream"""
    name: str
    type: StreamType
    display_name: str
    default_color: str = "#FFFFFF"
    hidden: bool = False  # Hide from stream selection UI

    # Graph data specific
    units: Optional[str] = None
    owns_axis: bool = False
    normalize: bool = False

    # Display range constraints (graph data)
    display_range_min: Optional[float] = None  # Fixed minimum for display range
    display_range_max: Optional[float] = None  # Fixed maximum for display range
    display_range_min_top: Optional[float] = None  # Minimum value for top of display (prevents over-scaling)
    display_range_max_bottom: Optional[float] = None  # Maximum value for bottom of display (prevents under-scaling)

    # Graph marker specific
    attach_to: Optional[str] = None
    offset: float = 0.0
    line_height: float = 0.0
    label: Optional[str] = None
    label_format: Optional[str] = None
    max_visible: Optional[int] = None

    # Bar data specific
    y_position: float = 0.0
    height: float = 0.0
    alpha: float = 1.0
    tooltip: Optional[str] = None
    duration_stream: Optional[str] = None  # For injector-style bars (legacy format)
    off_stream: Optional[str] = None  # For coil-style bars (legacy format)
    combined_format: bool = False  # True if using combined [time, duration] format
    legacy_format: bool = False  # True if this is a fallback for old HDF5 files
    duration_units: Optional[str] = None  # "ticks" (2μs) or "nanoseconds"

    # Time marker specific
    shape: Optional[str] = None
    size: int = 10

    def __post_init__(self):
        if isinstance(self.type, str):
            self.type = StreamType(self.type)


class StreamConfigManager:
    """Manages stream configuration from YAML file"""

    def __init__(self, config_path: Optional[Path] = None):
        """
        Initialize the configuration manager.

        Args:
            config_path: Path to YAML config file. If None, looks for stream_config.yaml
                        in the same directory as this module.
        """
        if config_path is None:
            config_path = Path(__file__).parent / "stream_config.yaml"

        self.config_path = config_path
        self.streams: Dict[str, StreamConfig] = {}
        self.settings: Dict[str, Any] = {}
        self._load_config()

    def _load_config(self):
        """Load configuration from YAML file with local overrides"""
        # Load base configuration
        try:
            with open(self.config_path, 'r') as f:
                config = yaml.safe_load(f)
        except FileNotFoundError:
            print(f"ERROR: Config file not found: {self.config_path}")
            print("Using empty configuration")
            return
        except yaml.YAMLError as e:
            print(f"ERROR: Failed to parse YAML config: {e}")
            print("Using empty configuration")
            return

        # Check for local override in current directory
        local_config_path = Path.cwd() / "stream_config.yaml"
        if local_config_path.exists() and local_config_path != self.config_path:
            try:
                with open(local_config_path, 'r') as f:
                    local_config = yaml.safe_load(f)

                if local_config:
                    print(f"Loading local config overrides from: {local_config_path}")
                    # Deep merge local config into base config
                    config = deep_merge(config, local_config)
                    print(f"  Applied overrides from {local_config_path}")
            except yaml.YAMLError as e:
                print(f"WARNING: Failed to parse local config {local_config_path}: {e}")
                print("  Ignoring local overrides")
            except Exception as e:
                print(f"WARNING: Error loading local config {local_config_path}: {e}")
                print("  Ignoring local overrides")

        # Load global settings
        self.settings = config.get('settings', {})

        # Load stream configurations
        streams_config = config.get('streams', {})
        errors = []
        warnings = []

        for name, stream_data in streams_config.items():
            try:
                stream_config = StreamConfig(
                    name=name,
                    **stream_data
                )

                # Validate display constraints
                validation_errors = self._validate_stream_config(stream_config)
                if validation_errors:
                    errors.extend([f"Stream '{name}': {err}" for err in validation_errors])
                else:
                    self.streams[name] = stream_config

            except TypeError as e:
                warnings.append(f"Stream '{name}': Unknown or invalid field in YAML: {e}")
                # Still try to create the stream without the invalid fields
                try:
                    # Filter out invalid fields by only using known fields
                    valid_data = {k: v for k, v in stream_data.items()
                                 if k in StreamConfig.__dataclass_fields__}
                    stream_config = StreamConfig(name=name, **valid_data)
                    self.streams[name] = stream_config
                except Exception as e2:
                    errors.append(f"Stream '{name}': Failed to load even after filtering: {e2}")
            except Exception as e:
                errors.append(f"Stream '{name}': {e}")
                continue

        # Print validation results
        if errors:
            print(f"\nERROR: Found {len(errors)} error(s) in stream configuration:")
            for error in errors:
                print(f"  - {error}")
            print()

        if warnings:
            print(f"\nWARNING: Found {len(warnings)} warning(s) in stream configuration:")
            for warning in warnings:
                print(f"  - {warning}")
            print()

        print(f"Loaded configuration for {len(self.streams)} streams from {self.config_path}")

    def _validate_stream_config(self, config: StreamConfig) -> list:
        """
        Validate a stream configuration for common errors.

        Args:
            config: StreamConfig to validate

        Returns:
            List of error messages (empty if valid)
        """
        errors = []

        # Validate display range constraints
        if config.display_range_min is not None and config.display_range_max is not None:
            if config.display_range_min >= config.display_range_max:
                errors.append(
                    f"display_range_min ({config.display_range_min}) must be less than "
                    f"display_range_max ({config.display_range_max})"
                )

            # Fixed range should not be combined with other constraints
            if config.display_range_min_top is not None:
                errors.append(
                    "display_range_min_top should not be used with fixed range "
                    "(display_range_min/max). Use only fixed range or dynamic constraints, not both."
                )
            if config.display_range_max_bottom is not None:
                errors.append(
                    "display_range_max_bottom should not be used with fixed range "
                    "(display_range_min/max). Use only fixed range or dynamic constraints, not both."
                )

        # Validate min_top and max_bottom constraints
        if config.display_range_min_top is not None and config.display_range_max_bottom is not None:
            if config.display_range_max_bottom >= config.display_range_min_top:
                errors.append(
                    f"display_range_max_bottom ({config.display_range_max_bottom}) must be less than "
                    f"display_range_min_top ({config.display_range_min_top})"
                )

        # Validate graph markers
        if config.type == StreamType.GRAPH_MARKER:
            if not config.attach_to:
                errors.append("Graph markers must specify 'attach_to' stream")

        # Validate bar data
        if config.type == StreamType.BAR_DATA:
            if config.legacy_format and not config.off_stream:
                errors.append("Legacy format bars must specify 'off_stream'")
            if not config.legacy_format and not config.combined_format:
                errors.append("Bars must be either legacy_format or combined_format")

        return errors

    def get_stream(self, name: str) -> Optional[StreamConfig]:
        """
        Get configuration for a stream.

        Args:
            name: Stream name

        Returns:
            StreamConfig object or None if not found
        """
        return self.streams.get(name)

    def get_streams_by_type(self, stream_type: StreamType) -> Dict[str, StreamConfig]:
        """
        Get all streams of a given type.

        Args:
            stream_type: StreamType enum value

        Returns:
            Dictionary mapping stream names to StreamConfig objects
        """
        return {
            name: config
            for name, config in self.streams.items()
            if config.type == stream_type
        }

    def is_event_only_stream(self, name: str) -> bool:
        """
        Check if stream should not be plotted as regular time-series data.

        Event-only streams include graph markers, bar data, and time markers.
        These have custom visualization and should be skipped in normal plotting loops.

        Args:
            name: Stream name

        Returns:
            True if stream is event-only (has custom visualization)
        """
        config = self.get_stream(name)
        if not config:
            return False
        return config.type in (
            StreamType.GRAPH_MARKER,
            StreamType.BAR_DATA,
            StreamType.TIME_MARKER
        )

    def should_skip_in_selection(self, name: str) -> bool:
        """
        Check if stream should be hidden from the stream selection UI.

        Duration streams and other helper streams that are auto-loaded with their
        parent stream should be hidden.

        Args:
            name: Stream name

        Returns:
            True if stream should be hidden from UI
        """
        # Check if stream is explicitly marked as hidden
        config = self.get_stream(name)
        if config and config.hidden:
            return True

        # Check if this stream is referenced as a duration/off stream
        for config in self.streams.values():
            if config.duration_stream == name or config.off_stream == name:
                return True
        return False

    def get_setting(self, key: str, default=None):
        """
        Get a global setting value.

        Args:
            key: Setting key (can use dot notation for nested keys)
            default: Default value if key not found

        Returns:
            Setting value or default
        """
        # Support dot notation for nested keys (e.g., 'performance.max_bars')
        keys = key.split('.')
        value = self.settings

        for k in keys:
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                return default

        return value

    def get_all_event_only_streams(self) -> set:
        """
        Get set of all stream names that are event-only (not regular time-series).

        This is useful for creating skip lists in plotting code.

        Returns:
            Set of stream names
        """
        return {
            name for name, config in self.streams.items()
            if config.type in (StreamType.GRAPH_MARKER, StreamType.BAR_DATA, StreamType.TIME_MARKER)
        }

    def reload(self):
        """Reload configuration from file (useful for development/testing)"""
        self.streams = {}
        self.settings = {}
        self._load_config()


# Singleton instance for convenience
_config_manager = None


def get_config_manager() -> StreamConfigManager:
    """
    Get the global StreamConfigManager instance.

    Returns:
        StreamConfigManager singleton
    """
    global _config_manager
    if _config_manager is None:
        _config_manager = StreamConfigManager()
    return _config_manager
