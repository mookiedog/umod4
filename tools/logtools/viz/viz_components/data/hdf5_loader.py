"""
HDF5 data loader for visualization tool.

Handles loading and parsing HDF5 log files, including:
- Reading datasets with different shapes (2D, 3D)
- Extracting metadata
- Applying unit conversions based on user preferences
"""

import os
import numpy as np

try:
    import h5py
    HDF5_AVAILABLE = True
except ImportError:
    HDF5_AVAILABLE = False
    h5py = None

from ..config import UnitConverter


class HDF5DataLoader:
    """Handles loading and parsing HDF5 log files"""

    def __init__(self, stream_config_manager):
        """
        Initialize the HDF5 data loader.

        Args:
            stream_config_manager: StreamConfigManager instance for stream configuration
        """
        self.stream_config = stream_config_manager

    def load_file(self, filepath, app_config):
        """
        Load an HDF5 file and extract all datasets.

        Args:
            filepath: Path to HDF5 file
            app_config: AppConfig instance for user preferences (units, etc.)

        Returns:
            Dictionary with keys:
                - 'raw_data': Dict mapping stream names to {'time': array, 'values': array}
                - 'stream_names': List of stream names (excluding hidden streams)
                - 'stream_ranges': Dict mapping stream names to (min, max) tuples
                - 'stream_metadata': Dict mapping stream names to metadata dicts
                - 'file_metadata': Dict of HDF5 file attributes
                - 'time_bounds': Tuple of (time_min, time_max)
        """
        if not HDF5_AVAILABLE:
            raise ImportError("h5py not available - cannot load HDF5 files")

        if not os.path.exists(filepath):
            raise FileNotFoundError(f"File not found: {filepath}")

        print(f"Loading HDF5 file: {filepath}")

        with h5py.File(filepath, 'r') as h5file:
            # Read file metadata
            file_metadata = {}
            print("Metadata:")
            for key, value in h5file.attrs.items():
                file_metadata[key] = value
                print(f"  {key}: {value}")

            # Load all datasets
            raw_data = {}
            stream_names = []
            stream_metadata = {}

            # Get all dataset names
            for key in h5file.keys():
                if key == 'eprom_loads':  # Skip special datasets
                    continue

                ds = h5file[key]

                # Handle different dataset shapes
                if len(ds.shape) == 2 and ds.shape[1] == 2:
                    # Standard (time_ns, value) format
                    if ds.shape[0] > 0:  # Only include non-empty datasets
                        print(f"  Loading {key}: {ds.shape[0]} samples")

                        # Detect native units from dataset name
                        data_type, native_units = UnitConverter.parse_units_from_name(key)

                        # Get user's preferred display units
                        display_units = native_units  # Default to native
                        if data_type == 'temperature':
                            display_units = app_config.get_temperature_units()
                        elif data_type == 'velocity':
                            display_units = app_config.get_velocity_units()
                        elif data_type == 'pressure':
                            display_units = app_config.get_pressure_units()

                        # Store metadata for this stream
                        stream_metadata[key] = {
                            'data_type': data_type,
                            'native_units': native_units,
                            'display_units': display_units
                        }

                        # Load and convert data
                        time_data = ds[:, 0] / 1e9  # Convert ns to seconds
                        value_data = ds[:, 1]

                        # Apply unit conversion if needed
                        if data_type == 'temperature' and native_units != display_units:
                            value_data = UnitConverter.convert_temperature(value_data, native_units, display_units)
                            print(f"    Converted from {native_units} to {display_units}")
                        elif data_type == 'velocity' and native_units != display_units:
                            value_data = UnitConverter.convert_velocity(value_data, native_units, display_units)
                            print(f"    Converted from {native_units} to {display_units}")
                        elif data_type == 'pressure' and native_units != display_units:
                            value_data = UnitConverter.convert_pressure(value_data, native_units, display_units)
                            print(f"    Converted from {native_units} to {display_units}")

                        # Store converted data
                        raw_data[key] = {
                            'time': time_data,
                            'values': value_data
                        }

                        # Don't add hidden streams to stream_names
                        if not self.stream_config.should_skip_in_selection(key):
                            stream_names.append(key)

                elif len(ds.shape) == 2 and ds.shape[1] == 3:
                    # 3D data like gps_position (time_ns, lat, lon)
                    if ds.shape[0] > 0:
                        print(f"  Loading {key}: {ds.shape[0]} samples (3D)")
                        time_ns = ds[:, 0] / 1e9
                        # Keep GPS position as a single entity (lat/lon cannot be separated)
                        if key == 'gps_position':
                            raw_data['gps_position'] = {
                                'time': time_ns,
                                'lat': ds[:, 1],
                                'lon': ds[:, 2]
                            }
                            # Note: gps_position is NOT added to stream_names
                            # It will be displayed as markers, not as a plottable stream

                elif len(ds.shape) == 1:
                    # 1D timestamp arrays (markers) - skip for now
                    if ds.shape[0] > 0:
                        print(f"  Skipping 1D marker dataset {key}: {ds.shape[0]} samples")

            print(f"\nTotal streams loaded: {len(stream_names)}")

            # Calculate ranges for each stream
            stream_ranges = {}
            all_times = []

            for stream in stream_names:
                stream_min = float(raw_data[stream]['values'].min())
                stream_max = float(raw_data[stream]['values'].max())
                # Add epsilon to avoid division by zero for constant streams
                if stream_max - stream_min < 1e-10:
                    stream_max = stream_min + 1.0
                stream_ranges[stream] = (stream_min, stream_max)
                print(f"  {stream}: range [{stream_min:.2f}, {stream_max:.2f}]")

                # Collect all time values to find overall bounds
                all_times.extend([raw_data[stream]['time'].min(), raw_data[stream]['time'].max()])

            # Set time bounds from raw data
            time_min = float(min(all_times)) if all_times else 0.0
            time_max = float(max(all_times)) if all_times else 0.0

            print(f"Time range: [{time_min:.2f}s, {time_max:.2f}s], span: {time_max - time_min:.2f}s")

            return {
                'raw_data': raw_data,
                'stream_names': stream_names,
                'stream_ranges': stream_ranges,
                'stream_metadata': stream_metadata,
                'file_metadata': file_metadata,
                'time_bounds': (time_min, time_max)
            }

    def get_metadata(self, filepath):
        """
        Get metadata from an HDF5 file without loading all data.

        Args:
            filepath: Path to HDF5 file

        Returns:
            Dictionary containing file attributes and dataset info
        """
        if not HDF5_AVAILABLE:
            raise ImportError("h5py not available")

        if not os.path.exists(filepath):
            raise FileNotFoundError(f"File not found: {filepath}")

        metadata = {
            'filepath': filepath,
            'filesize_mb': os.path.getsize(filepath) / (1024 * 1024),
            'attributes': {},
            'datasets': {},
            'eprom_loads': []
        }

        with h5py.File(filepath, 'r') as f:
            # Get file attributes
            for key, value in f.attrs.items():
                metadata['attributes'][key] = value

            # Get dataset information
            for key in f.keys():
                if key == 'eprom_loads':
                    # Parse EPROM loads
                    eprom_loads = f['eprom_loads'][:]
                    for load in eprom_loads:
                        name = load['name'].decode('utf-8').rstrip('\x00')
                        metadata['eprom_loads'].append({
                            'name': name,
                            'address': int(load['address']),
                            'length': int(load['length']),
                            'error_status': int(load['error_status'])
                        })
                else:
                    ds = f[key]
                    metadata['datasets'][key] = {
                        'shape': ds.shape,
                        'dtype': str(ds.dtype),
                        'size': ds.shape[0] if len(ds.shape) > 0 else 0
                    }

        return metadata
