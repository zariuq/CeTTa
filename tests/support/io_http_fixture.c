#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    IO_FIXTURE_HEADER_LIMIT = 65536,
    IO_FIXTURE_BODY_LIMIT = 1048576,
};

typedef struct {
    int fd;
    const char *root;
} IoFixtureClient;

typedef struct {
    char method[16];
    char path[2048];
    unsigned char *body;
    size_t body_len;
} IoFixtureRequest;

static bool fixture_write_all(int fd, const void *data, size_t length) {
    const unsigned char *bytes = data;
    while (length > 0u) {
        ssize_t written = write(fd, bytes, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        bytes += (size_t)written;
        length -= (size_t)written;
    }
    return true;
}

static bool fixture_header(int fd, int status, const char *reason,
                           const char *content_type, size_t length,
                           const char *extra) {
    char header[4096];
    int count = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status, reason, content_type, length, extra ? extra : "");
    return count > 0 && (size_t)count < sizeof(header) &&
           fixture_write_all(fd, header, (size_t)count);
}

static void fixture_response(int fd, int status, const char *reason,
                             const char *content_type,
                             const void *body, size_t length,
                             const char *extra) {
    if (fixture_header(fd, status, reason, content_type, length, extra) &&
        length > 0u)
        (void)fixture_write_all(fd, body, length);
}

static bool fixture_parse_content_length(const char *headers,
                                         size_t *length_out) {
    const char *line = strstr(headers, "\r\n") + 2;
    *length_out = 0u;
  while (line && !(line[0] == '\r' && line[1] == '\n')) {
        const char *end = strstr(line, "\r\n");
        if (!end)
            return false;
        static const char key[] = "Content-Length:";
        if ((size_t)(end - line) >= sizeof(key) - 1u &&
            strncasecmp(line, key, sizeof(key) - 1u) == 0) {
            const char *value = line + sizeof(key) - 1u;
            while (value < end && (*value == ' ' || *value == '\t'))
                value++;
            if (value == end)
                return false;
            size_t parsed = 0u;
            while (value < end) {
                if (*value < '0' || *value > '9' ||
                    parsed > (SIZE_MAX - (size_t)(*value - '0')) / 10u)
                    return false;
                parsed = parsed * 10u + (size_t)(*value - '0');
                value++;
            }
            *length_out = parsed;
        }
        line = end + 2;
    }
    return true;
}

static bool fixture_read_request(int fd, IoFixtureRequest *request) {
    char headers[IO_FIXTURE_HEADER_LIMIT + 1u];
    size_t used = 0u;
    char *terminator = NULL;
    while (!terminator && used < IO_FIXTURE_HEADER_LIMIT) {
        ssize_t got = read(fd, headers + used, IO_FIXTURE_HEADER_LIMIT - used);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            return false;
        used += (size_t)got;
        headers[used] = '\0';
        terminator = strstr(headers, "\r\n\r\n");
    }
    if (!terminator)
        return false;
    char version[16];
    if (sscanf(headers, "%15s %2047s %15s", request->method,
               request->path, version) != 3 ||
        strncmp(version, "HTTP/", 5u) != 0)
        return false;
    char *query = strchr(request->path, '?');
    if (query)
        *query = '\0';

    size_t body_len = 0u;
    if (!fixture_parse_content_length(headers, &body_len) ||
        body_len > IO_FIXTURE_BODY_LIMIT)
        return false;
    request->body = malloc(body_len + 1u);
    if (!request->body)
        return false;
    request->body_len = body_len;
    size_t header_len = (size_t)(terminator + 4 - headers);
    size_t buffered = used - header_len;
    if (buffered > body_len)
        buffered = body_len;
    if (buffered > 0u)
        memcpy(request->body, headers + header_len, buffered);
    size_t body_used = buffered;
    while (body_used < body_len) {
        ssize_t got = read(fd, request->body + body_used,
                           body_len - body_used);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            free(request->body);
            request->body = NULL;
            return false;
        }
        body_used += (size_t)got;
    }
    request->body[body_len] = '\0';
    return true;
}

static bool fixture_decimal_suffix(const char *path, const char *prefix,
                                   long *value_out) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0)
        return false;
    char *end = NULL;
    errno = 0;
    long value = strtol(path + prefix_len, &end, 10);
    if (errno || end == path + prefix_len || *end != '\0')
        return false;
    *value_out = value;
    return true;
}

static const char *fixture_content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot && strcmp(dot, ".html") == 0)
        return "text/html; charset=utf-8";
    if (dot && strcmp(dot, ".js") == 0)
        return "text/javascript; charset=utf-8";
    if (dot && strcmp(dot, ".wasm") == 0)
        return "application/wasm";
    return "application/octet-stream";
}

static void fixture_static(int fd, const char *root, const char *path) {
    if (!root || strstr(path, "..")) {
        fixture_response(fd, 404, "Not Found", "text/plain", "not found",
                         9u, NULL);
        return;
    }
    const char *relative = strcmp(path, "/") == 0 ? "/index.html" : path;
    size_t needed = strlen(root) + strlen(relative) + 1u;
    char *filename = malloc(needed);
    if (!filename)
        return;
    snprintf(filename, needed, "%s%s", root, relative);
    struct stat statbuf;
    FILE *file = fopen(filename, "rb");
    if (!file || fstat(fileno(file), &statbuf) != 0 || statbuf.st_size < 0) {
        if (file)
            fclose(file);
        free(filename);
        fixture_response(fd, 404, "Not Found", "text/plain", "not found",
                         9u, NULL);
        return;
    }
    size_t length = (size_t)statbuf.st_size;
    if (!fixture_header(fd, 200, "OK", fixture_content_type(relative),
                        length, NULL)) {
        fclose(file);
        free(filename);
        return;
    }
    unsigned char buffer[16384];
    while (!feof(file)) {
        size_t got = fread(buffer, 1u, sizeof(buffer), file);
        if (got > 0u && !fixture_write_all(fd, buffer, got))
            break;
        if (got == 0u && ferror(file))
            break;
    }
    fclose(file);
    free(filename);
}

