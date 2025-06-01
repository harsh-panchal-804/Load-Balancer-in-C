#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>

#define MAX_BACKEND 3
#define LISTEN_PORT 3000
#define BUFFER_SIZE (1024 * 1024) /// 1Mb

const char *backends_ip[] = {
    "ec2-13-203-231-9.ap-south-1.compute.amazonaws.com",
    "ec2-13-203-231-9.ap-south-1.compute.amazonaws.com",
    "ec2-13-203-231-9.ap-south-1.compute.amazonaws.com"
};
const int backend_port[] = { 3001, 3002, 3003 };
int backend_index = 0;
pthread_mutex_t backend_mutex = PTHREAD_MUTEX_INITIALIZER;

int get_next_backend() {
    pthread_mutex_lock(&backend_mutex);
    int idx = backend_index;
    backend_index = (backend_index + 1) % MAX_BACKEND;
    pthread_mutex_unlock(&backend_mutex);
    return idx;
}

int connect_to_backend(int idx) {
    struct addrinfo hints, *res, *rp;
    char port_str[6];
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", backend_port[idx]);

    if (getaddrinfo(backends_ip[idx], port_str, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

typedef struct { int client_sock; } thread_arg;

void *handle_client(void *p) {
    thread_arg *arg = p;
    int client = arg->client_sock;
    free(arg);

    char *buf = malloc(BUFFER_SIZE);
    if (!buf) { close(client); return NULL; }

    // Read HTTP request until "\r\n\r\n"
    ssize_t req_len = 0, n;
    while ((n = recv(client, buf + req_len, BUFFER_SIZE - req_len, 0)) > 0) {
        req_len += n;
        if (req_len >= 4 && strstr(buf, "\r\n\r\n")) break;
        if (req_len >= BUFFER_SIZE) break;
    }
    if (n < 0 || req_len == 0) { free(buf); close(client); return NULL; }

    // Connect to backend
    int backend_idx = get_next_backend();
    int backend = connect_to_backend(backend_idx);
    if (backend < 0) { free(buf); close(client); return NULL; }

    // Forward request to backend
    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t w = send(backend, buf + sent, req_len - sent, 0);
        if (w <= 0) { close(backend); free(buf); close(client); return NULL; }
        sent += w;
    }
    ///shutdown(backend,SHUT_WR); this line wont allow integration with npx serve

    // Forward backend response to client
    while ((n = recv(backend, buf, BUFFER_SIZE, 0)) > 0) {
        ssize_t offset = 0;
        while (offset < n) {
            ssize_t w = send(client, buf + offset, n - offset, 0);
            if (w <= 0) break;
            offset += w;
        }
    }

    close(backend);
    free(buf);
    close(client);
    return NULL;
}

int main() {
    signal(SIGPIPE,SIG_IGN);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(LISTEN_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(listener, SOMAXCONN) < 0) {
        perror("bind/listen"); exit(1);
    }

    printf("Load balancer listening on port %d\n", LISTEN_PORT);

    while (1) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) continue;
        thread_arg *arg = malloc(sizeof(*arg));
        if (!arg) { close(client); continue; }
        arg->client_sock = client;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, arg) != 0) {
            close(client); free(arg); continue;
        }
        pthread_detach(tid);
    }
    close(listener);
    return 0;
}
