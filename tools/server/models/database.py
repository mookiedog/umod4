"""Database models and initialization for umod4 server."""

from sqlalchemy import create_engine, Column, String, Integer, Float, Boolean, DateTime, ForeignKey, Text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, relationship
from datetime import datetime
import os

Base = declarative_base()


class Device(Base):
    """Device registry - tracks all known umod4 devices."""
    __tablename__ = 'devices'

    mac_address = Column(String, primary_key=True)
    display_name = Column(String, nullable=False)
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
        return f"<Device(mac={self.mac_address}, name={self.display_name})>"


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
                    display_name=default_name,
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
