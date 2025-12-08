# Test Data

This directory contains sample log files for testing the decoder and visualizer.

## Purpose

- Test `decodelog.py` conversion from binary logs to HDF5
- Provide sample data for `viz.py` testing and demonstrations
- Serve as example logs for users learning the system

## File Format

- `.log` or `.um4` files are raw binary logs from the umod4 hardware
- These files can be converted to HDF5 format using `decodelog.py`:
  ```bash
  python ../decoder/decodelog.py sample.log --format hdf5 -o sample.h5
  ```

## Adding Sample Logs

When adding sample logs to this directory:

1. Keep files small (under 10 MB if possible)
2. Name files descriptively (e.g., `idle.log`, `acceleration.log`, `cruise.log`)
3. Document what each log contains (engine condition, duration, etc.)

## Sample Log Descriptions

(Add descriptions of sample logs here as they are added)
