"""
Simple HTTP proxy server for testing
"""

import base64
import select
import socket
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer


class ProxyHandler(BaseHTTPRequestHandler):
    """HTTP proxy handler"""

    def log_message(self, format, *args):
        """Suppress log messages during tests"""
        pass

    def do_CONNECT(self):
        """Handle CONNECT method for HTTPS proxying"""
        # Check for Proxy-Authorization if auth is required
        if hasattr(self.server, "username") and self.server.username:
            auth_header = self.headers.get("Proxy-Authorization")
            if not auth_header:
                self.send_response(407)
                self.send_header("Proxy-Authenticate", 'Basic realm="Proxy"')
                self.end_headers()
                return

            # Verify credentials
            try:
                auth_type, credentials = auth_header.split(" ", 1)
                if auth_type == "Basic":
                    decoded = base64.b64decode(credentials).decode("utf-8")
                    username, password = decoded.split(":", 1)
                    if username != self.server.username or password != self.server.password:
                        self.send_response(403)
                        self.end_headers()
                        return
            except Exception:
                self.send_response(403)
                self.end_headers()
                return

        # Parse host and port
        host, port = self.path.split(":")
        port = int(port)

        try:
            # Connect to target server
            target_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            target_sock.connect((host, port))

            # Send success response
            self.send_response(200, "Connection Established")
            self.end_headers()

            # Relay data between client and target using select()
            sockets = [self.connection, target_sock]
            timeout = 30  # 30 second timeout for idle connections

            while True:
                try:
                    # Wait for data on either socket with timeout
                    readable, _, exceptional = select.select(sockets, [], sockets, timeout)

                    # If timeout or error, close connection
                    if not readable or exceptional:
                        break

                    # Process readable sockets
                    for sock in readable:
                        try:
                            data = sock.recv(4096)
                            if not data:
                                # Connection closed by one side
                                target_sock.close()
                                return

                            # Send data to the other socket
                            if sock is self.connection:
                                target_sock.sendall(data)
                            else:
                                self.connection.sendall(data)
                        except Exception:
                            # Error reading/writing, close connection
                            target_sock.close()
                            return
                except Exception:
                    break

            target_sock.close()
        except Exception:
            self.send_response(502)
            self.end_headers()

    def do_GET(self):
        """Handle GET requests (for HTTP proxying)"""
        # Check for Proxy-Authorization if auth is required
        if hasattr(self.server, "username") and self.server.username:
            auth_header = self.headers.get("Proxy-Authorization")
            if not auth_header:
                self.send_response(407)
                self.send_header("Proxy-Authenticate", 'Basic realm="Proxy"')
                self.end_headers()
                return

            # Verify credentials
            try:
                auth_type, credentials = auth_header.split(" ", 1)
                if auth_type == "Basic":
                    decoded = base64.b64decode(credentials).decode("utf-8")
                    username, password = decoded.split(":", 1)
                    if username != self.server.username or password != self.server.password:
                        self.send_response(403)
                        self.end_headers()
                        return
            except Exception:
                self.send_response(403)
                self.end_headers()
                return

        # For testing, just return a simple response
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("X-Proxied", "true")
        self.end_headers()
        self.wfile.write(b"Proxied response")


class MockProxyServer:
    """Mock HTTP proxy server for testing"""

    def __init__(self, port=0, username=None, password=None):
        self.port = port
        self.username = username
        self.password = password
        self.server = None
        self.thread = None

    def start(self):
        """Start the proxy server"""
        self.server = HTTPServer(("127.0.0.1", self.port), ProxyHandler)

        # Attach auth credentials to server
        if self.username:
            self.server.username = self.username
            self.server.password = self.password

        # Get actual port if 0 was specified
        self.port = self.server.server_port

        # Start in background thread
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()

        # Wait a bit for server to start
        import time

        time.sleep(0.1)

    def stop(self):
        """Stop the proxy server"""
        if self.server:
            self.server.shutdown()
            self.server.server_close()
        if self.thread:
            self.thread.join(timeout=1)

    @property
    def url(self):
        """Get proxy URL"""
        return f"http://127.0.0.1:{self.port}"

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