static void fixture_get(int fd, const char *root, const char *path) {
    if (strcmp(path, "/slow") == 0) {
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 200000000L};
        (void)nanosleep(&pause, NULL);
        fixture_response(fd, 200, "OK", "text/plain", "slow", 4u, NULL);
    } else if (strcmp(path, "/large") == 0) {
        static const char body[] = "response-larger-than-four-bytes";
        fixture_response(fd, 200, "OK", "text/plain", body,
                         sizeof(body) - 1u, NULL);
    } else if (strcmp(path, "/one") == 0) {
        fixture_response(fd, 200, "OK", "text/plain", "one", 3u, NULL);
    } else if (strcmp(path, "/two") == 0) {
        fixture_response(fd, 200, "OK", "text/plain", "two", 3u, NULL);
    } else if (strcmp(path, "/json") == 0) {
        static const char body[] = "{\"x\":1,\"x\":2}";
        fixture_response(fd, 200, "OK", "application/json", body,
                         sizeof(body) - 1u, NULL);
    } else if (strcmp(path, "/drip") == 0) {
        unsigned char chunk[1024];
        for (size_t index = 0u; index < sizeof(chunk); index++)
            chunk[index] = (unsigned char)("0123456789abcdef"[index % 16u]);
        if (!fixture_header(fd, 200, "OK", "application/octet-stream",
                            64u * sizeof(chunk), NULL))
            return;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 5000000L};
        for (size_t index = 0u; index < 64u; index++) {
            if (!fixture_write_all(fd, chunk, sizeof(chunk)))
                break;
            (void)nanosleep(&pause, NULL);
        }
    } else {
        long value = 0;
        if (fixture_decimal_suffix(path, "/bytes/", &value) &&
            value >= 0 && value <= IO_FIXTURE_BODY_LIMIT) {
            size_t length = (size_t)value;
            unsigned char *body = malloc(length ? length : 1u);
            if (!body)
                return;
            for (size_t index = 0u; index < length; index++)
                body[index] = (unsigned char)('a' + index % 26u);
            fixture_response(fd, 200, "OK", "text/plain", body, length, NULL);
            free(body);
        } else if (fixture_decimal_suffix(path, "/status/", &value) &&
                   value >= 100 && value <= 599) {
            char body[64];
            int count = snprintf(body, sizeof(body), "status-%ld", value);
            fixture_response(fd, (int)value, "Status", "text/plain", body,
                             count > 0 ? (size_t)count : 0u, NULL);
        } else if (fixture_decimal_suffix(path, "/redirect/", &value) &&
                   value > 0 && value <= 16) {
            char extra[128];
            if (value > 1)
                snprintf(extra, sizeof(extra), "Location: /redirect/%ld\r\n",
                         value - 1);
            else
                snprintf(extra, sizeof(extra), "Location: /one\r\n");
            fixture_response(fd, 302, "Found", "text/plain", "", 0u, extra);
        } else {
            fixture_static(fd, root, path);
        }
    }
}

static void *fixture_client_main(void *opaque) {
    IoFixtureClient *client = opaque;
    int fd = client->fd;
    const char *root = client->root;
    free(client);
    IoFixtureRequest request = {0};
    if (!fixture_read_request(fd, &request)) {
        fixture_response(fd, 400, "Bad Request", "text/plain", "bad request",
                         11u, NULL);
    } else if (strcmp(request.method, "GET") == 0) {
        fixture_get(fd, root, request.path);
    } else if (strcmp(request.method, "POST") == 0 &&
               strcmp(request.path, "/echo") == 0) {
        fixture_response(fd, 200, "OK", "text/plain", request.body,
                         request.body_len, NULL);
    } else {
        fixture_response(fd, 404, "Not Found", "text/plain", "not found",
                         9u, NULL);
    }
    free(request.body);
    close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    const char *root = NULL;
    if (argc == 3 && strcmp(argv[1], "--root") == 0)
        root = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--root DIRECTORY]\n", argv[0]);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    int reuse = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 256) != 0) {
        perror("bind/listen");
        close(server);
        return 1;
    }
    socklen_t address_len = sizeof(address);
    if (getsockname(server, (struct sockaddr *)&address, &address_len) != 0) {
        perror("getsockname");
        close(server);
        return 1;
    }
    printf("%u\n", (unsigned)ntohs(address.sin_port));
    fflush(stdout);

    for (;;) {
        int fd = accept(server, NULL, NULL);
        if (fd < 0 && errno == EINTR)
            continue;
        if (fd < 0) {
            perror("accept");
            break;
        }
        IoFixtureClient *client = malloc(sizeof(*client));
        if (!client) {
            close(fd);
            continue;
        }
        client->fd = fd;
        client->root = root;
        pthread_t thread;
        if (pthread_create(&thread, NULL, fixture_client_main, client) != 0) {
            close(fd);
            free(client);
            continue;
        }
        (void)pthread_detach(thread);
    }
    close(server);
    return 1;
}
