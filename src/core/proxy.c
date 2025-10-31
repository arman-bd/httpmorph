/**
 * proxy.c - HTTP proxy handling
 */

#include "internal/proxy.h"
#include "internal/util.h"

/**
 * Parse a proxy URL
 */
int httpmorph_parse_proxy_url(const char *proxy_url, char **host, uint16_t *port,
                               char **username, char **password, bool *use_tls) {
    if (!proxy_url || !host || !port) {
        return -1;
    }

    *host = NULL;
    *port = 0;
    if (username) *username = NULL;
    if (password) *password = NULL;
    if (use_tls) *use_tls = false;

    /* Parse proxy URL format: [http://][username:password@]host:port */
    const char *start = proxy_url;

    /* Skip http:// or https:// and detect if proxy uses TLS */
    if (strncmp(start, "http://", 7) == 0) {
        start += 7;
        if (use_tls) *use_tls = false;
    } else if (strncmp(start, "https://", 8) == 0) {
        start += 8;
        if (use_tls) *use_tls = true;
    }

    /* Check for username:password@ */
    const char *at_sign = strchr(start, '@');
    if (at_sign && username && password) {
        const char *colon = strchr(start, ':');
        if (colon && colon < at_sign) {
            size_t user_len = colon - start;
            size_t pass_len = at_sign - colon - 1;

            *username = strndup(start, user_len);
            *password = strndup(colon + 1, pass_len);

            start = at_sign + 1;
        }
    }

    /* Parse host:port */
    const char *colon = strchr(start, ':');
    if (colon) {
        size_t host_len = colon - start;
        *host = strndup(start, host_len);
        *port = (uint16_t)atoi(colon + 1);
    } else {
        *host = strdup(start);
        *port = 8080;  /* Default proxy port */
    }

    return (*host != NULL) ? 0 : -1;
}

/**
 * Send HTTP CONNECT request to establish tunnel through proxy
 */
int httpmorph_proxy_connect(int sockfd, SSL *proxy_ssl, const char *target_host,
                             uint16_t target_port, const char *proxy_username,
                             const char *proxy_password, uint32_t timeout_ms) {
    char connect_req[2048];
    int len;

    /* Build CONNECT request */
    len = snprintf(connect_req, sizeof(connect_req),
                   "CONNECT %s:%u HTTP/1.1\r\n"
                   "Host: %s:%u\r\n",
                   target_host, target_port, target_host, target_port);

    /* Add Proxy-Authorization if credentials provided */
    if (proxy_username && proxy_password) {
        char credentials[512];
        snprintf(credentials, sizeof(credentials), "%s:%s", proxy_username, proxy_password);

        char *encoded = httpmorph_base64_encode(credentials, strlen(credentials));
        if (encoded) {
            len += snprintf(connect_req + len, sizeof(connect_req) - len,
                          "Proxy-Authorization: Basic %s\r\n", encoded);
            free(encoded);
        }
    }

    /* End headers */
    len += snprintf(connect_req + len, sizeof(connect_req) - len, "\r\n");

    /* Send CONNECT request - use SSL if proxy uses TLS */
    ssize_t sent;
    if (proxy_ssl) {
        sent = SSL_write(proxy_ssl, connect_req, len);
    } else {
        sent = send(sockfd, connect_req, len, 0);
    }
    if (sent != len) {
        return -1;
    }

    /* Read response - use SSL if proxy uses TLS */
    char response[4096];
    ssize_t received;
    if (proxy_ssl) {
        received = SSL_read(proxy_ssl, response, sizeof(response) - 1);
    } else {
        received = recv(sockfd, response, sizeof(response) - 1, 0);
    }
    if (received <= 0) {
        return -1;
    }
    response[received] = '\0';

    /* Check for 200 Connection established */
    if (strncmp(response, "HTTP/1", 6) == 0) {
        char *space = strchr(response, ' ');
        if (space) {
            int status = atoi(space + 1);
            if (status == 200) {
                return 0;  /* Success */
            }
        }
    }

    return -1;  /* Proxy connection failed */
}
