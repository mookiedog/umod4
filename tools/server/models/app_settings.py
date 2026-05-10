"""Persistent application settings backed by a JSON file."""

import json
import os


class AppSettings:
    """Simple key-value store persisted as JSON alongside the database."""

    def __init__(self, settings_path=None):
        if settings_path is None:
            if os.name == 'nt':
                base_path = os.path.join(os.environ.get('APPDATA', ''), 'umod4_server')
            else:
                base_path = os.path.expanduser('~/.umod4_server')
            os.makedirs(base_path, exist_ok=True)
            settings_path = os.path.join(base_path, 'app_settings.json')

        self._path = settings_path
        self._data = {}
        self._load()

    def _load(self):
        if os.path.exists(self._path):
            try:
                with open(self._path, 'r') as f:
                    self._data = json.load(f)
            except (json.JSONDecodeError, OSError):
                self._data = {}

    def _save(self):
        try:
            with open(self._path, 'w') as f:
                json.dump(self._data, f, indent=2)
        except OSError as e:
            print(f"AppSettings: failed to save {self._path}: {e}")

    def get(self, key, default=None):
        return self._data.get(key, default)

    def set(self, key, value):
        self._data[key] = value
        self._save()
