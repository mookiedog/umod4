"""Flask HTTP server for umod4 device communication."""

from flask import Flask, request, jsonify, send_file
from werkzeug.serving import make_server
from datetime import datetime
import threading
import os
import json
import zlib
from models.database import Database, Device, Transfer, Connection


class Umod4Server:
    """HTTP server for receiving log files and serving firmware updates."""

    def __init__(self, database, port=8080, host='0.0.0.0'):
        """Initialize HTTP server.

        Args:
            database: Database instance
            port: Server port (default: 8080)
            host: Server host (default: 0.0.0.0 for all interfaces)
        """
        self.database = database
        self.port = port
        self.host = host
        self.app = Flask(__name__)
        self.server = None
        self.server_thread = None
        self.running = False

        # Callbacks for GUI updates
        self.on_device_registered = None
        self.on_transfer_started = None
        self.on_transfer_completed = None
        self.on_connection_event = None

        # Session cleanup task
        self.cleanup_thread = None
        self.cleanup_running = False

        self._setup_routes()

    def _setup_routes(self):
        """Set up Flask routes."""

        @self.app.route('/api/device/register', methods=['POST'])
        def register_device():
            """Device registration/heartbeat endpoint."""
            try:
                data = request.json
                mac_address = data.get('mac_address')

                if not mac_address:
                    return jsonify({'error': 'mac_address required'}), 400

                session = self.database.get_session()
                try:
                    # Get or create device within the same session
                    device = session.query(Device).filter_by(mac_address=mac_address).first()
                    is_new = False

                    if not device:
                        # Create new device
                        if os.name == 'nt':
                            base_path = os.path.join(os.environ.get('APPDATA', ''), 'umod4_server', 'logs')
                        else:
                            base_path = os.path.expanduser('~/.umod4_server/logs')

                        mac_clean = mac_address.replace(':', '-')
                        log_path = os.path.join(base_path, mac_clean)

                        device = Device(
                            mac_address=mac_address,
                            name=None,  # New devices start with blank name
                            display_name=mac_address,  # Display MAC until named
                            log_storage_path=log_path,
                            first_seen=datetime.utcnow(),
                            last_seen=datetime.utcnow()
                        )
                        session.add(device)
                        is_new = True

                        # Create log storage directory
                        os.makedirs(log_path, exist_ok=True)

                    # Update device info
                    if 'wp_version' in data:
                        device.wp_version = data['wp_version']
                    if 'ep_version' in data:
                        device.ep_version = data['ep_version']
                    device.last_seen = datetime.utcnow()

                    session.commit()

                    # Get values before closing session
                    device_mac = device.mac_address
                    device_name = device.display_name

                    # Notify GUI
                    if self.on_device_registered and is_new:
                        self.on_device_registered(device)

                    # Record connection event
                    ip_address = data.get('ip_address', request.remote_addr)
                    connection = self.database.add_connection(device_mac, ip_address)

                    if self.on_connection_event:
                        self.on_connection_event(connection)

                    # Return device configuration
                    response = {
                        'device_id': device_mac,
                        'display_name': device_name,
                        'log_upload_path': f'/logs/upload/{device_mac}',
                        'firmware_check_url': f'/firmware/latest/{device_mac}'
                    }

                    return jsonify(response), 200

                finally:
                    session.close()

            except Exception as e:
                return jsonify({'error': str(e)}), 500

        @self.app.route('/logs/list/<device_mac>', methods=['GET'])
        def list_logs(device_mac):
            """List uploaded log files for a device."""
            try:
                session = self.database.get_session()
                try:
                    device = session.query(Device).filter_by(mac_address=device_mac).first()
                    if not device:
                        return jsonify({'error': 'Device not found'}), 404

                    log_path = device.log_storage_path
                    if not os.path.exists(log_path):
                        return jsonify([]), 200

                    # List all .um4 files
                    files = [f for f in os.listdir(log_path) if f.endswith('.um4')]
                    return jsonify(files), 200
                finally:
                    session.close()

            except Exception as e:
                return jsonify({'error': str(e)}), 500

        @self.app.route('/logs/upload/<device_mac>/chunk', methods=['POST'])
        def upload_log_chunk(device_mac):
            """Upload a single chunk of a log file with resumption support."""
            try:
                # Parse chunk metadata from headers
                filename = request.headers.get('X-Filename')
                chunk_offset = request.headers.get('X-Chunk-Offset')
                chunk_size = request.headers.get('X-Chunk-Size')
                total_size = request.headers.get('X-Total-Size')
                is_last_chunk = request.headers.get('X-Is-Last-Chunk', 'false').lower() == 'true'
                chunk_crc32 = request.headers.get('X-Chunk-CRC32')  # Hex string
                session_id = request.headers.get('X-Session-ID')  # Optional for resume

                # Validate required headers
                if not all([filename, chunk_offset, chunk_size, total_size, chunk_crc32]):
                    return jsonify({'error': 'Missing required headers'}), 400

                # Ensure .um4 extension
                if not filename.endswith('.um4'):
                    filename += '.um4'

                # Parse numeric values
                try:
                    chunk_offset = int(chunk_offset)
                    chunk_size = int(chunk_size)
                    total_size = int(total_size)
                    expected_crc32 = int(chunk_crc32, 16)  # Parse hex string
                except ValueError:
                    return jsonify({'error': 'Invalid header values'}), 400

                # Validate device exists
                session = self.database.get_session()
                try:
                    device = session.query(Device).filter_by(mac_address=device_mac).first()
                    if not device:
                        return jsonify({'error': 'Device not found'}), 404
                finally:
                    session.close()

                # Get or create upload session
                upload_session = None
                is_new_session = False

                if session_id:
                    # Resume existing session
                    upload_session = self.database.get_upload_session(session_id)
                    if not upload_session:
                        return jsonify({'error': 'Session not found'}), 404

                    # Validate session matches file
                    if upload_session.filename != filename or upload_session.total_size != total_size:
                        return jsonify({'error': 'Session mismatch'}), 400
                else:
                    # Check for existing in-progress session
                    upload_session = self.database.find_existing_upload_session(device_mac, filename)

                    if not upload_session:
                        # Create new session
                        upload_session = self.database.create_upload_session(
                            device_mac=device_mac,
                            filename=filename,
                            total_size=total_size,
                            chunk_size=chunk_size
                        )
                        is_new_session = True

                        # Create Transfer record
                        transfer = self.database.add_transfer(
                            device_mac=device_mac,
                            filename=filename,
                            size_bytes=total_size,
                            status='in_progress'
                        )

                        # Link session to transfer
                        self.database.update_upload_session(
                            upload_session.session_id,
                            transfer_id=transfer.id
                        )

                        # Notify GUI
                        if self.on_transfer_started:
                            self.on_transfer_started(transfer)

                # Validate chunk offset is at expected position
                if chunk_offset != upload_session.bytes_received:
                    # Client is sending wrong offset - tell them where we are
                    return jsonify({
                        'error': 'Offset mismatch',
                        'expected_offset': upload_session.bytes_received,
                        'session_id': upload_session.session_id
                    }), 409  # Conflict

                # Validate chunk doesn't exceed file size
                if chunk_offset + chunk_size > total_size:
                    return jsonify({'error': 'Chunk exceeds file size'}), 400

                # Validate chunk size matches session (except last chunk which may be smaller)
                if not is_last_chunk and chunk_size != upload_session.chunk_size:
                    return jsonify({'error': f'Chunk size mismatch: expected {upload_session.chunk_size}, got {chunk_size}'}), 400

                # Read chunk data
                chunk_data = request.data

                # Validate chunk size matches header
                if len(chunk_data) != chunk_size:
                    return jsonify({'error': f'Chunk size mismatch: expected {chunk_size}, got {len(chunk_data)}'}), 400

                # Verify CRC32
                actual_crc32 = zlib.crc32(chunk_data) & 0xFFFFFFFF  # Ensure unsigned 32-bit
                if actual_crc32 != expected_crc32:
                    return jsonify({
                        'error': 'CRC32 mismatch',
                        'expected': f'0x{expected_crc32:08X}',
                        'actual': f'0x{actual_crc32:08X}'
                    }), 400

                # Append chunk to partial file
                try:
                    os.makedirs(os.path.dirname(upload_session.partial_file_path), exist_ok=True)
                    with open(upload_session.partial_file_path, 'ab') as f:
                        # Seek to correct position (in case file was partially written)
                        f.seek(chunk_offset)
                        f.write(chunk_data)
                except Exception as e:
                    # Update session as failed
                    self.database.update_upload_session(
                        upload_session.session_id,
                        status='failed'
                    )
                    if upload_session.transfer_id:
                        self.database.update_transfer(
                            upload_session.transfer_id,
                            status='failed',
                            error_message=f'File write error: {str(e)}'
                        )
                    return jsonify({'error': f'File write failed: {str(e)}'}), 500

                # Update session progress
                new_bytes_received = chunk_offset + chunk_size
                self.database.update_upload_session(
                    upload_session.session_id,
                    bytes_received=new_bytes_received,
                    last_activity=datetime.utcnow()
                )

                # If this is the last chunk, finalize upload
                if is_last_chunk or new_bytes_received >= total_size:
                    # Move .part file to final destination
                    final_path = upload_session.partial_file_path.replace('.part', '')

                    try:
                        os.rename(upload_session.partial_file_path, final_path)
                    except Exception as e:
                        self.database.update_upload_session(
                            upload_session.session_id,
                            status='failed'
                        )
                        if upload_session.transfer_id:
                            self.database.update_transfer(
                                upload_session.transfer_id,
                                status='failed',
                                error_message=f'File rename failed: {str(e)}'
                            )
                        return jsonify({'error': f'File finalization failed: {str(e)}'}), 500

                    # Calculate transfer speed
                    end_time = datetime.utcnow()
                    start_time = upload_session.created_at
                    duration = (end_time - start_time).total_seconds()
                    if duration > 0:
                        speed_mbps = (total_size / (1024 * 1024)) / duration
                    else:
                        speed_mbps = 0

                    # Update session and transfer as complete
                    self.database.update_upload_session(
                        upload_session.session_id,
                        status='completed'
                    )

                    if upload_session.transfer_id:
                        self.database.update_transfer(
                            upload_session.transfer_id,
                            status='success',
                            end_time=end_time,
                            transfer_speed_mbps=speed_mbps
                        )

                        # Notify GUI
                        if self.on_transfer_completed:
                            self.on_transfer_completed(upload_session.transfer_id)

                    return jsonify({
                        'status': 'complete',
                        'session_id': upload_session.session_id,
                        'filename': filename,
                        'bytes_received': new_bytes_received,
                        'transfer_speed_mbps': round(speed_mbps, 2)
                    }), 200

                # Chunk accepted, more to come
                return jsonify({
                    'status': 'ok',
                    'session_id': upload_session.session_id,
                    'bytes_received': new_bytes_received,
                    'next_offset': new_bytes_received
                }), 200

            except Exception as e:
                import traceback
                traceback.print_exc()
                return jsonify({'error': str(e)}), 500

        @self.app.route('/logs/upload/<device_mac>/session', methods=['GET'])
        def query_upload_session(device_mac):
            """Query upload session status for resumption.

            Query params:
                filename: Name of file being uploaded
            """
            try:
                filename = request.args.get('filename')
                if not filename:
                    return jsonify({'error': 'filename parameter required'}), 400

                # Ensure .um4 extension
                if not filename.endswith('.um4'):
                    filename += '.um4'

                # Find existing session
                upload_session = self.database.find_existing_upload_session(device_mac, filename)

                if not upload_session:
                    return jsonify({'session_found': False}), 200

                return jsonify({
                    'session_found': True,
                    'session_id': upload_session.session_id,
                    'filename': upload_session.filename,
                    'total_size': upload_session.total_size,
                    'bytes_received': upload_session.bytes_received,
                    'next_offset': upload_session.bytes_received,
                    'chunk_size': upload_session.chunk_size,
                    'created_at': upload_session.created_at.isoformat(),
                    'last_activity': upload_session.last_activity.isoformat()
                }), 200

            except Exception as e:
                return jsonify({'error': str(e)}), 500

        @self.app.route('/logs/upload/<device_mac>', methods=['POST'])
        def upload_log(device_mac):
            """Upload a log file from a device."""
            transfer_id = None
            try:
                # Get filename from header or use default
                filename = request.headers.get('X-Filename', 'unknown.um4')

                # Ensure .um4 extension
                if not filename.endswith('.um4'):
                    filename += '.um4'

                # Get device and log path within session
                session = self.database.get_session()
                try:
                    device = session.query(Device).filter_by(mac_address=device_mac).first()
                    if not device:
                        return jsonify({'error': 'Device not found'}), 404

                    log_path = device.log_storage_path
                finally:
                    session.close()

                os.makedirs(log_path, exist_ok=True)

                # Full path for file
                file_path = os.path.join(log_path, filename)

                # Get file size from Content-Length header
                content_length = request.content_length or 0

                # Create transfer record
                transfer = self.database.add_transfer(
                    device_mac=device_mac,
                    filename=filename,
                    size_bytes=content_length,
                    status='in_progress'
                )
                transfer_id = transfer.id

                # Notify GUI
                if self.on_transfer_started:
                    self.on_transfer_started(transfer)

                # Record start time for speed calculation
                start_time = datetime.utcnow()

                # Save file
                with open(file_path, 'wb') as f:
                    # Read in chunks
                    chunk_size = 65536  # 64KB chunks
                    while True:
                        chunk = request.stream.read(chunk_size)
                        if not chunk:
                            break
                        f.write(chunk)

                # Calculate transfer speed
                end_time = datetime.utcnow()
                duration = (end_time - start_time).total_seconds()
                if duration > 0:
                    speed_mbps = (content_length / (1024 * 1024)) / duration
                else:
                    speed_mbps = 0

                # Update transfer record
                self.database.update_transfer(
                    transfer_id,
                    status='success',
                    end_time=end_time,
                    transfer_speed_mbps=speed_mbps
                )

                # Notify GUI
                if self.on_transfer_completed:
                    self.on_transfer_completed(transfer_id)

                response = {
                    'status': 'ok',
                    'filename': filename,
                    'saved_to': file_path,
                    'size_bytes': content_length,
                    'transfer_speed_mbps': round(speed_mbps, 2)
                }

                return jsonify(response), 200

            except Exception as e:
                # Update transfer as failed
                if transfer_id:
                    self.database.update_transfer(
                        transfer_id,
                        status='failed',
                        end_time=datetime.utcnow(),
                        error_message=str(e)
                    )
                    if self.on_transfer_completed:
                        self.on_transfer_completed(transfer_id)

                return jsonify({'error': str(e)}), 500

        @self.app.route('/firmware/latest/<device_mac>', methods=['GET'])
        def check_firmware(device_mac):
            """Check for firmware updates for a device."""
            try:
                # For now, return stub response (Phase 7 feature)
                response = {
                    'wp_version': '1.0.0',
                    'wp_url': '/firmware/stable/WP.uf2',
                    'ep_version': '1.0.0',
                    'ep_url': '/firmware/stable/EP.uf2',
                    'update_available': False
                }
                return jsonify(response), 200

            except Exception as e:
                return jsonify({'error': str(e)}), 500

        @self.app.route('/health', methods=['GET'])
        def health():
            """Health check endpoint."""
            return jsonify({'status': 'ok', 'server': 'umod4_server'}), 200

    def _session_cleanup_task(self):
        """Background task to cleanup stale upload sessions."""
        import time

        while self.cleanup_running:
            try:
                # Cleanup sessions older than 24 hours
                count = self.database.cleanup_stale_sessions(max_age_hours=24)
                if count > 0:
                    print(f"Session cleanup: Removed {count} stale sessions")
            except Exception as e:
                print(f"Session cleanup error: {e}")

            # Sleep for 1 hour
            for _ in range(3600):  # Check every second for clean shutdown
                if not self.cleanup_running:
                    break
                time.sleep(1)

    def start(self):
        """Start the HTTP server in a background thread."""
        if self.running:
            return

        self.server = make_server(self.host, self.port, self.app, threaded=True)
        self.server_thread = threading.Thread(target=self.server.serve_forever)
        self.server_thread.daemon = True
        self.server_thread.start()
        self.running = True

        # Start session cleanup task
        self.cleanup_running = True
        self.cleanup_thread = threading.Thread(target=self._session_cleanup_task)
        self.cleanup_thread.daemon = True
        self.cleanup_thread.start()

    def stop(self):
        """Stop the HTTP server."""
        if not self.running:
            return

        if self.server:
            self.server.shutdown()

        # Stop cleanup task
        self.cleanup_running = False
        if self.cleanup_thread:
            self.cleanup_thread.join(timeout=2)

        self.running = False

    def get_url(self):
        """Get the server URL."""
        import socket
        hostname = socket.gethostname()
        return f"http://{hostname}.local:{self.port}"
