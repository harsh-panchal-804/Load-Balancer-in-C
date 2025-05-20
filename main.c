#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <errno.h>

#define MAX_BACKEND   3
#define LISTEN_PORT   3000
#define BUFFER_SIZE   (1024 * 1024)

const char *backends_ip[]   = { "13.203.231.9", "13.203.231.9", "13.203.231.9" };
const int   backend_port[]  = { 3001,             3002,             3003 };
int         backend_index   = MAX_BACKEND - 1;

void get_next_backend() {
    backend_index = (backend_index + 1) % MAX_BACKEND;
}

int connect_to_backend(int idx) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket()"); return -1; }
    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(backend_port[idx]);
    if (inet_pton(AF_INET, backends_ip[idx], &srv.sin_addr) <= 0) {
        perror("inet_pton()"); close(s); return -1;
    }
    if (connect(s, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect()"); close(s); return -1;
    }
    return s;
}

typedef struct { int client_sock; } thread_arg;

void *handle_client(void *p) {
    thread_arg *arg = p;
    int client = arg->client_sock;
    free(arg);

    fprintf(stderr, "Client fd=%d connected\n", client);

    // disable Nagle on client socket
    int one = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char *buf = malloc(BUFFER_SIZE);
    if (!buf) { close(client); return NULL; }

    // 1) Read request headers
    ssize_t req_len = 0, n;
    while ((n = recv(client, buf + req_len, BUFFER_SIZE - req_len, 0)) > 0) {
        req_len += n;
        if (strstr(buf, "\r\n\r\n")) break;
        if (req_len >= BUFFER_SIZE) break;
    }
    if (n < 0) { perror("recv(client)"); goto cleanup_client; }

    // 2) Connect and forward request to backend
    get_next_backend();
    int backend = connect_to_backend(backend_index);
    if (backend < 0) goto cleanup_client;
    setsockopt(backend, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t w = send(backend, buf + sent, req_len - sent, 0);
        if (w <= 0) { perror("send(backend)"); goto cleanup_both; }
        sent += w;
    }
    // signal end-of-request
    shutdown(backend, SHUT_WR);

    // 3) Buffer the entire backend response (header + body)
    char *resp_buf = NULL;
    size_t resp_cap = 0, resp_len = 0;
    while ((n = recv(backend, buf, BUFFER_SIZE, 0)) > 0) {
        if ((size_t)resp_len + n > resp_cap) {
            size_t new_cap = resp_cap ? resp_cap * 2 : n * 2;
            if (new_cap < (size_t)resp_len + n) new_cap = resp_len + n;
            char *p = realloc(resp_buf, new_cap);
            if (!p) { perror("realloc"); break; }
            resp_buf = p;
            resp_cap = new_cap;
        }
        memcpy(resp_buf + resp_len, buf, n);
        resp_len += n;
    }
    if (n < 0 && errno != ECONNRESET) perror("recv(backend)");

    // 4) Send the full buffered response to the client
    size_t offset = 0;
    while (offset < resp_len) {
        ssize_t w = send(client, resp_buf + offset, resp_len - offset, 0);
        if (w <= 0) { perror("send(client)"); break; }
        offset += w;
    }

    free(resp_buf);

cleanup_both:
    close(backend);
cleanup_client:
    free(buf);
    close(client);
    fprintf(stderr, "Connection fd=%d closed\n", client);
    return NULL;
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket()"); exit(1); }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(LISTEN_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(listener, SOMAXCONN) < 0) {
        perror("bind/listen"); exit(1);
    }

    fprintf(stderr, "Load-balancer listening on port %d\n", LISTEN_PORT);

    while (1) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) { perror("accept()"); continue; }
        thread_arg *arg = malloc(sizeof(*arg));
        arg->client_sock = client;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, arg) != 0) {
            perror("pthread_create()"); close(client); free(arg); continue;
        }
        pthread_detach(tid);
    }

    close(listener);
    return 0;
}
