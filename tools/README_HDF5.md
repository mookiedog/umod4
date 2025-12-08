# HDF5 Log Conversion and Verification

This directory contains tools for converting binary umod4 logs to HDF5 format and verifying the output.

## Converting Binary Logs to HDF5

Use `decodelog.py` with the `--format hdf5` option:

```bash
# Using the project's Python virtual environment
build/.venv/bin/python3 tools/logtools/decoder/decodelog.py <input_log_file> --format hdf5 -o output.h5

# Example:
build/.venv/bin/python3 tools/logtools/decoder/decodelog.py ~/logs/log.17 --format hdf5 -o log17.h5
```

**Note:** The `--output` (or `-o`) parameter is required when using HDF5 format.

## Verifying HDF5 Output

After creating an HDF5 file, verify its contents using `verify_hdf5.py`:

```bash
# Using the project's Python virtual environment
build/.venv/bin/python3 tools/logtools/decoder/verify_hdf5.py <hdf5_file>

# Example:
build/.venv/bin/python3 tools/logtools/decoder/verify_hdf5.py log17.h5
```

The verification script will:
- Display all metadata attributes (log versions, GPS-calculated start time, etc.)
- List EPROM loads with their status
- Show statistics for all datasets (sample count, value ranges, time ranges)
- Check time monotonicity (ensures timestamps always increase)
- Display sample data from key datasets
- Provide a summary of verification results

## HDF5 File Structure

The HDF5 file contains:

### Metadata (Root Attributes)
- `log_version_ecu`, `log_version_ep`, `log_version_wp` - Log format versions
- `log_start_timestamp_utc` - Absolute UTC timestamp of log start (back-calculated from GPS)
- `log_start_timestamp_iso` - Human-readable ISO format timestamp
- `gps_sync_elapsed_ns` - Nanoseconds elapsed when GPS first synchronized

### EPROM Loads Dataset
- `eprom_loads` - Compound dataset with name, address, length, error_status for each EPROM load

### Time-Series Datasets
All datasets use a unified nanosecond time axis from log start. Most are 2D arrays: `[time_ns, value]`

**Engine Timing:**
- `ecu_crankref_timestamp`, `ecu_crankref_id`
- `ecu_camshaft_timestamp`, `ecu_cam_error`
- `ecu_rpm_instantaneous`, `ecu_rpm_smoothed`
- `ecu_time_marker` - Time marker events (emitted when crank is not turning)

**Fuel Injection:**
- `ecu_front_inj_on`, `ecu_front_inj_duration`
- `ecu_rear_inj_on`, `ecu_rear_inj_duration`

**Ignition:**
- `ecu_front_coil_on`, `ecu_front_coil_off`, `ecu_front_ign_delay`
- `ecu_front_coil_manual_on`, `ecu_front_coil_manual_off`
- `ecu_rear_coil_on`, `ecu_rear_coil_off`, `ecu_rear_ign_delay`
- `ecu_rear_coil_manual_on`, `ecu_rear_coil_manual_off`
- `ecu_spark_x1`, `ecu_spark_x2`, `ecu_nospark`

**Sensors:**
- `ecu_throttle_adc`, `ecu_map_adc`, `ecu_aap_adc`
- `ecu_air_temp_c`, `ecu_coolant_temp_c`, `ecu_battery_voltage_v`

**Errors:**
- `ecu_error_L000C`, `ecu_error_L000D`, `ecu_error_L000E`, `ecu_error_L000F`

**Markers (1D timestamp arrays):**
- `ecu_marker_5ms`, `ecu_marker_p6_max`

**Miscellaneous:**
- `ecu_cpu_event`, `ecu_l4000_event`, `ecu_portg_debug`, `ecu_fuel_pump`

**GPS (3-column for position):**
- `gps_position` - `[time_ns, latitude, longitude]`
- `gps_velocity_mph`, `gps_fix_type`

**Filesystem Performance:**
- `wp_fs_write_time_ms`, `wp_fs_sync_time_ms`

## Performance Features

- **Chunking:** Data is stored in ~1 second chunks (1000 samples) for efficient time-range queries
- **Compression:** Gzip level 4 compression reduces file size
- **Resizable datasets:** Can grow as more data is appended
- **Converted values:** Temperatures in °C, voltages in V, speeds in MPH (not raw ADC values)

## Time Tracking

All events share a unified nanosecond counter:
- Timestamped events advance time by `delta_ticks × 2000ns` (2μs per tick)
- Untimestamped events advance by 1ns (preserves sequence)
- 16-bit timestamp wraparound is handled automatically
- GPS data is used to back-calculate absolute UTC start time
