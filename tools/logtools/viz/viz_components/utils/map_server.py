"""Serves the GPS track map page from http://127.0.0.1:<port>/.

The map page loads its tiles from OpenStreetMap, whose servers answer any request that carries no
Referer header with a "403 Access blocked" picture.  A page opened as a file:// URL sends no
Referer.  A page served from a local web server does, so the visualizer serves the map page
itself instead of handing the browser a file.

The server listens on 127.0.0.1 only (this machine), serves nothing but the one page, and runs
in a daemon thread, so it ends when the visualizer ends.
"""

import http.server
import threading


class MapPageServer:
    """One tiny local web server that serves the latest map page."""

    def __init__(self):
        self._page = b""
        self._server = None
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path.split("?")[0] in ("/", "/map.html"):
                    body = owner._page
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(body)
                else:
                    self.send_error(404)

            def log_message(self, *args):
                pass    # keep the visualizer's console quiet

        self._handler = Handler

    def serve(self, html):
        """Make html the page that is served, start the server if needed, and return the URL."""
        self._page = html.encode("utf-8")
        if self._server is None:
            self._server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), self._handler)
            threading.Thread(target=self._server.serve_forever, daemon=True).start()
        return f"http://127.0.0.1:{self._server.server_address[1]}/"
