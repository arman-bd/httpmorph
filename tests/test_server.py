"""
Test HTTP/HTTPS server for httpmorph testing

Provides a simple HTTP/HTTPS server that can be used to test the httpmorph client.
"""

import json
import os
import ssl
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Optional


class MockHTTPHandler(BaseHTTPRequestHandler):
    """HTTP request handler for testing"""

    def log_message(self, format, *args):
        """Suppress log messages during tests"""
        pass

    def do_GET(self):
        """Handle GET requests"""
        # Strip query parameters for path matching
        path_without_query = self.path.split("?")[0]

        if path_without_query == "/get":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {
                "method": "GET",
                "path": self.path,
                "headers": dict(self.headers),
                "args": {},
            }
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query == "/status/200":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"OK")

        elif path_without_query == "/status/204":
            self.send_response(204)
            self.end_headers()
            # 204 No Content - no body

        elif path_without_query == "/status/404":
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

        elif path_without_query == "/headers":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {"headers": dict(self.headers)}
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query == "/delay/1":
            time.sleep(1)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Delayed response")

        elif path_without_query == "/redirect/1":
            self.send_response(302)
            self.send_header("Location", "/get")
            self.end_headers()

        elif path_without_query == "/gzip":
            import gzip

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Encoding", "gzip")
            self.end_headers()
            response = {"compressed": True, "gzipped": True}
            compressed = gzip.compress(json.dumps(response).encode())
            self.wfile.write(compressed)

        elif path_without_query == "/user-agent":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {"user-agent": self.headers.get("User-Agent", "")}
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query.startswith("/redirect/"):
            # Extract redirect count
            try:
                count = int(path_without_query.split("/")[-1])
                if count > 1:
                    self.send_response(302)
                    self.send_header("Location", f"/redirect/{count - 1}")
                    self.end_headers()
                else:
                    self.send_response(302)
                    self.send_header("Location", "/get")
                    self.end_headers()
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query.startswith("/status/"):
            # Extract status code
            try:
                status_code = int(path_without_query.split("/")[-1])
                self.send_response(status_code)
                if status_code != 204:  # 204 No Content has no body
                    self.send_header("Content-Type", "text/plain")
                    self.end_headers()
                    if status_code >= 200 and status_code < 300:
                        self.wfile.write(f"Status {status_code}".encode())
                else:
                    self.end_headers()
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query == "/unicode":
            # Return various unicode characters
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            response = {
                "message": "Hello World",
                "chinese": "你好世界",
                "japanese": "こんにちは世界",
                "korean": "안녕하세요 세계",
                "arabic": "مرحبا بالعالم",
                "russian": "Привет мир",
                "emoji": "👋🌍🎉",
                "mixed": "Hello 世界 🌍",
                "special": "¡Hñola! Çà va? Ñoño",
            }
            self.wfile.write(json.dumps(response, ensure_ascii=False).encode("utf-8"))

        elif path_without_query == "/binary":
            # Return binary data
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.end_headers()
            # Mix of binary data including null bytes and high bytes
            binary_data = bytes(range(256))
            self.wfile.write(binary_data)

        elif path_without_query == "/utf8-header":
            # Return response with UTF-8 in custom headers
            # Note: HTTP headers must be latin-1, so we use percent-encoding for unicode
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("X-Custom-Message", "Hello World")
            self.send_header("X-Chinese-Encoded", "%E4%BD%A0%E5%A5%BD")  # URL-encoded 你好
            self.send_header("X-Latin", "café")  # Latin-1 compatible
            self.end_headers()
            self.wfile.write(b"Response with unicode-safe headers")

        elif path_without_query == "/malformed-json":
            # Return malformed JSON
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"incomplete": "json"')

        elif path_without_query == "/mixed-encoding":
            # Return mixed encoding content
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            # Mix of UTF-8 and latin-1 characters
            text = "UTF-8: 你好 Latin-1: café résumé"
            self.wfile.write(text.encode("utf-8"))

        elif path_without_query == "/very-long-line":
            # Return response with very long line
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            long_line = "A" * 10000 + "\n"
            self.wfile.write(long_line.encode("utf-8"))

        elif path_without_query == "/null-bytes":
            # Return response with null bytes
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.end_headers()
            data = b"Hello\x00World\x00\x00Test"
            self.wfile.write(data)

        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_POST(self):
        """Handle POST requests"""
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        if self.path == "/post":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()

            try:
                json_data = json.loads(body.decode()) if body else {}
            except Exception:
                json_data = None

            response = {
                "method": "POST",
                "path": self.path,
                "headers": dict(self.headers),
                "data": body.decode() if body else "",
                "json": json_data,
            }
            self.wfile.write(json.dumps(response).encode())

        elif self.path == "/post/form":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {"method": "POST", "form": body.decode() if body else ""}
            self.wfile.write(json.dumps(response).encode())

        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_PUT(self):
        """Handle PUT requests"""
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        response = {"method": "PUT", "path": self.path, "data": body.decode() if body else ""}
        self.wfile.write(json.dumps(response).encode())

    def do_DELETE(self):
        """Handle DELETE requests"""
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        response = {"method": "DELETE", "path": self.path}
        self.wfile.write(json.dumps(response).encode())


