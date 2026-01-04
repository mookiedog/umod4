"""Database models and initialization for umod4 server."""

from sqlalchemy import create_engine, Column, String, Integer, Float, Boolean, DateTime, ForeignKey, Text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, relationship
from datetime import datetime, timedelta
import uuid
import os

Base = declarative_base()


class Device(Base):
    """Device registry - tracks all known umod4 devices."""
    __tablename__ = 'devices'

    mac_address = Column(String, primary_key=True)
    name = Column(String, nullable=True, unique=True)  # User-assigned name (blank for new devices, must be unique)
    display_name = Column(String, nullable=False)  # Computed: name or mac_address for display
    log_storage_path = Column(String, nullable=False)
    auto_upload = Column(Boolean, default=True)
    notifications_enabled = Column(Boolean, default=True)
    firmware_track = Column(String, default='stable')  # 'stable' or 'beta'
    first_seen = Column(DateTime, default=datetime.utcnow)
    last_seen = Column(DateTime)
    wp_version = Column(String)
    ep_version = Column(String)
    notes = Column(Text)

    # Relationships
    transfers = relationship("Transfer", back_populates="device", cascade="all, delete-orphan")
    connections = relationship("Connection", back_populates="device", cascade="all, delete-orphan")

    def __repr__(self):
        return f"<Device(mac={self.mac_address}, name={self.name or self.mac_address})>"

    def update_display_name(self):
        """Update display_name based on name field."""
        self.display_name = self.name if self.name else self.mac_address


class Transfer(Base):
    """Transfer history - tracks all file uploads from devices."""
    __tablename__ = 'transfers'

    id = Column(Integer, primary_key=True, autoincrement=True)
    device_mac = Column(String, ForeignKey('devices.mac_address'), nullable=False)
    filename = Column(String, nullable=False)
    size_bytes = Column(Integer, nullable=False)
    transfer_speed_mbps = Column(Float)
    start_time = Column(DateTime, nullable=False, default=datetime.utcnow)
    end_time = Column(DateTime)
    status = Column(String, nullable=False)  # 'success', 'failed', 'in_progress'
    error_message = Column(Text)

    # Relationship
    device = relationship("Device", back_populates="transfers")

    def __repr__(self):
        return f"<Transfer(id={self.id}, device={self.device_mac}, file={self.filename}, status={self.status})>"


class Connection(Base):
    """Connection events - tracks device connections and disconnections."""
    __tablename__ = 'connections'

    id = Column(Integer, primary_key=True, autoincrement=True)
    device_mac = Column(String, ForeignKey('devices.mac_address'), nullable=False)
    connect_time = Column(DateTime, nullable=False, default=datetime.utcnow)
    disconnect_time = Column(DateTime)
    ip_address = Column(String)
    duration_seconds = Column(Integer)

    # Relationship
    device = relationship("Device", back_populates="connections")

    def __repr__(self):
        return f"<Connection(id={self.id}, device={self.device_mac}, ip={self.ip_address})>"


class UploadSession(Base):
    """Upload session - tracks multi-chunk file uploads for resumption."""
    __tablename__ = 'upload_sessions'

    id = Column(Integer, primary_key=True, autoincrement=True)
    session_id = Column(String, unique=True, nullable=False, index=True)  # UUID
    device_mac = Column(String, ForeignKey('devices.mac_address'), nullable=False)
    filename = Column(String, nullable=False)
    total_size = Column(Integer, nullable=False)
    chunk_size = Column(Integer, nullable=False)  # Device-negotiated chunk size
    bytes_received = Column(Integer, nullable=False, default=0)  # Last contiguous byte
    transfer_id = Column(Integer, ForeignKey('transfers.id'), nullable=True)  # Link to Transfer
    created_at = Column(DateTime, nullable=False, default=datetime.utcnow)
    last_activity = Column(DateTime, nullable=False, default=datetime.utcnow)
    status = Column(String, nullable=False, default='in_progress')  # 'in_progress', 'completed', 'failed', 'abandoned'
    partial_file_path = Column(String, nullable=False)  # Path to .part file

    # Relationship
    device = relationship("Device", backref="upload_sessions")
    transfer = relationship("Transfer", backref="upload_session")

    def __repr__(self):
        return f"<UploadSession(id={self.session_id}, device={self.device_mac}, file={self.filename}, progress={self.bytes_received}/{self.total_size})>"


