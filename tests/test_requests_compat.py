"""
Test suite for requests library compatibility features

This module tests all features needed to make httpmorph a drop-in replacement
for the requests library.
"""

import json as json_module

import pytest

import httpmorph


class TestResponseJsonMethod:
    """Tests for Response.json() method"""

    def test_json_parsing_valid(self, httpbin_server):
        """Test json() method with valid JSON response"""
        response = httpmorph.get(f"{httpbin_server}/json")
        assert response.status_code == 200

        # Should be able to call .json()
        data = response.json()
        assert isinstance(data, dict)
        assert "slideshow" in data

    def test_json_parsing_empty(self, httpbin_server):
        """Test json() method with empty response"""
        # POST to endpoint that returns empty body
        response = httpmorph.post(f"{httpbin_server}/status/204")

        # Should raise JSONDecodeError for empty content
        with pytest.raises((json_module.JSONDecodeError, ValueError)):
            response.json()

    def test_json_parsing_invalid(self, httpbin_server):
        """Test json() method with invalid JSON"""
        response = httpmorph.get(f"{httpbin_server}/html")

        # HTML response should raise JSONDecodeError
        with pytest.raises((json_module.JSONDecodeError, ValueError)):
            response.json()

    def test_json_with_encoding(self, httpbin_server):
        """Test json() handles different encodings"""
        response = httpmorph.get(f"{httpbin_server}/encoding/utf8")
        assert response.status_code == 200

        # Should handle UTF-8 encoded JSON
        data = response.json()
        assert isinstance(data, (dict, str))


class TestResponseOkProperty:
    """Tests for Response.ok property"""

    def test_ok_for_2xx_status(self, httpbin_server):
        """Test .ok is True for 2xx status codes"""
        test_cases = [200, 201, 204]

        for expected_status in test_cases:
            response = httpmorph.get(f"{httpbin_server}/status/{expected_status}")
            assert response.status_code == expected_status
            assert response.ok is True, f"Expected .ok=True for status {expected_status}"

    def test_ok_for_3xx_status(self, httpbin_server):
        """Test .ok is True for 3xx redirects (before following)"""
        response = httpmorph.get(f"{httpbin_server}/status/302", allow_redirects=False)
        assert response.status_code == 302
        assert response.ok is True

    def test_not_ok_for_4xx_status(self, httpbin_server):
        """Test .ok is False for 4xx client errors"""
        test_cases = [400, 401, 403, 404, 429]

        for status in test_cases:
            response = httpmorph.get(f"{httpbin_server}/status/{status}")
            assert response.status_code == status
            assert response.ok is False, f"Expected .ok=False for status {status}"

    def test_not_ok_for_5xx_status(self, httpbin_server):
        """Test .ok is False for 5xx server errors"""
        test_cases = [500, 502, 503, 504]

        for status in test_cases:
            response = httpmorph.get(f"{httpbin_server}/status/{status}")
            assert response.status_code == status
            assert response.ok is False, f"Expected .ok=False for status {status}"


class TestResponseRaiseForStatus:
    """Tests for Response.raise_for_status() method"""

    def test_raise_for_status_success(self, httpbin_server):
        """Test raise_for_status() doesn't raise for 2xx"""
        response = httpmorph.get(f"{httpbin_server}/status/200")

        # Should not raise
        response.raise_for_status()  # No exception expected

    def test_raise_for_status_4xx(self, httpbin_server):
        """Test raise_for_status() raises HTTPError for 4xx"""
        response = httpmorph.get(f"{httpbin_server}/status/404")

        with pytest.raises(httpmorph.HTTPError) as exc_info:
            response.raise_for_status()

        assert "404" in str(exc_info.value)

    def test_raise_for_status_5xx(self, httpbin_server):
        """Test raise_for_status() raises HTTPError for 5xx"""
        response = httpmorph.get(f"{httpbin_server}/status/500")

        with pytest.raises(httpmorph.HTTPError) as exc_info:
            response.raise_for_status()

        assert "500" in str(exc_info.value)

    def test_raise_for_status_chain(self, httpbin_server):
        """Test raise_for_status() can be chained"""
        # Should work in method chain
        response = httpmorph.get(f"{httpbin_server}/json")
        data = response.raise_for_status().json()
        assert isinstance(data, dict)


class TestResponseReasonProperty:
    """Tests for Response.reason property"""

    def test_reason_for_common_statuses(self, httpbin_server):
        """Test .reason returns correct HTTP reason phrase"""
        test_cases = [
            (200, "OK"),
            (201, "Created"),
            (204, "No Content"),
            (301, "Moved Permanently"),
            (302, "Found"),
            (400, "Bad Request"),
            (401, "Unauthorized"),
            (403, "Forbidden"),
            (404, "Not Found"),
            (500, "Internal Server Error"),
            (502, "Bad Gateway"),
            (503, "Service Unavailable"),
        ]

        for status, expected_reason in test_cases:
            # Disable redirects for 3xx codes to check the reason phrase
            allow_redirects = status < 300 or status >= 400
            response = httpmorph.get(
                f"{httpbin_server}/status/{status}", allow_redirects=allow_redirects
            )
            assert response.status_code == status
            assert response.reason == expected_reason


