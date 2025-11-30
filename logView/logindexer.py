#!/usr/bin/env python3
"""
Log Indexer for umod4 Streaming Server

Builds a fast random-access index of log events by scanning the binary file
and recording the file offset of each event. This allows the streaming server
to quickly seek to and parse any range of events without loading the entire file.
"""

import sys
import os
from pathlib import Path

class LogIndexer:
    """Indexes a binary log file for fast random access."""

    def __init__(self, filepath, logsyms_path=None):
        """
        Initialize indexer with log file path.

        Args:
            filepath: Path to binary log file
            logsyms_path: Optional path to Logsyms directory
        """
        self.filepath = filepath
        self.file = None
        self.event_index = []  # List of (event_num, file_offset) for ALL events
        self.record_index = []  # List of (record_num, event_num) for displayable records only
        self.total_events = 0
        self.total_records = 0
        self.file_size = 0

        # Load Logsyms
        if logsyms_path:
            sys.path.insert(0, logsyms_path)
        else:
            # Default: use umod4 venv
            script_dir = Path(__file__).parent.absolute()
            venv_path = os.path.join(script_dir, "..", "build", ".venv")
            site_packages = os.path.join(venv_path, "lib",
                                        f"python{sys.version_info.major}.{sys.version_info.minor}",
                                        "site-packages")
            sys.path.insert(0, site_packages)

        import Logsyms as ls
        self.L = ls.Logsyms

    def open(self):
        """Open log file and build index."""
        print(f"Indexing {self.filepath}...")

        self.file = open(self.filepath, 'rb')
        self.file_size = os.path.getsize(self.filepath)

        # Build index
        self._build_index()

        print(f"✅ Indexed {self.total_events:,} events, {self.total_records:,} records ({self.file_size:,} bytes)")

    def _build_index(self):
        """
        Scan file and build index with 1:1 event-to-record mapping.

        Every event produces exactly one record. String characters are displayed
        as individual records (one char per line), not accumulated.

        Builds:
        - event_index: maps event_num -> file_offset
        - record_index: maps record_num -> event_num (always record_num == event_num)
        """
        self.event_index = []
        self.record_index = []
        event_num = 0

        while True:
            offset = self.file.tell()

            # Read event ID byte
            b = self.file.read(1)
            if len(b) < 1:
                break

            # Record this event's offset and create corresponding record
            self.event_index.append((event_num, offset))
            self.record_index.append((event_num, event_num))  # 1:1 mapping

            event_id = b[0]

            # Skip event data based on type
            data_len = self._get_event_data_length(event_id)
            if data_len > 0:
                self.file.read(data_len)

            event_num += 1

        self.total_events = event_num
        self.total_records = event_num  # Always equal now

    def _get_event_data_length(self, event_id):
        """
        Return the data length (in bytes) for a given event ID.

        Dynamically builds the map from Logsyms by matching TYPE and DLEN pairs.
        """
        # Build event map once on first call
        if not hasattr(self, '_event_length_cache'):
            self._event_length_cache = {}
            L = self.L

            # Get all attributes from Logsyms
            for attr in dir(L):
                if '_TYPE_' in attr and not attr.endswith('_DLEN'):
                    # This is a type constant - find corresponding DLEN
                    # E.g. LOGID_ECU_MAP_TYPE_U8 → LOGID_ECU_MAP_DLEN
                    dlen_name = attr.replace('_TYPE_', '_DLEN').split('_TYPE')[0] + '_DLEN'

                    if hasattr(L, dlen_name):
                        type_val = getattr(L, attr)
                        dlen_val = getattr(L, dlen_name)
                        self._event_length_cache[type_val] = dlen_val

        return self._event_length_cache.get(event_id, 0)

    def get_event_offset(self, event_num):
        """Get file offset for a specific event number."""
        if event_num < 0 or event_num >= len(self.event_index):
            return None
        return self.event_index[event_num][1]

    def get_record_event_range(self, start_record, end_record):
        """
        Get the event range needed to decode records [start_record, end_record).

        Returns (start_event, end_event) tuple, or None if invalid.
        """
        if start_record < 0 or start_record >= self.total_records:
            return None

        end_record = min(end_record, self.total_records)
        if start_record >= end_record:
            return None

        # Get event number for first record
        start_event = self.record_index[start_record][1]

        # Get event number for last record (end is exclusive, so we want the event AFTER the last record)
        if end_record >= self.total_records:
            end_event = self.total_events
        else:
            end_event = self.record_index[end_record][1]

        return (start_event, end_event)

    def get_total_events(self):
        """Return total number of events in log."""
        return self.total_events

    def get_total_records(self):
        """Return total number of displayable records in log."""
        return self.total_records

    def close(self):
        """Close log file."""
        if self.file:
            self.file.close()
            self.file = None


if __name__ == '__main__':
    # Test indexer
    if len(sys.argv) < 2:
        print("Usage: python3 logindexer.py <logfile>")
        sys.exit(1)

    indexer = LogIndexer(sys.argv[1])
    indexer.open()

    print(f"\nEvent index sample (first 10 events):")
    for i in range(min(10, len(indexer.event_index))):
        event_num, offset = indexer.event_index[i]
        print(f"  Event {event_num}: offset {offset}")

    print(f"\nRecord index sample (first 10 records):")
    for i in range(min(10, len(indexer.record_index))):
        record_num, event_num = indexer.record_index[i]
        print(f"  Record {record_num} -> Event {event_num}")

    indexer.close()
