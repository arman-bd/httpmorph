# cython: language_level=3
"""
_http2.pyx - Simple Cython wrapper for HTTP/2 client
"""

from libc.stdint cimport uint8_t
from libc.stdlib cimport free

cdef extern from "http2_client.h":
    ctypedef struct http2_response_t:
        int status_code
        uint8_t *body
        size_t body_len
        int http_version

    http2_response_t *http2_get(const char *url)
    void http2_response_free(http2_response_t *response)


class HTTP2Response:
    """HTTP/2 response object"""
    def __init__(self, status_code, body, http_version):
        self.status_code = status_code
        self.body = body
        self.http_version = http_version
        self.text = body.decode('utf-8', errors='replace') if body else ''


def get(url):
    """
    Execute an HTTP/2 GET request

    Args:
        url: URL to request (must be https://)

    Returns:
        HTTP2Response object or None on error
    """
    if isinstance(url, str):
        url = url.encode('utf-8')

    cdef http2_response_t *response = http2_get(url)
    if response == NULL:
        return None

    # Extract data before freeing
    status_code = response.status_code
    http_version = response.http_version
    body = response.body[:response.body_len] if response.body else b''

    # Free C structure
    http2_response_free(response)

    return HTTP2Response(status_code, body, http_version)