class TestResponseContentProperty:
    """Tests for Response.content property (alias for .body)"""

    def test_content_is_bytes(self, httpbin_server):
        """Test .content returns bytes"""
        response = httpmorph.get(f"{httpbin_server}/bytes/100")

        assert hasattr(response, "content")
        assert isinstance(response.content, bytes)
        assert len(response.content) == 100

    def test_content_equals_body(self, httpbin_server):
        """Test .content is same as .body"""
        response = httpmorph.get(f"{httpbin_server}/get")

        assert response.content == response.body


class TestResponseUrlProperty:
    """Tests for Response.url property"""

    def test_url_property_exists(self, httpbin_server):
        """Test response has .url property"""
        url = f"{httpbin_server}/get"
        response = httpmorph.get(url)

        assert hasattr(response, "url")
        assert response.url == url

    def test_url_after_redirect(self, httpbin_server):
        """Test .url shows final URL after redirects"""
        response = httpmorph.get(f"{httpbin_server}/redirect/1")

        # Should be the final URL after redirect
        assert response.url == f"{httpbin_server}/get"


class TestResponseElapsedProperty:
    """Tests for Response.elapsed property"""

    def test_elapsed_is_timedelta(self, httpbin_server):
        """Test .elapsed returns timedelta object"""
        from datetime import timedelta

        response = httpmorph.get(f"{httpbin_server}/delay/1")

        assert hasattr(response, "elapsed")
        assert isinstance(response.elapsed, timedelta)
        assert response.elapsed.total_seconds() >= 1.0


class TestJsonParameter:
    """Tests for json= parameter in requests"""

    def test_json_parameter_dict(self, httpbin_server):
        """Test json= parameter with dict"""
        data = {"name": "John", "age": 30, "city": "New York"}

        response = httpmorph.post(f"{httpbin_server}/post", json=data)
        assert response.status_code == 200

        response_data = response.json()
        assert response_data["json"] == data

    def test_json_parameter_list(self, httpbin_server):
        """Test json= parameter with list"""
        data = [1, 2, 3, 4, 5]

        response = httpmorph.post(f"{httpbin_server}/post", json=data)
        response_data = response.json()
        assert response_data["json"] == data

    def test_json_parameter_nested(self, httpbin_server):
        """Test json= parameter with nested structures"""
        data = {"user": {"name": "Alice", "metadata": {"age": 25, "tags": ["python", "developer"]}}}

        response = httpmorph.post(f"{httpbin_server}/post", json=data)
        response_data = response.json()
        assert response_data["json"] == data

    def test_json_sets_content_type(self, httpbin_server):
        """Test json= parameter sets Content-Type header"""
        response = httpmorph.post(f"{httpbin_server}/post", json={"test": "value"})

        response_data = response.json()
        headers = response_data["headers"]
        assert "application/json" in headers.get("Content-Type", "")

    def test_json_with_put_request(self, httpbin_server):
        """Test json= works with PUT requests"""
        data = {"updated": True}

        response = httpmorph.put(f"{httpbin_server}/put", json=data)
        response_data = response.json()
        assert response_data["json"] == data

    def test_json_with_patch_request(self, httpbin_server):
        """Test json= works with PATCH requests"""
        data = {"patch": "value"}

        response = httpmorph.patch(f"{httpbin_server}/patch", json=data)
        response_data = response.json()
        assert response_data["json"] == data


