#include "lwip_wrapper.hpp"
#include "../utils/logger.hpp"

#ifdef HAVE_LWIP
#include <lwip/api.h>
#include <lwip/netdb.h>
#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include "../filesystem/fat32.hpp"
#include "../mem/pma.hpp"

// A conservative cap for fetched responses: 1 MiB
#define HTTP_FETCH_MAX_SIZE (1024 * 1024)

static int parse_url(const char* url, char* host, size_t host_sz, uint16_t* port, char* path, size_t path_sz) {
    if (!url || !host || !port || !path) return -1;
    const char* p = url;
    const char* scheme = strstr(p, "://");
    if (!scheme) return -1;
    p = scheme + 3;
    size_t hi = 0;
    while (*p && *p != ':' && *p != '/' && hi + 1 < host_sz) host[hi++] = *p++;
    host[hi] = '\0';
    *port = 80;
    if (*p == ':') {
        ++p;
        unsigned long v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        if (v > 0 && v < 65535) *port = (uint16_t)v;
    }
    if (*p == '/') ++p;
    size_t pi = 0;
    while (*p && pi + 1 < path_sz) path[pi++] = *p++;
    path[pi] = '\0';
    return 0;
}

extern "C" int http_fetch_via_lwip(const char* url, const char* out_path) {
    if (!url || !out_path) return -1;
    char host[128]; char path[512]; uint16_t port = 80;
    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        hanacore::utils::log_info_cpp("lwip: parse_url failed for %s", url);
        return -1;
    }

    ip_addr_t addr;
    err_t r = netconn_gethostbyname(host, &addr);
    if (r != ERR_OK) {
        hanacore::utils::log_info_cpp("lwip: DNS lookup failed for %s", host);
        return -1;
    }

    struct netconn* conn = netconn_new(NETCONN_TCP);
    if (!conn) return -1;
    r = netconn_connect(conn, &addr, port);
    if (r != ERR_OK) { netconn_delete(conn); return -1; }

    // Send GET request (HTTP/1.0 to simplify)
    char req[1024]; int reqn = snprintf(req, sizeof(req), "GET /%s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (reqn <= 0) { netconn_close(conn); netconn_delete(conn); return -1; }
    netconn_write(conn, req, reqn, NETCONN_COPY);

    // Allocate buffer from PMA (page-aligned allocation)
    size_t pages = (HTTP_FETCH_MAX_SIZE + 0xFFF) / 0x1000;
    void* buf = pma_alloc_pages(pages);
    if (!buf) { netconn_close(conn); netconn_delete(conn); return -1; }
    size_t cap = pages * 0x1000;
    size_t off = 0;

    struct netbuf* inbuf = NULL;
    while (netconn_recv(conn, &inbuf) == ERR_OK && inbuf != NULL) {
        void* data; u16_t len;
        netbuf_data(inbuf, &data, &len);
        if (off + len > cap) {
            netbuf_delete(inbuf); break; // overflow
        }
        memcpy((uint8_t*)buf + off, data, len);
        off += len;
        netbuf_delete(inbuf);
    }

    netconn_close(conn);
    netconn_delete(conn);

    if (off == 0) return -1;

    // Strip HTTP headers: find "\r\n\r\n"
    size_t header_end = 0;
    for (size_t i = 0; i + 3 < off; ++i) {
        if (((char*)buf)[i] == '\r' && ((char*)buf)[i+1] == '\n' && ((char*)buf)[i+2] == '\r' && ((char*)buf)[i+3] == '\n') { header_end = i + 4; break; }
    }
    const void* body = (header_end < off) ? ((uint8_t*)buf + header_end) : buf;
    size_t body_len = (header_end < off) ? (off - header_end) : off;

    int rc = hanacore::fs::fat32_write_file(out_path, body, body_len);
    return rc;
}
#else
// Default stub implementation when lwIP is not linked in. This keeps the
// symbol available for callers; CMake may define HAVE_LWIP and provide a real
// implementation of `http_fetch_via_lwip`.
extern "C" int http_fetch_via_lwip(const char* url, const char* out_path) {
    (void)url; (void)out_path;
    hanacore::utils::log_info_cpp("lwip: not available in this build");
    return -1;
}
#endif


