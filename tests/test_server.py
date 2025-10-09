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

            # Parse query parameters
            args = {}
            if "?" in self.path:
                from urllib.parse import parse_qs

                query = self.path.split("?", 1)[1]
                params = parse_qs(query, keep_blank_values=True)
                # Convert lists to single values where appropriate
                for key, values in params.items():
                    if len(values) == 1:
                        args[key] = values[0]
                    else:
                        args[key] = values

            response = {
                "method": "GET",
                "path": self.path,
                "headers": dict(self.headers),
                "args": args,
                "url": f"http://{self.headers.get('Host', 'localhost')}{self.path}",
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

        elif path_without_query.startswith("/delay/"):
            # Extract delay time
            try:
                delay = int(path_without_query.split("/")[-1])
                time.sleep(delay)
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                response = {"delay": delay}
                self.wfile.write(json.dumps(response).encode())
            except Exception:
                self.send_response(400)
                self.end_headers()

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

        elif path_without_query == "/ip":
            # Return client IP
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            # Get client IP from connection
            client_ip = self.client_address[0] if self.client_address else "127.0.0.1"
            response = {"origin": client_ip}
            self.wfile.write(json.dumps(response).encode())

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

        elif path_without_query.startswith("/absolute-redirect/"):
            # Absolute redirect
            try:
                count = int(path_without_query.split("/")[-1])
                if count > 1:
                    self.send_response(302)
                    # Get the full URL
                    host = self.headers.get("Host", "localhost")
                    self.send_header("Location", f"http://{host}/absolute-redirect/{count - 1}")
                    self.end_headers()
                else:
                    self.send_response(302)
                    host = self.headers.get("Host", "localhost")
                    self.send_header("Location", f"http://{host}/get")
                    self.end_headers()
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query.startswith("/relative-redirect/"):
            # Relative redirect
            try:
                count = int(path_without_query.split("/")[-1])
                if count > 1:
                    self.send_response(302)
                    self.send_header("Location", f"/relative-redirect/{count - 1}")
                    self.end_headers()
                else:
                    self.send_response(302)
                    self.send_header("Location", "/get")
                    self.end_headers()
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query == "/json":
            # Return JSON data
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {
                "slideshow": {
                    "author": "Yours Truly",
                    "date": "date of publication",
                    "slides": [
                        {"title": "Wake up to WonderWidgets!", "type": "all"},
                        {
                            "items": [
                                "Why <em>WonderWidgets</em> are great",
                                "Who <em>buys</em> WonderWidgets",
                            ],
                            "title": "Overview",
                            "type": "all",
                        },
                    ],
                    "title": "Sample Slide Show",
                }
            }
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query == "/html":
            # Return HTML content
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            html = """<!DOCTYPE html>
<html>
<head><title>Test HTML</title></head>
<body><h1>Test HTML Page</h1></body>
</html>"""
            self.wfile.write(html.encode())

        elif path_without_query.startswith("/bytes/"):
            # Return binary data of specified length
            try:
                length = int(path_without_query.split("/")[-1])
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.end_headers()
                self.wfile.write(b"\x00" * length)
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query.startswith("/stream-bytes/"):
            # Stream binary data of specified length
            try:
                length = int(path_without_query.split("/")[-1])
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.end_headers()
                # Send in chunks to simulate streaming
                chunk_size = 1024
                for i in range(0, length, chunk_size):
                    chunk_len = min(chunk_size, length - i)
                    self.wfile.write(b"\x00" * chunk_len)
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query.startswith("/stream/"):
            # Stream response
            try:
                count = int(path_without_query.split("/")[-1])
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                for i in range(count):
                    line = json.dumps({"id": i, "data": f"line {i}"}) + "\n"
                    self.wfile.write(line.encode())
            except Exception:
                self.send_response(400)
                self.end_headers()

        elif path_without_query == "/cookies":
            # Return cookies
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            # Parse cookies from Cookie header
            cookie_header = self.headers.get("Cookie", "")
            cookies = {}
            if cookie_header:
                for item in cookie_header.split(";"):
                    item = item.strip()
                    if "=" in item:
                        key, value = item.split("=", 1)
                        cookies[key] = value
            response = {"cookies": cookies}
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query.startswith("/cookies/set"):
            # Set cookies and redirect to /cookies
            self.send_response(302)
            self.send_header("Location", "/cookies")

            # Parse query parameters or path segments for cookies to set
            if "?" in self.path:
                query = self.path.split("?", 1)[1]
                from urllib.parse import parse_qs

                params = parse_qs(query)
                for key, values in params.items():
                    if values:
                        self.send_header("Set-Cookie", f"{key}={values[0]}")
            elif "/" in path_without_query[len("/cookies/set") :]:
                # Format: /cookies/set/name/value
                parts = path_without_query[len("/cookies/set") :].strip("/").split("/")
                if len(parts) >= 2:
                    name, value = parts[0], parts[1]
                    self.send_header("Set-Cookie", f"{name}={value}")

            self.end_headers()

        elif path_without_query == "/basic-auth/user/pass" or path_without_query.startswith(
            "/basic-auth/"
        ):
            # Basic auth endpoint
            auth_header = self.headers.get("Authorization", "")

            # Extract expected username/password from path
            parts = path_without_query.split("/")
            expected_user = parts[2] if len(parts) > 2 else "user"
            expected_pass = parts[3] if len(parts) > 3 else "pass"

            # Check if auth is correct
            import base64

            expected_auth = base64.b64encode(f"{expected_user}:{expected_pass}".encode()).decode()

            if auth_header == f"Basic {expected_auth}":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                response = {"authenticated": True, "user": expected_user}
                self.wfile.write(json.dumps(response).encode())
            else:
                self.send_response(401)
                self.send_header("WWW-Authenticate", 'Basic realm="Fake Realm"')
                self.end_headers()

        elif path_without_query.startswith("/encoding/"):
            # Return content with specific encoding
            encoding_type = path_without_query.split("/")[-1]
            self.send_response(200)
            self.send_header("Content-Type", f"application/json; charset={encoding_type}")
            self.end_headers()
            response = {"encoding": encoding_type, "message": "Hello 世界"}
            # Encode based on requested encoding
            if encoding_type == "utf8" or encoding_type == "utf-8":
                self.wfile.write(json.dumps(response, ensure_ascii=False).encode("utf-8"))
            else:
                # Default to utf-8 for other encodings
                self.wfile.write(json.dumps(response, ensure_ascii=False).encode("utf-8"))

        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_POST(self):
        """Handle POST requests"""
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        # Strip query parameters for path matching
        path_without_query = self.path.split("?")[0]

        if path_without_query == "/post":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()

            # Parse multipart form-data if present
            content_type = self.headers.get("Content-Type", "")
            files = {}
            form_data = {}
            json_data = None
            data_str = ""

            if content_type.startswith("multipart/form-data"):
                # Extract boundary
                boundary = None
                for part in content_type.split(";"):
                    part = part.strip()
                    if part.startswith("boundary="):
                        boundary = part.split("=", 1)[1]
                        break

                if boundary:
                    # Parse multipart data
                    parts = body.split(f"--{boundary}".encode())
                    for part in parts:
                        if not part or part == b"--\r\n" or part == b"--":
                            continue

                        # Split headers and content
                        if b"\r\n\r\n" in part:
                            headers_section, content = part.split(b"\r\n\r\n", 1)
                            # Remove trailing \r\n
                            content = content.rstrip(b"\r\n")

                            # Parse Content-Disposition header
                            headers_text = headers_section.decode("utf-8", errors="ignore")
                            field_name = None
                            filename = None

                            for line in headers_text.split("\r\n"):
                                if line.startswith("Content-Disposition:"):
                                    # Parse field name and filename
                                    if 'name="' in line:
                                        name_start = line.index('name="') + 6
                                        name_end = line.index('"', name_start)
                                        field_name = line[name_start:name_end]

                                    if 'filename="' in line:
                                        fn_start = line.index('filename="') + 10
                                        fn_end = line.index('"', fn_start)
                                        filename = line[fn_start:fn_end]

                            if field_name:
                                if filename:
                                    # It's a file - httpbin format includes filename in value
                                    content_str = content.decode("utf-8", errors="ignore")
                                    files[field_name] = f"{filename}:{content_str}"
                                else:
                                    # It's form data
                                    form_data[field_name] = content.decode("utf-8", errors="ignore")
            else:
                # Try parsing as JSON, but always include raw body in data
                data_str = body.decode("utf-8", errors="replace") if body else ""
                try:
                    json_data = json.loads(data_str) if data_str else {}
                except Exception:
                    json_data = None

            response = {
                "method": "POST",
                "path": self.path,
                "headers": dict(self.headers),
                "data": data_str,
                "json": json_data,
                "files": files if files else {},
                "form": form_data if form_data else {},
            }
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query == "/post/form":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {"method": "POST", "form": body.decode() if body else ""}
            self.wfile.write(json.dumps(response).encode())

        elif path_without_query.startswith("/redirect-to"):
            # POST redirect that preserves method (307/308)
            # Extract query params for redirect URL
            if "?" in self.path:
                query = self.path.split("?", 1)[1]
                # Parse url parameter
                from urllib.parse import parse_qs

                params = parse_qs(query)
                redirect_url = params.get("url", ["/get"])[0]
                status_code = int(params.get("status_code", [307])[0])
            else:
                redirect_url = "/get"
                status_code = 307

            self.send_response(status_code)
            self.send_header("Location", redirect_url)
            self.end_headers()

        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_PUT(self):
        """Handle PUT requests"""
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        path_without_query = self.path.split("?")[0]

        if path_without_query == "/put":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()

            try:
                json_data = json.loads(body.decode()) if body else {}
            except Exception:
                json_data = None

            response = {
                "method": "PUT",
                "path": self.path,
                "headers": dict(self.headers),
                "data": body.decode() if body else "",
                "json": json_data,
            }
            self.wfile.write(json.dumps(response).encode())
        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_DELETE(self):
        """Handle DELETE requests"""
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        response = {"method": "DELETE", "path": self.path}
        self.wfile.write(json.dumps(response).encode())

    def do_HEAD(self):
        """Handle HEAD requests"""
        # HEAD is like GET but without body
        path_without_query = self.path.split("?")[0]

        if path_without_query == "/get" or path_without_query == "/head":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", "100")  # Approximate size
            self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()

    def do_PATCH(self):
        """Handle PATCH requests"""
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        path_without_query = self.path.split("?")[0]

        if path_without_query == "/patch":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()

            try:
                json_data = json.loads(body.decode()) if body else {}
            except Exception:
                json_data = None

            response = {
                "method": "PATCH",
                "path": self.path,
                "headers": dict(self.headers),
                "data": body.decode() if body else "",
                "json": json_data,
            }
            self.wfile.write(json.dumps(response).encode())
        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_OPTIONS(self):
        """Handle OPTIONS requests"""
        self.send_response(200)
        self.send_header("Allow", "GET, POST, PUT, DELETE, HEAD, PATCH, OPTIONS")
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        response = {"methods": ["GET", "POST", "PUT", "DELETE", "HEAD", "PATCH", "OPTIONS"]}
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
