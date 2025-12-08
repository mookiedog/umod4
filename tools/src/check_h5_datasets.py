#!/usr/bin/env python3
"""
Quick script to list all datasets in an HDF5 file and their shapes.
Usage: python3 check_h5_datasets.py <file.h5>
"""

import sys
import h5py

if len(sys.argv) < 2:
    print("Usage: python3 check_h5_datasets.py <file.h5>")
    sys.exit(1)

filename = sys.argv[1]

print(f"Checking HDF5 file: {filename}\n")

with h5py.File(filename, 'r') as h5file:
    print("Datasets:")
    print("-" * 60)

    # Sort datasets by name for easier reading
    dataset_names = sorted(h5file.keys())

    for name in dataset_names:
        ds = h5file[name]
        if hasattr(ds, 'shape'):
            shape = ds.shape
            dtype = ds.dtype
            print(f"  {name:40s} {str(shape):15s} {dtype}")
        else:
            print(f"  {name:40s} (group)")

    print("-" * 60)
    print(f"\nTotal datasets: {len(dataset_names)}")

    # Check specifically for coil bar datasets
    coil_bars = [name for name in dataset_names if 'coil_bar' in name]
    if coil_bars:
        print(f"\nCoil bar datasets found: {len(coil_bars)}")
        for name in coil_bars:
            ds = h5file[name]
            print(f"  {name}: {ds.shape[0]} samples")
    else:
        print("\nNo coil bar datasets found!")