class TestParamsParameter:
    """Tests for params= parameter in requests"""

    def test_params_dict(self, httpbin_server):
        """Test params= parameter with dict"""
        params = {"key1": "value1", "key2": "value2"}

        response = httpmorph.get(f"{httpbin_server}/get", params=params)
        response_data = response.json()

        assert response_data["args"]["key1"] == "value1"
        assert response_data["args"]["key2"] == "value2"

    def test_params_with_special_chars(self, httpbin_server):
        """Test params= handles URL encoding"""
        params = {"search": "hello world", "filter": "type=a&b"}

        response = httpmorph.get(f"{httpbin_server}/get", params=params)
        response_data = response.json()

        assert response_data["args"]["search"] == "hello world"
        assert response_data["args"]["filter"] == "type=a&b"

    def test_params_with_list_values(self, httpbin_server):
        """Test params= with list values"""
        params = {"tags": ["python", "http", "client"]}

        response = httpmorph.get(f"{httpbin_server}/get", params=params)
        response_data = response.json()

        # Should handle list as multiple params or comma-separated
        assert "tags" in response_data["args"]

    def test_params_with_none_values(self, httpbin_server):
        """Test params= ignores None values"""
        params = {"key1": "value1", "key2": None, "key3": "value3"}

        response = httpmorph.get(f"{httpbin_server}/get", params=params)
        response_data = response.json()

        assert "key1" in response_data["args"]
        assert "key2" not in response_data["args"]  # None should be omitted
        assert "key3" in response_data["args"]

    def test_params_appends_to_existing_query(self, httpbin_server):
        """Test params= appends to existing query string"""
        response = httpmorph.get(f"{httpbin_server}/get?existing=param", params={"new": "param"})
        response_data = response.json()

        assert response_data["args"]["existing"] == "param"
        assert response_data["args"]["new"] == "param"


class TestHttpMethods:
    """Tests for HEAD, PATCH, OPTIONS methods"""

    def test_head_request(self, httpbin_server):
        """Test HEAD request"""
        response = httpmorph.head(f"{httpbin_server}/get")

        assert response.status_code == 200
        # HEAD should not have body
        assert len(response.body) == 0 or response.body is None

    def test_head_returns_headers(self, httpbin_server):
        """Test HEAD request returns headers"""
        response = httpmorph.head(f"{httpbin_server}/get")

        assert response.headers is not None
        assert "Content-Type" in response.headers

    def test_patch_request(self, httpbin_server):
        """Test PATCH request"""
        data = {"field": "updated"}

        response = httpmorph.patch(f"{httpbin_server}/patch", json=data)
        assert response.status_code == 200

        response_data = response.json()
        assert response_data["json"] == data

    def test_options_request(self, httpbin_server):
        """Test OPTIONS request"""
        response = httpmorph.options(f"{httpbin_server}/get")

        assert response.status_code == 200
        # OPTIONS should return allowed methods in headers
        assert "Allow" in response.headers or "allow" in response.headers


class TestExceptionHierarchy:
    """Tests for exception compatibility"""

    def test_exception_classes_exist(self, httpbin_server):
        """Test all exception classes are defined"""
        assert hasattr(httpmorph, "RequestException")
        assert hasattr(httpmorph, "HTTPError")
        assert hasattr(httpmorph, "ConnectionError")
        assert hasattr(httpmorph, "Timeout")
        assert hasattr(httpmorph, "TooManyRedirects")

    def test_exception_inheritance(self, httpbin_server):
        """Test exception inheritance chain"""
        # All should inherit from RequestException
        assert issubclass(httpmorph.HTTPError, httpmorph.RequestException)
        assert issubclass(httpmorph.ConnectionError, httpmorph.RequestException)
        assert issubclass(httpmorph.Timeout, httpmorph.RequestException)

    def test_timeout_exception(self, httpbin_server):
        """Test Timeout exception is raised"""
        with pytest.raises(httpmorph.Timeout):
            # Very short timeout should fail
            httpmorph.get(f"{httpbin_server}/delay/10", timeout=0.1)

    def test_connection_error_exception(self, httpbin_server):
        """Test ConnectionError for unreachable host"""
        with pytest.raises(httpmorph.ConnectionError):
            httpmorph.get("http://invalid-host-that-does-not-exist-12345.com")

    def test_http_error_from_raise_for_status(self, httpbin_server):
        """Test HTTPError from raise_for_status()"""
        response = httpmorph.get(f"{httpbin_server}/status/404")

        with pytest.raises(httpmorph.HTTPError):
            response.raise_for_status()


class TestCookiesParameter:
    """Tests for cookies= parameter"""

    def test_cookies_dict(self, httpbin_server):
        """Test cookies= parameter with dict"""
        cookies = {"session_id": "abc123", "user_token": "xyz789"}

        response = httpmorph.get(f"{httpbin_server}/cookies", cookies=cookies)
        response_data = response.json()

        assert response_data["cookies"]["session_id"] == "abc123"
        assert response_data["cookies"]["user_token"] == "xyz789"

    def test_cookies_sent_in_header(self, httpbin_server):
        """Test cookies are sent in Cookie header"""
        cookies = {"test": "value"}

        response = httpmorph.get(f"{httpbin_server}/headers", cookies=cookies)
        response_data = response.json()

        assert "Cookie" in response_data["headers"]
        assert "test=value" in response_data["headers"]["Cookie"]


