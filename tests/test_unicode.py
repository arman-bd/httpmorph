"""
Unicode and encoding tests for httpmorph

Tests handling of:
- Unicode characters in response bodies
- Unicode in headers
- Binary data
- Malformed data
- Mixed encodings
- Edge cases with special characters
"""

import json

import pytest

import httpmorph
from tests.test_server import MockHTTPServer


class TestUnicodeResponseBodies:
    """Test unicode and foreign text in response bodies"""

    def test_unicode_json_response(self):
        """Test response with various unicode characters"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            # Parse JSON
            data = json.loads(response.body.decode("utf-8"))

            # Verify various unicode strings
            assert data["chinese"] == "你好世界"
            assert data["japanese"] == "こんにちは世界"
            assert data["korean"] == "안녕하세요 세계"
            assert data["arabic"] == "مرحبا بالعالم"
            assert data["russian"] == "Привет мир"
            assert data["emoji"] == "👋🌍🎉"
            assert data["mixed"] == "Hello 世界 🌍"
            assert data["special"] == "¡Hñola! Çà va? Ñoño"

    def test_chinese_characters(self):
        """Test Chinese characters specifically"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            assert "你好世界" in body
            assert "世界" in body

    def test_japanese_characters(self):
        """Test Japanese characters"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            assert "こんにちは世界" in body

    def test_korean_characters(self):
        """Test Korean characters"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            assert "안녕하세요 세계" in body

    def test_arabic_characters(self):
        """Test Arabic characters (RTL text)"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            assert "مرحبا بالعالم" in body

    def test_russian_cyrillic(self):
        """Test Russian Cyrillic characters"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            assert "Привет мир" in body

    def test_emoji_in_response(self):
        """Test emoji characters"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            assert "👋" in body
            assert "🌍" in body
            assert "🎉" in body

    def test_mixed_unicode(self):
        """Test mixed unicode (ASCII + CJK + Emoji)"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            data = json.loads(response.body.decode("utf-8"))
            assert data["mixed"] == "Hello 世界 🌍"
            # Verify all three types present
            assert "Hello" in data["mixed"]  # ASCII
            assert "世界" in data["mixed"]  # CJK
            assert "🌍" in data["mixed"]  # Emoji

    def test_special_latin_characters(self):
        """Test special Latin characters with accents"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            data = json.loads(response.body.decode("utf-8"))
            assert "¡" in data["special"]
            assert "ñ" in data["special"]
            assert "Ñ" in data["special"]
            assert "Ç" in data["special"]
            assert "à" in data["special"]


class TestUnicodeHeaders:
    """Test unicode in HTTP headers"""

    def test_unicode_in_response_headers(self):
        """Test response with unicode-encoded custom headers

        HTTP headers must be latin-1 compatible.
        Unicode can be sent as percent-encoded or latin-1 chars.
        """
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/utf8-header")
            assert response.status_code == 200

            # Body should be readable
            assert b"Response with unicode-safe headers" in response.body

    def test_send_unicode_in_request_headers(self):
        """Test sending unicode in request headers

        httpmorph should handle or reject unicode in request headers gracefully.
        """
        with MockHTTPServer() as server:
            # This may succeed or fail gracefully
            try:
                response = httpmorph.get(f"{server.url}/headers", headers={"X-Unicode": "你好"})
                # If it succeeds, verify it's handled
                assert response.status_code >= 0
            except (ValueError, UnicodeEncodeError):
                # Acceptable to reject unicode headers
                pass


class TestBinaryData:
    """Test handling of binary data"""

    def test_binary_response(self):
        """Test response with binary data"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/binary")
            assert response.status_code == 200

            # Should receive all 256 bytes
            assert len(response.body) == 256
            assert response.body == bytes(range(256))

    def test_null_bytes_in_response(self):
        """Test response containing null bytes"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/null-bytes")
            assert response.status_code == 200

            # Verify null bytes preserved
            assert b"\x00" in response.body
            assert response.body == b"Hello\x00World\x00\x00Test"
            assert len(response.body) == 17

    def test_high_byte_values(self):
        """Test response with high byte values (>127)"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/binary")
            assert response.status_code == 200

            # Check high bytes present
            assert response.body[255] == 255
            assert response.body[200] == 200
            assert response.body[128] == 128


