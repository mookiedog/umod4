#!/home/robin/projects/umod4/build/.venv/bin/python3
"""
umod4 Log Streaming Server

A local HTTP server that streams log chunks on-demand to the browser viewer.
Handles unlimited file sizes with constant memory usage.

Usage:
    ./logserver.py <logfile>
    # Or:
    /home/robin/projects/umod4/build/.venv/bin/python3 logserver.py <logfile>

Then open browser to: http://localhost:5000
"""

import sys
import os
import json
import argparse
import webbrowser
from pathlib import Path

# Ensure we're using the right Python (from venv)
venv_python = "/home/robin/projects/umod4/build/.venv/bin/python3"
if sys.executable != venv_python and os.path.exists(venv_python):
    # Re-exec with venv Python
    os.execv(venv_python, [venv_python] + sys.argv)

from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS

# Import the decoder infrastructure
sys.path.insert(0, str(Path(__file__).parent.parent / 'tools' / 'src'))
from logindexer import LogIndexer

# Import comprehensive chunk decoder from local directory
sys.path.insert(0, str(Path(__file__).parent))
from logchunkdecoder import LogChunkDecoder

app = Flask(__name__, static_folder='.')
CORS(app)  # Enable CORS for local development

# Global state
current_log = None
current_filepath = None


# ================================================================================================
# Flask Routes
# ================================================================================================

@app.route('/')
def index():
    """Serve the streaming viewer HTML page."""
    return send_from_directory('.', 'index_streaming.html')

@app.route('/js/<path:filename>')
def serve_js(filename):
    """Serve JavaScript files."""
    return send_from_directory('js', filename)

@app.route('/api/info')
def get_info():
    """Return information about the currently loaded log."""
    if current_log is None:
        return jsonify({'error': 'No log file loaded'}), 404

    return jsonify({
        'filename': os.path.basename(current_filepath),
        'total_events': current_log.get_total_records(),  # Return total RECORDS for display
        'file_size': current_log.file_size
    })

@app.route('/api/events')
def get_events():
    """
    Return a chunk of decoded records.

    Query parameters:
        start: First record index (inclusive)
        end: Last record index (exclusive)

    Returns:
        JSON with 'records' array and 'total' count
    """
    if current_log is None:
        return jsonify({'error': 'No log file loaded'}), 404

    # Parse query parameters (these are RECORD numbers which are now 1:1 with events)
    start_record = int(request.args.get('start', 0))
    end_record = int(request.args.get('end', 100))

    # With 1:1 mapping, records ARE events
    start_event = start_record
    end_event = end_record

    # Create decoder for this chunk
    decoder = LogChunkDecoder(current_log.file, current_log)

    # Decode events to produce records
    # Decoder handles mid-string detection automatically
    records = decoder.decode_chunk(start_event, end_event)

    print(f"API /events: requested records {start_record}-{end_record}, decoded {len(records)} records", flush=True)

    return jsonify({
        'records': records,
        'total': current_log.get_total_records()
    })

@app.route('/api/search')
def search():
    """
    Search for events matching a text query.

    Query parameters:
        q: Search query string

    Returns:
        JSON with 'matches' array of event indices
    """
    if current_log is None:
        return jsonify({'error': 'No log file loaded'}), 404

    query = request.args.get('q', '').lower()
    if not query:
        return jsonify({'matches': []})

    # For now, return empty - full implementation would stream through file
    # and test each decoded event's text content
    # This is a Phase 2 feature
    return jsonify({'matches': []})


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description='umod4 Log Streaming Server')
    parser.add_argument('logfile', help='Binary log file to serve')
    parser.add_argument('--port', type=int, default=5000, help='Port to listen on (default: 5000)')
    parser.add_argument('--no-browser', action='store_true', help='Do not automatically open browser')

    args = parser.parse_args()

    # Validate log file exists
    if not os.path.exists(args.logfile):
        print(f"Error: Log file not found: {args.logfile}")
        return 1

    # Load and index log file
    global current_log, current_filepath
    current_filepath = args.logfile

    try:
        current_log = LogIndexer(args.logfile)
        current_log.open()
    except Exception as e:
        print(f"Error indexing log file: {e}")
        return 1

    # Start server
    url = f"http://localhost:{args.port}"
    print(f"\n🏍️  umod4 Log Streaming Server")
    print(f"   File: {os.path.basename(args.logfile)}")
    print(f"   Events: {current_log.get_total_events():,}")
    print(f"   Records: {current_log.get_total_records():,}")
    print(f"   URL: {url}\n")

    # Open browser
    if not args.no_browser:
        webbrowser.open(url)

    # Run Flask server
    app.run(host='localhost', port=args.port, debug=False)

    return 0


if __name__ == '__main__':
    sys.exit(main())
