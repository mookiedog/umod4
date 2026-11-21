# Stream Configuration Guide

## Overview

The visualizer uses a YAML file to define how each data stream is displayed.
You can change colors, display ranges and other settings without modifying any code.

Most day-to-day customization is done inside the visualizer itself, not in this file.
For example, right-click a stream's checkbox to pick a different color, and the visualizer remembers your choice.
It also remembers your stream order, units and display modes.
The YAML file supplies the defaults, and it is the only way to change settings the visualizer has no menu for, such as a stream's display range or the initial view window.
The examples below show those cases.

## Configuration Files

### Base Configuration

* **Location**: `tools/logtools/viz/stream_config.yaml`
* **Purpose**: the default configuration that ships with the application
* **Scope**: defines all the available streams and their default settings

### Local Overrides

* **Location**: `stream_config.yaml` in the directory the visualizer is launched from (the "current directory")
* **Purpose**: your own customizations
* **Scope**: overrides specific settings from the base configuration

Which directory is "current" depends on how you start the visualizer:

* From the release zip, `run_viz.cmd` and `run_viz.sh` first change to the folder they live in.
  Put your `stream_config.yaml` in that folder, next to the launcher.
* From a repository checkout with `uv run tools/logtools/viz/viz.py <file.h5>`, it is whatever directory your terminal is in.

## How Local Overrides Work

When you run the visualizer, it:

1. Loads the base configuration.
2. Checks whether `stream_config.yaml` exists in the current directory.
3. If it does, **deep merges** the local file into the base configuration.

Only the values you specify in the local file change, so it only needs to contain the settings you want to change.

## Deep Merge Behavior

* **Dictionaries** are merged recursively: nested keys are combined.
* **Strings, numbers and booleans** are replaced by the override value.
* **Lists** are replaced entirely by the override value.

## Examples

### Example 1: Change a Stream Color

**Base configuration** (excerpt):

```yaml
streams:
  ecu_air_temp_c:
    type: graph_data
    display_name: "Air Temperature (C)"
    default_color: "#005500"
    units: celsius
    owns_axis: true
    normalize: true
    display_range_min: 0.0
    display_range_max: 120.0
```

**Local override**:

```yaml
streams:
  ecu_air_temp_c:
    default_color: "#00FF00"  # Just change the color to green
```

**Result**: air temperature is drawn in green, and all its other settings are unchanged.

### Example 2: Adjust a Display Range

**Local override**:

```yaml
streams:
  ecu_air_temp_c:
    display_range_min: 10.0   # Change min from 0 to 10
    display_range_max: 100.0  # Change max from 120 to 100
```

**Result**: the air temperature axis now shows 10 to 100 degrees C instead of 0 to 120.

### Example 3: Change Several Streams

**Local override**:

```yaml
streams:
  ecu_air_temp_c:
    default_color: "#00FF00"

  ecu_coolant_temp_c:
    default_color: "#0000FF"

  gps_velocity_mph:
    display_range_min_top: 60.0  # Change from 30 mph to 60 mph
```

### Example 4: Change Global Settings

**Local override**:

```yaml
settings:
  data_normalize_max: 0.90  # Use 90% of the window height instead of 85%

  nav_offset_before: 2.0    # Show 2 seconds before the anchor (instead of 1.0)
  nav_offset_after: 8.0     # Show 8 seconds after the anchor (instead of 5.0)
```

## Tips

1. **Start small**: only override what you need.
2. **Check the syntax**: YAML is whitespace-sensitive, so use 2 spaces for indentation.
3. **Several setups**: keep a different `stream_config.yaml` in each directory you launch from, for example one for track days and one for street rides.
4. **Debugging**: the visualizer prints which config files it loads when it starts.

## Validation

The visualizer validates the merged configuration (base plus local overrides) on startup and prints any problems to stdout, with the stream name:

* A stream with an unknown field gets a warning, and the unknown field is ignored.
* A stream that fails validation is skipped, with an error.
* A local file that is not valid YAML is ignored, with a warning.