class TestAuthParameter:
    """Tests for auth= parameter"""

    def test_basic_auth_tuple(self, httpbin_server):
        """Test auth= with (username, password) tuple"""
        auth = ("user", "pass")

        response = httpmorph.get(f"{httpbin_server}/basic-auth/user/pass", auth=auth)
        assert response.status_code == 200

    def test_basic_auth_failure(self, httpbin_server):
        """Test auth= with wrong credentials"""
        auth = ("wrong", "credentials")

        response = httpmorph.get(f"{httpbin_server}/basic-auth/user/pass", auth=auth)
        assert response.status_code == 401

    def test_auth_sets_header(self, httpbin_server):
        """Test auth= sets Authorization header"""
        auth = ("testuser", "testpass")

        response = httpmorph.get(f"{httpbin_server}/headers", auth=auth)
        response_data = response.json()

        assert "Authorization" in response_data["headers"]
        assert "Basic" in response_data["headers"]["Authorization"]


class TestSessionHeadersPersistence:
    """Tests for Session.headers persistence"""

    def test_session_headers_persist(self, httpbin_server):
        """Test headers set on session persist across requests"""
        session = httpmorph.Session()
        session.headers = {"X-Custom-Header": "test-value"}

        # First request
        response1 = session.get(f"{httpbin_server}/headers")
        data1 = response1.json()
        assert data1["headers"]["X-Custom-Header"] == "test-value"

        # Second request should still have the header
        response2 = session.get(f"{httpbin_server}/headers")
        data2 = response2.json()
        assert data2["headers"]["X-Custom-Header"] == "test-value"

    def test_session_headers_update(self, httpbin_server):
        """Test session headers can be updated"""
        session = httpmorph.Session()
        session.headers = {"Header1": "value1"}

        # Update headers
        session.headers["Header2"] = "value2"

        response = session.get(f"{httpbin_server}/headers")
        data = response.json()

        assert data["headers"]["Header1"] == "value1"
        assert data["headers"]["Header2"] == "value2"

    def test_request_headers_override_session(self, httpbin_server):
        """Test per-request headers override session headers"""
        session = httpmorph.Session()
        session.headers = {"User-Agent": "SessionAgent"}

        response = session.get(f"{httpbin_server}/headers", headers={"User-Agent": "RequestAgent"})
        data = response.json()

        # Request header should override session header
        assert data["headers"]["User-Agent"] == "RequestAgent"


class TestSessionCookiesPersistence:
    """Tests for Session cookie persistence"""

    def test_session_cookies_persist(self, httpbin_server):
        """Test cookies are persisted across requests in session"""
        session = httpmorph.Session()

        # Set a cookie
        session.get(f"{httpbin_server}/cookies/set/test/value")

        # Cookie should be sent in next request
        response = session.get(f"{httpbin_server}/cookies")
        data = response.json()

        assert data["cookies"]["test"] == "value"

    def test_session_receives_set_cookie(self, httpbin_server):
        """Test session stores cookies from Set-Cookie header"""
        session = httpmorph.Session()

        # Server sets a cookie
        session.get(f"{httpbin_server}/cookies/set?mycookie=myvalue")

        # Check if cookie was stored
        assert "mycookie" in session.cookies
        assert session.cookies["mycookie"] == "myvalue"


class TestAllowRedirectsParameter:
    """Tests for allow_redirects= parameter"""

    def test_redirects_followed_by_default(self, httpbin_server):
        """Test redirects are followed by default"""
        response = httpmorph.get(f"{httpbin_server}/redirect/1")

        assert response.status_code == 200
        assert response.url == f"{httpbin_server}/get"

    def test_redirects_not_followed_when_false(self, httpbin_server):
        """Test redirects not followed when allow_redirects=False"""
        response = httpmorph.get(f"{httpbin_server}/redirect/1", allow_redirects=False)

        assert response.status_code in (301, 302, 303, 307, 308)
        assert "Location" in response.headers

    def test_redirect_history(self, httpbin_server):
        """Test response.history contains redirect chain"""
        response = httpmorph.get(f"{httpbin_server}/redirect/3")

        assert len(response.history) == 3
        for r in response.history:
            assert r.status_code in (301, 302, 303, 307, 308)


class TestTimeoutParameter:
    """Tests for timeout parameter"""

    def test_timeout_connect_and_read(self, httpbin_server):
        """Test timeout as tuple (connect, read)"""
        # Should succeed with reasonable timeout
        response = httpmorph.get(f"{httpbin_server}/get", timeout=(5, 10))
        assert response.status_code == 200

    def test_timeout_single_value(self, httpbin_server):
        """Test timeout as single value"""
        response = httpmorph.get(f"{httpbin_server}/get", timeout=10)
        assert response.status_code == 200

    def test_timeout_raises_exception(self, httpbin_server):
        """Test timeout raises Timeout exception"""
        with pytest.raises(httpmorph.Timeout):
            httpmorph.get(f"{httpbin_server}/delay/10", timeout=0.5)


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"])