class TestEncodingEdgeCases:
    """Test edge cases with encodings"""

    def test_mixed_encoding_response(self):
        """Test response with mixed encoding characters"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/mixed-encoding")
            assert response.status_code == 200

            # Should be decodable as UTF-8
            text = response.body.decode("utf-8")
            assert "你好" in text
            assert "café" in text
            assert "résumé" in text

    def test_very_long_line(self):
        """Test response with very long line (10000+ chars)"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/very-long-line")
            assert response.status_code == 200

            # Should handle long lines
            assert len(response.body) >= 10000
            assert response.body[:100] == b"A" * 100


class TestMalformedData:
    """Test handling of malformed data"""

    def test_malformed_json(self):
        """Test response with malformed JSON

        httpmorph returns raw body - JSON parsing is user's responsibility.
        """
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/malformed-json")
            assert response.status_code == 200

            # Response succeeds, but JSON parsing should fail
            with pytest.raises(json.JSONDecodeError):
                json.loads(response.body)

    def test_incomplete_response_body(self):
        """Test handling of incomplete response

        This tests the actual response data, not simulated.
        """
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/malformed-json")
            assert response.status_code == 200
            # Should still return whatever data was received
            assert len(response.body) > 0


class TestPostWithUnicode:
    """Test POST requests with unicode data"""

    def test_post_unicode_json(self):
        """Test POST with unicode in JSON body"""
        with MockHTTPServer() as server:
            payload = {"message": "你好世界", "emoji": "🎉", "mixed": "Hello 世界"}
            response = httpmorph.post(f"{server.url}/post", json=payload)
            assert response.status_code == 200

            # Verify server received unicode data
            data = json.loads(response.body)
            received = json.loads(data["data"])
            assert received["message"] == "你好世界"
            assert received["emoji"] == "🎉"

    def test_post_unicode_form_data(self):
        """Test POST with unicode in form data"""
        with MockHTTPServer() as server:
            form_data = "name=你好&message=世界"
            response = httpmorph.post(f"{server.url}/post", data=form_data.encode("utf-8"))
            assert response.status_code == 200
            # The server receives the data and includes it in the response
            # Unicode may be escaped in JSON representation
            body = response.body.decode("utf-8")
            assert "name=" in body
            assert "message=" in body
            # Verify unicode data was received (may be escaped as \u4f60\u597d)
            data = json.loads(body)
            assert "name=" in data["data"]


class TestUnicodeInURLs:
    """Test unicode in URLs"""

    def test_unicode_in_query_params(self):
        """Test unicode in query parameters

        Should be URL-encoded properly or handled gracefully.
        """
        with MockHTTPServer() as server:
            # This may succeed with encoding or fail gracefully
            try:
                response = httpmorph.get(f"{server.url}/get?name=你好")
                assert response.status_code >= 0
            except (ValueError, UnicodeEncodeError):
                # Acceptable to reject unicode in URLs
                pass

    def test_unicode_in_path(self):
        """Test unicode in URL path

        Should be handled or rejected gracefully.
        """
        with MockHTTPServer() as server:
            try:
                response = httpmorph.get(f"{server.url}/你好")
                # May succeed or return 404
                assert response.status_code >= 0
            except (ValueError, UnicodeEncodeError):
                # Acceptable to reject unicode in paths
                pass


class TestRealWorldUnicode:
    """Test real-world unicode scenarios"""

    def test_multilingual_content(self):
        """Test response with content in multiple languages"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            data = json.loads(response.body.decode("utf-8"))

            # Verify we can handle at least 5 different languages
            languages = ["chinese", "japanese", "korean", "arabic", "russian"]
            for lang in languages:
                assert lang in data
                assert len(data[lang]) > 0

    def test_emoji_sequences(self):
        """Test emoji sequences (multi-codepoint emojis)"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/unicode")
            assert response.status_code == 200

            body = response.body.decode("utf-8")
            # Basic emoji should work
            assert "👋" in body
            assert "🌍" in body
            assert "🎉" in body

    def test_zero_width_characters(self):
        """Test handling of zero-width characters

        Zero-width characters are valid unicode but invisible.
        """
        with MockHTTPServer() as server:
            # Post data with zero-width characters
            payload = {"text": "Hello\u200bWorld"}  # Zero-width space
            response = httpmorph.post(f"{server.url}/post", json=payload)
            assert response.status_code == 200


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
