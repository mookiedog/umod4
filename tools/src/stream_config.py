"""Stream configuration system for visualization tool

This module provides a configuration-driven approach to defining how different
data streams should be visualized. Stream types include:
- graph_data: Regular time-series plots
- graph_marker: Event markers attached to graph data
- bar_data: Horizontal bars with start/duration
- time_marker: Markers on the time axis itself
"""

import yaml
from pathlib import Path
from enum import Enum
from typing import Dict, Any, Optional
from dataclasses import dataclass, field


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
        """Load configuration from YAML file"""
        try:
            with open(self.config_path, 'r') as f:
                config = yaml.safe_load(f)
        except FileNotFoundError:
            print(f"WARNING: Config file not found: {self.config_path}")
            print("Using empty configuration")
            return
        except yaml.YAMLError as e:
            print(f"ERROR: Failed to parse YAML config: {e}")
            print("Using empty configuration")
            return

        # Load global settings
        self.settings = config.get('settings', {})

        # Load stream configurations
        streams_config = config.get('streams', {})
        for name, stream_data in streams_config.items():
            try:
                self.streams[name] = StreamConfig(
                    name=name,
                    **stream_data
                )
            except Exception as e:
                print(f"WARNING: Failed to load config for stream '{name}': {e}")
                continue

        print(f"Loaded configuration for {len(self.streams)} streams from {self.config_path}")

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
