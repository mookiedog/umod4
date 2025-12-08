# Stream Configuration Guide

## Overview

The visualizer uses a YAML-based configuration system to define how data streams are displayed. This allows you to customize colors, display ranges, and other visualization parameters without modifying code.

## Configuration Files

### Base Configuration
- **Location**: `tools/src/stream_config.yaml`
- **Purpose**: Default configuration shipped with the application
- **Scope**: Defines all available streams and their default settings

### Local Overrides
- **Location**: `./stream_config.yaml` (in your current working directory)
- **Purpose**: Project-specific customizations
- **Scope**: Overrides specific settings from the base config

## How Local Overrides Work

When you run the visualizer, it:
1. Loads the base configuration from `tools/src/stream_config.yaml`
2. Checks if `./stream_config.yaml` exists in your current directory
3. If found, **deep merges** the local config into the base config
4. Only the values you specify in the local config are changed

This means you only need to include the specific settings you want to change in your local config file.

## Deep Merge Behavior

The merge is **recursive** for nested dictionaries:
- **Dictionaries**: Merged recursively (nested keys are combined)
- **Primitives** (strings, numbers, booleans): Replaced with override value
- **Lists**: Replaced entirely with override value

## Examples

### Example 1: Change Stream Color

**Base config** (`tools/src/stream_config.yaml`):
```yaml
streams:
  ecu_air_temp_c:
    type: graph_data
    display_name: "Air Temperature"
    units: celsius
    default_color: "#FF0000"
    display_range_min: 0.0
    display_range_max: 120.0
```

**Local override** (`./stream_config.yaml`):
```yaml
streams:
  ecu_air_temp_c:
    default_color: "#00FF00"  # Just change the color to green
```

**Result**: Air temperature displays in green, all other settings remain unchanged.

### Example 2: Adjust Display Range

**Local override** (`./stream_config.yaml`):
```yaml
streams:
  ecu_air_temp_c:
    display_range_min: 10.0   # Change min from 0 to 10
    display_range_max: 100.0  # Change max from 120 to 100
```

**Result**: Air temperature axis now shows 10-100°C instead of 0-120°C.

### Example 3: Multiple Stream Changes

**Local override** (`./stream_config.yaml`):
```yaml
streams:
  ecu_air_temp_c:
    default_color: "#00FF00"

  ecu_coolant_temp_c:
    default_color: "#0000FF"

  gps_velocity_mph:
    display_range_min_top: 60.0  # Change from 30 mph to 60 mph
```

### Example 4: Override Global Settings

**Local override** (`./stream_config.yaml`):
```yaml
settings:
  data_normalize_max: 0.90  # Use 90% of window height instead of 85%

  nav_offset_before: 2.0    # Show 2 seconds before anchor (instead of 1.0)
  nav_offset_after: 8.0     # Show 8 seconds after anchor (instead of 5.0)
```

### Example 5: Add New Stream (Advanced)

You can even define entirely new streams in your local config:

**Local override** (`./stream_config.yaml`):
```yaml
streams:
  my_custom_stream:
    type: graph_data
    display_name: "Custom Data"
    units: custom_units
    owns_axis: true
    normalize: true
    default_color: "#FFAA00"
```

## Use Cases

### Per-Project Customization
Create different local configs for different data collection scenarios:
```bash
~/projects/bike_data/track_day/stream_config.yaml     # Track-specific settings
~/projects/bike_data/street_ride/stream_config.yaml  # Street-specific settings
```

### Team Collaboration
- Base config stays in version control (git)
- Local overrides can be `.gitignore`'d for personal preferences
- Or committed for team-wide project settings

### Temporary Experiments
Test different display ranges or colors without modifying the main config:
```bash
cd ~/temp_analysis
# Create stream_config.yaml with experimental settings
viz some_data.h5
# Settings only apply in this directory
```

## Configuration Priority

From highest to lowest priority:
1. Local override (`./stream_config.yaml`)
2. Base configuration (`tools/src/stream_config.yaml`)

## Tips

1. **Start small**: Only override what you need
2. **Check syntax**: YAML is whitespace-sensitive (use 2 spaces for indentation)
3. **Version control**: Consider committing project-specific local configs
4. **Debugging**: The visualizer prints which config files it loads on startup

## Validation

The visualizer validates both the base and merged configuration on startup. Any errors will be reported to stdout with specific stream names and error messages.
