"""Device file management dialog."""

from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QPushButton,
                               QTableWidget, QTableWidgetItem, QHeaderView,
                               QMessageBox, QLabel, QCheckBox, QProgressDialog)
from PySide6.QtCore import Qt, QThread, Signal
from device_client import DeviceClient


class DeviceFilesDialog(QDialog):
    """Dialog for viewing and managing files on a device."""

    def __init__(self, database, device_mac, device_ip, parent=None):
        super().__init__(parent)
        self.database = database
        self.device_mac = device_mac
        self.device_ip = device_ip
        self.device_name = None
        self.setWindowTitle(f"Manage Files on Device")
        self.setModal(True)
        self.resize(800, 500)

        self._load_device_info()
        self._setup_ui()
        self._refresh_files()

    def _load_device_info(self):
        """Load device information from database."""
        from models.database import Device
        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.device_mac).first()
            if device:
                self.device_name = device.display_name
                self.setWindowTitle(f"Manage Files on {self.device_name}")
        finally:
            session.close()

    def _setup_ui(self):
        """Set up the dialog UI."""
        layout = QVBoxLayout(self)

        # Header
        header_text = f"Files on {self.device_name or self.device_mac}"
        if self.device_ip:
            header_text += f" ({self.device_ip})"
        header = QLabel(header_text)
        header.setStyleSheet("font-weight: bold; font-size: 14px;")
        layout.addWidget(header)

        # Info label
        info_label = QLabel("Log files (.um4, .log) must be downloaded before deletion. Other files can be deleted freely.")
        info_label.setStyleSheet("color: gray; font-size: 11px;")
        layout.addWidget(info_label)

        # File table
        self.file_table = QTableWidget()
        self.file_table.setColumnCount(5)
        self.file_table.setHorizontalHeaderLabels([
            "☑", "Filename", "Size (KB)", "Downloaded", "Status"
        ])
        self.file_table.setColumnWidth(0, 40)
        self.file_table.setColumnWidth(2, 100)
        self.file_table.setColumnWidth(3, 100)
        self.file_table.horizontalHeader().setStretchLastSection(True)
        self.file_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.file_table.setSelectionMode(QTableWidget.SelectionMode.NoSelection)
        layout.addWidget(self.file_table)

        # Buttons
        button_layout = QHBoxLayout()

        self.select_all_button = QPushButton("Select All Deletable")
        self.select_all_button.clicked.connect(self._select_all_deletable)
        button_layout.addWidget(self.select_all_button)

        self.deselect_all_button = QPushButton("Deselect All")
        self.deselect_all_button.clicked.connect(self._deselect_all)
        button_layout.addWidget(self.deselect_all_button)

        button_layout.addStretch()

        self.delete_button = QPushButton("Delete Selected")
        self.delete_button.clicked.connect(self._delete_selected)
        self.delete_button.setStyleSheet("QPushButton { background-color: #d32f2f; color: white; }")
        button_layout.addWidget(self.delete_button)

        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.clicked.connect(self._refresh_files)
        button_layout.addWidget(self.refresh_button)

        self.close_button = QPushButton("Close")
        self.close_button.clicked.connect(self.accept)
        button_layout.addWidget(self.close_button)

        layout.addLayout(button_layout)

    def _refresh_files(self):
        """Refresh file list from device."""
        try:
            # Get files from device
            client = DeviceClient(self.device_ip, timeout=10)
            device_files = client.list_log_files()

            if device_files is None:
                QMessageBox.warning(self, "Connection Error",
                                  f"Failed to connect to device at {self.device_ip}")
                return

            # Get list of successfully downloaded files from database
            from models.database import Transfer
            session = self.database.get_session()
            try:
                # Get all successful transfers for this device
                successful_transfers = session.query(Transfer).filter_by(
                    device_mac=self.device_mac,
                    status='success'
                ).all()

                downloaded_files = {t.filename for t in successful_transfers}
            finally:
                session.close()

            # Populate table
            self.file_table.setRowCount(len(device_files))

            for row, file_info in enumerate(device_files):
                filename = file_info['filename']
                file_size = file_info['size']
                is_log_file = filename.endswith('.um4') or filename.endswith('.log')
                is_downloaded = filename in downloaded_files

                # Determine if file can be deleted:
                # - Log files (.um4, .log) require download first
                # - Other files (uploaded files like .uf2) can always be deleted
                can_delete = is_downloaded or not is_log_file

                # Checkbox (enabled based on can_delete)
                checkbox = QCheckBox()
                checkbox.setEnabled(can_delete)
                checkbox_widget = QTableWidgetItem()
                checkbox_widget.setData(Qt.ItemDataRole.UserRole, filename)
                self.file_table.setItem(row, 0, checkbox_widget)
                self.file_table.setCellWidget(row, 0, checkbox)

                # Filename
                name_item = QTableWidgetItem(filename)
                name_item.setFlags(name_item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                self.file_table.setItem(row, 1, name_item)

                # Size in KB
                size_kb = file_size / 1024
                size_item = QTableWidgetItem(f"{size_kb:,.1f}")
                size_item.setFlags(size_item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                size_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
                self.file_table.setItem(row, 2, size_item)

                # Downloaded status (only relevant for log files)
                if is_log_file:
                    downloaded_item = QTableWidgetItem("Yes" if is_downloaded else "No")
                    downloaded_item.setFlags(downloaded_item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                    if is_downloaded:
                        downloaded_item.setForeground(Qt.GlobalColor.darkGreen)
                    else:
                        downloaded_item.setForeground(Qt.GlobalColor.red)
                else:
                    downloaded_item = QTableWidgetItem("N/A")
                    downloaded_item.setFlags(downloaded_item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                    downloaded_item.setForeground(Qt.GlobalColor.gray)
                self.file_table.setItem(row, 3, downloaded_item)

                # Status
                if can_delete:
                    status = "Can delete"
                else:
                    status = "Log not downloaded"
                status_item = QTableWidgetItem(status)
                status_item.setFlags(status_item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                if not can_delete:
                    status_item.setForeground(Qt.GlobalColor.gray)
                self.file_table.setItem(row, 4, status_item)

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to refresh file list: {e}")

    def _select_all_deletable(self):
        """Select all files that can be deleted."""
        for row in range(self.file_table.rowCount()):
            checkbox = self.file_table.cellWidget(row, 0)
            if checkbox and checkbox.isEnabled():
                checkbox.setChecked(True)

    def _deselect_all(self):
        """Deselect all files."""
        for row in range(self.file_table.rowCount()):
            checkbox = self.file_table.cellWidget(row, 0)
            if checkbox:
                checkbox.setChecked(False)

    def _get_selected_files(self):
        """Get list of selected filenames."""
        selected = []
        for row in range(self.file_table.rowCount()):
            checkbox = self.file_table.cellWidget(row, 0)
            if checkbox and checkbox.isChecked():
                filename_item = self.file_table.item(row, 0)
                filename = filename_item.data(Qt.ItemDataRole.UserRole)
                selected.append(filename)
        return selected

    def _delete_selected(self):
        """Delete selected files from device."""
        selected_files = self._get_selected_files()

        if not selected_files:
            QMessageBox.warning(self, "No Selection",
                              "Please select at least one file to delete.")
            return

        # Confirmation dialog
        file_list = "\n".join(f"  • {f}" for f in selected_files)
        log_files = [f for f in selected_files if f.endswith('.um4') or f.endswith('.log')]
        other_files = [f for f in selected_files if not (f.endswith('.um4') or f.endswith('.log'))]

        extra_info = ""
        if log_files:
            extra_info = f"\n\n{len(log_files)} log file(s) have been safely downloaded to your computer."
        if other_files:
            extra_info += f"\n\n{len(other_files)} non-log file(s) will be permanently deleted."

        reply = QMessageBox.question(
            self,
            "Confirm Deletion",
            f"Delete {len(selected_files)} file(s) from device?\n\n"
            f"{file_list}\n\n"
            f"This action cannot be undone!{extra_info}",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )

        if reply != QMessageBox.StandardButton.Yes:
            return

        # Delete files
        client = DeviceClient(self.device_ip, timeout=30)
        failed_deletions = []

        progress = QProgressDialog("Deleting files...", "Cancel", 0, len(selected_files), self)
        progress.setWindowModality(Qt.WindowModality.WindowModal)
        progress.setMinimumDuration(0)

        for i, filename in enumerate(selected_files):
            if progress.wasCanceled():
                break

            progress.setValue(i)
            progress.setLabelText(f"Deleting {filename}...")

            success, error_msg = client.delete_log_file(filename)
            if not success:
                failed_deletions.append((filename, error_msg))

        progress.setValue(len(selected_files))

        # Show results
        if failed_deletions:
            error_list = "\n".join(f"  • {f}: {e}" for f, e in failed_deletions)
            QMessageBox.warning(
                self,
                "Deletion Incomplete",
                f"Failed to delete {len(failed_deletions)} file(s):\n\n{error_list}"
            )
        else:
            QMessageBox.information(
                self,
                "Deletion Complete",
                f"Successfully deleted {len(selected_files)} file(s) from device."
            )

        # Refresh file list
        self._refresh_files()
