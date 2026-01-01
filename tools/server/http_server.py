"""Flask HTTP server for umod4 device communication."""

from flask import Flask, request, jsonify, send_file
from werkzeug.serving import make_server
from datetime import datetime
import threading
import os
import json
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
                            display_name=mac_address,  # Default to MAC, user can rename
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

        @self.app.route('/logs/upload/<device_mac>', methods=['POST'])
        def upload_log(device_mac):
            """Upload a log file from a device."""
            try:
                # Get filename from header or use default
                filename = request.headers.get('X-Filename', 'unknown.um4')

                # Ensure .um4 extension
                if not filename.endswith('.um4'):
                    filename += '.um4'

                # Get device
                device, _ = self.database.get_or_create_device(device_mac)
                log_path = device.log_storage_path
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
                    transfer.id,
                    status='success',
                    end_time=end_time,
                    transfer_speed_mbps=speed_mbps
                )

                # Notify GUI
                if self.on_transfer_completed:
                    self.on_transfer_completed(transfer.id)

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
                if 'transfer' in locals():
                    self.database.update_transfer(
                        transfer.id,
                        status='failed',
                        end_time=datetime.utcnow(),
                        error_message=str(e)
                    )
                    if self.on_transfer_completed:
                        self.on_transfer_completed(transfer.id)

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

    def start(self):
        """Start the HTTP server in a background thread."""
        if self.running:
            return

        self.server = make_server(self.host, self.port, self.app, threaded=True)
        self.server_thread = threading.Thread(target=self.server.serve_forever)
        self.server_thread.daemon = True
        self.server_thread.start()
        self.running = True

    def stop(self):
        """Stop the HTTP server."""
        if not self.running:
            return

        if self.server:
            self.server.shutdown()
        self.running = False

    def get_url(self):
        """Get the server URL."""
        import socket
        hostname = socket.gethostname()
        return f"http://{hostname}.local:{self.port}"