class MockHTTPServer:
    """Mock HTTP/HTTPS server for testing"""

    def __init__(self, port: int = 0, ssl_enabled: bool = False):
        self.port = port
        self.ssl_enabled = ssl_enabled
        self.server: Optional[HTTPServer] = None
        self.thread: Optional[threading.Thread] = None
        self.cert_file = None
        self.key_file = None

    def start(self):
        """Start the test server"""
        self.server = HTTPServer(("127.0.0.1", self.port), MockHTTPHandler)

        if self.ssl_enabled:
            # Create self-signed certificate for testing
            self._create_self_signed_cert()
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(self.cert_file, self.key_file)
            self.server.socket = context.wrap_socket(self.server.socket, server_side=True)

        # Get the actual port if 0 was specified
        self.port = self.server.server_port

        # Start server in background thread
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()

        # Wait a bit for server to start
        time.sleep(0.1)

    def stop(self):
        """Stop the test server"""
        if self.server:
            self.server.shutdown()
            self.server.server_close()
        if self.thread:
            self.thread.join(timeout=1)

        # Clean up certificate files
        if self.cert_file and os.path.exists(self.cert_file):
            os.unlink(self.cert_file)
        if self.key_file and os.path.exists(self.key_file):
            os.unlink(self.key_file)

    def _create_self_signed_cert(self):
        """Create a self-signed certificate for testing"""
        try:
            import datetime

            from cryptography import x509
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import rsa
            from cryptography.x509.oid import NameOID

            # Generate private key
            key = rsa.generate_private_key(
                public_exponent=65537,
                key_size=2048,
            )

            # Generate certificate
            subject = issuer = x509.Name(
                [
                    x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
                    x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "Test"),
                    x509.NameAttribute(NameOID.LOCALITY_NAME, "Test"),
                    x509.NameAttribute(NameOID.ORGANIZATION_NAME, "httpmorph"),
                    x509.NameAttribute(NameOID.COMMON_NAME, "localhost"),
                ]
            )

            cert = (
                x509.CertificateBuilder()
                .subject_name(subject)
                .issuer_name(issuer)
                .public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(datetime.datetime.utcnow())
                .not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(days=1))
                .add_extension(
                    x509.SubjectAlternativeName(
                        [
                            x509.DNSName("localhost"),
                            x509.DNSName("127.0.0.1"),
                        ]
                    ),
                    critical=False,
                )
                .sign(key, hashes.SHA256())
            )

            # Write certificate and key to temp files
            with tempfile.NamedTemporaryFile(mode="wb", delete=False, suffix=".crt") as f:
                f.write(cert.public_bytes(serialization.Encoding.PEM))
                self.cert_file = f.name

            with tempfile.NamedTemporaryFile(mode="wb", delete=False, suffix=".key") as f:
                f.write(
                    key.private_bytes(
                        encoding=serialization.Encoding.PEM,
                        format=serialization.PrivateFormat.TraditionalOpenSSL,
                        encryption_algorithm=serialization.NoEncryption(),
                    )
                )
                self.key_file = f.name

        except ImportError:
            # If cryptography is not available, skip SSL tests
            raise RuntimeError("cryptography package required for SSL tests")

    @property
    def url(self):
        """Get the base URL for the server"""
        protocol = "https" if self.ssl_enabled else "http"
        return f"{protocol}://127.0.0.1:{self.port}"

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