class Database:
    """Database manager for umod4 server."""

    def __init__(self, db_path=None):
        """Initialize database connection.

        Args:
            db_path: Path to SQLite database file. If None, uses default location.
        """
        if db_path is None:
            db_path = self._get_default_db_path()

        # Ensure directory exists
        os.makedirs(os.path.dirname(db_path), exist_ok=True)

        self.db_path = db_path
        self.engine = create_engine(f'sqlite:///{db_path}', echo=False)
        Base.metadata.create_all(self.engine)
        self.Session = sessionmaker(bind=self.engine)

        # Run migrations to update existing databases
        self._migrate_database()

    def _migrate_database(self):
        """Apply database migrations for schema updates."""
        import sqlite3

        # Check if 'name' column exists in devices table
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        try:
            # Check if name column exists
            cursor.execute("PRAGMA table_info(devices)")
            columns = [row[1] for row in cursor.fetchall()]

            if 'name' not in columns:
                print("Migrating database: Adding 'name' column to devices table...")
                # Add name column (nullable, unique)
                cursor.execute("ALTER TABLE devices ADD COLUMN name TEXT")
                conn.commit()
                print("Migration complete: Added 'name' column")

        except Exception as e:
            print(f"Warning: Database migration failed: {e}")
            conn.rollback()
        finally:
            conn.close()

    @staticmethod
    def _get_default_db_path():
        """Get default database path based on platform."""
        if os.name == 'nt':  # Windows
            base_path = os.path.join(os.environ.get('APPDATA', ''), 'umod4_server')
        else:  # Linux/macOS/WSL2
            base_path = os.path.expanduser('~/.umod4_server')

        os.makedirs(base_path, exist_ok=True)
        return os.path.join(base_path, 'devices.db')

    def get_session(self):
        """Get a new database session."""
        return self.Session()

    def get_or_create_device(self, mac_address, default_name=None):
        """Get existing device or create new one.

        Args:
            mac_address: Device MAC address
            default_name: Default display name if creating new device

        Returns:
            tuple: (Device, is_new) where is_new is True if device was just created
        """
        session = self.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()

            if device:
                # Update last_seen
                device.last_seen = datetime.utcnow()
                session.commit()
                return device, False
            else:
                # Create new device with default settings
                if default_name is None:
                    default_name = mac_address

                # Default log storage path (will prompt user to customize)
                if os.name == 'nt':
                    base_path = os.path.join(os.environ.get('APPDATA', ''), 'umod4_server', 'logs')
                else:
                    base_path = os.path.expanduser('~/.umod4_server/logs')

                # Use MAC with dashes removed for directory name
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
                session.commit()

                # Create log storage directory
                os.makedirs(log_path, exist_ok=True)

                return device, True
        finally:
            session.close()

    def add_transfer(self, device_mac, filename, size_bytes, status='in_progress'):
        """Add a new transfer record.

        Args:
            device_mac: Device MAC address
            filename: Name of file being transferred
            size_bytes: File size in bytes
            status: Transfer status (default: 'in_progress')

        Returns:
            Transfer: The created transfer record
        """
        session = self.get_session()
        try:
            transfer = Transfer(
                device_mac=device_mac,
                filename=filename,
                size_bytes=size_bytes,
                status=status,
                start_time=datetime.utcnow()
            )
            session.add(transfer)
            session.commit()
            transfer_id = transfer.id
            return session.query(Transfer).get(transfer_id)
        finally:
            session.close()

    def update_transfer(self, transfer_id, **kwargs):
        """Update transfer record.

        Args:
            transfer_id: Transfer ID
            **kwargs: Fields to update (status, end_time, transfer_speed_mbps, error_message)
        """
        session = self.get_session()
        try:
            transfer = session.query(Transfer).get(transfer_id)
            if transfer:
                for key, value in kwargs.items():
                    setattr(transfer, key, value)
                session.commit()
        finally:
            session.close()

    def add_connection(self, device_mac, ip_address=None):
        """Add a new connection event.

        Args:
            device_mac: Device MAC address
            ip_address: Device IP address

        Returns:
            Connection: The created connection record
        """
        session = self.get_session()
        try:
            connection = Connection(
                device_mac=device_mac,
                connect_time=datetime.utcnow(),
                ip_address=ip_address
            )
            session.add(connection)
            session.commit()
            connection_id = connection.id
            return session.query(Connection).get(connection_id)
        finally:
            session.close()

    def close_connection(self, connection_id):
        """Mark connection as closed.

        Args:
            connection_id: Connection ID
        """
        session = self.get_session()
        try:
            connection = session.query(Connection).get(connection_id)
            if connection:
                connection.disconnect_time = datetime.utcnow()
                if connection.connect_time:
                    delta = connection.disconnect_time - connection.connect_time
                    connection.duration_seconds = int(delta.total_seconds())
                session.commit()
        finally:
            session.close()

    def create_upload_session(self, device_mac, filename, total_size, chunk_size):
        """Create a new upload session.

        Args:
            device_mac: Device MAC address
            filename: Name of file being uploaded
            total_size: Total file size in bytes
            chunk_size: Device-negotiated chunk size

        Returns:
            UploadSession: The created session
        """
        session = self.get_session()
        try:
            # Generate unique session ID
            session_id = str(uuid.uuid4())

            # Get device's log storage path
            device = session.query(Device).filter_by(mac_address=device_mac).first()
            if not device:
                raise ValueError(f"Device {device_mac} not found")

            # Create partial file path
            partial_file_path = os.path.join(device.log_storage_path, f"{filename}.part")

            upload_session = UploadSession(
                session_id=session_id,
                device_mac=device_mac,
                filename=filename,
                total_size=total_size,
                chunk_size=chunk_size,
                bytes_received=0,
                partial_file_path=partial_file_path,
                created_at=datetime.utcnow(),
                last_activity=datetime.utcnow(),
                status='in_progress'
            )
            session.add(upload_session)
            session.commit()

            # Return session object (need to query again to get ID)
            result_session = session.query(UploadSession).filter_by(session_id=session_id).first()
            return result_session
        finally:
            session.close()

    def get_upload_session(self, session_id):
        """Get upload session by session ID.

        Args:
            session_id: Session UUID string

        Returns:
            UploadSession or None
        """
        session = self.get_session()
        try:
            return session.query(UploadSession).filter_by(session_id=session_id).first()
        finally:
            session.close()

    def update_upload_session(self, session_id, **kwargs):
        """Update upload session.

        Args:
            session_id: Session UUID
            **kwargs: Fields to update (bytes_received, status, last_activity, etc.)
        """
        session = self.get_session()
        try:
            upload_session = session.query(UploadSession).filter_by(session_id=session_id).first()
            if upload_session:
                for key, value in kwargs.items():
                    setattr(upload_session, key, value)
                upload_session.last_activity = datetime.utcnow()
                session.commit()
        finally:
            session.close()

    def cleanup_stale_sessions(self, max_age_hours=24):
        """Delete upload sessions older than max_age_hours with no activity.

        Args:
            max_age_hours: Maximum age in hours before session is considered stale

        Returns:
            int: Number of sessions cleaned up
        """
        session = self.get_session()
        try:
            cutoff_time = datetime.utcnow() - timedelta(hours=max_age_hours)
            stale_sessions = session.query(UploadSession).filter(
                UploadSession.last_activity < cutoff_time,
                UploadSession.status == 'in_progress'
            ).all()

            count = 0
            for upload_session in stale_sessions:
                # Delete partial file if exists
                if os.path.exists(upload_session.partial_file_path):
                    try:
                        os.remove(upload_session.partial_file_path)
                    except Exception as e:
                        print(f"Warning: Failed to delete {upload_session.partial_file_path}: {e}")

                # Mark as abandoned
                upload_session.status = 'abandoned'
                count += 1

            session.commit()
            return count
        finally:
            session.close()

    def find_existing_upload_session(self, device_mac, filename):
        """Find existing in-progress upload session for a file.

        Args:
            device_mac: Device MAC address
            filename: Filename being uploaded

        Returns:
            UploadSession or None
        """
        session = self.get_session()
        try:
            return session.query(UploadSession).filter_by(
                device_mac=device_mac,
                filename=filename,
                status='in_progress'
            ).order_by(UploadSession.created_at.desc()).first()
        finally:
            session.close()

    def update_device_name(self, mac_address, new_name):
        """Update device name and handle directory renaming.

        Args:
            mac_address: Device MAC address
            new_name: New name for device (or None/empty string to clear)

        Returns:
            tuple: (success, error_message)
        """
        import shutil

        session = self.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return False, "Device not found"

            # Normalize empty string to None
            new_name = new_name.strip() if new_name else None

            # Check if name is unique (if not blank)
            if new_name:
                existing = session.query(Device).filter(
                    Device.name == new_name,
                    Device.mac_address != mac_address
                ).first()
                if existing:
                    return False, f"Name '{new_name}' is already in use by another device"

            old_name = device.name
            old_path = device.log_storage_path

            # Update name
            device.name = new_name
            device.update_display_name()

            # Update log storage path if needed
            if old_name != new_name:
                # Determine new path
                if os.name == 'nt':
                    base_path = os.path.join(os.environ.get('APPDATA', ''), 'umod4_server', 'logs')
                else:
                    base_path = os.path.expanduser('~/.umod4_server/logs')

                if new_name:
                    # Use name for directory
                    new_path = os.path.join(base_path, new_name)
                else:
                    # Use MAC for directory
                    mac_clean = mac_address.replace(':', '-')
                    new_path = os.path.join(base_path, mac_clean)

                # Rename directory if old one exists
                if os.path.exists(old_path) and old_path != new_path:
                    try:
                        # Ensure parent exists
                        os.makedirs(base_path, exist_ok=True)

                        # Move directory
                        shutil.move(old_path, new_path)
                        device.log_storage_path = new_path
                    except Exception as e:
                        return False, f"Failed to rename directory: {str(e)}"
                else:
                    # Just update path, create if needed
                    device.log_storage_path = new_path
                    os.makedirs(new_path, exist_ok=True)

            session.commit()
            return True, None

        except Exception as e:
            session.rollback()
            return False, str(e)
        finally:
            session.close()
