#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <errno.h>
#include <netdb.h>

#define MAX_BACKEND   3
#define LISTEN_PORT   3000
#define BUFFER_SIZE   (1024 * 1024)

const char *backends_ip[]   = { 
    "ec2-13-203-231-9.ap-south-1.compute.amazonaws.com",
    "ec2-13-203-231-9.ap-south-1.compute.amazonaws.com",
    "ec2-13-203-231-9.ap-south-1.compute.amazonaws.com" 
};
const int   backend_port[]  = { 3001, 3002, 3003 };
int         backend_index   = MAX_BACKEND - 1;

/// global mutex
pthread_mutex_t backend_mutex = PTHREAD_MUTEX_INITIALIZER;

void get_next_backend() {
    pthread_mutex_lock(&backend_mutex);
    backend_index = (backend_index + 1) % MAX_BACKEND;
    pthread_mutex_unlock(&backend_mutex);
}

int connect_to_backend(int idx) {
    struct addrinfo hints, *res, *rp;
    char port_str[6];
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // permit both ipv4 and ipv6
    hints.ai_socktype = SOCK_STREAM; 

    snprintf(port_str, sizeof(port_str), "%d", backend_port[idx]);

    if (getaddrinfo(backends_ip[idx], port_str, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock < 0) {
        fprintf(stderr, "Could not connect to backend %s:%s\n", backends_ip[idx], port_str);
    }
    return sock;
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

    ssize_t req_len = 0, n;
    while ((n = recv(client, buf + req_len, BUFFER_SIZE - req_len, 0)) > 0) {
        req_len += n;
        if (strstr(buf, "\r\n\r\n")) break;
        if (req_len >= BUFFER_SIZE) break;
    }
    if (n < 0) { perror("recv(client)"); goto cleanup_client; }

   
    get_next_backend();
    int backend;
    
    pthread_mutex_lock(&backend_mutex);
    backend = backend_index;
    pthread_mutex_unlock(&backend_mutex);

    backend = connect_to_backend(backend);
    if (backend < 0) goto cleanup_client;
    setsockopt(backend, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  
    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t w = send(backend, buf + sent, req_len - sent, 0);
        if (w <= 0) { perror("send(backend)"); goto cleanup_both; }
        sent += w;
    }
    shutdown(backend, SHUT_WR);

    
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
    pthread_mutex_destroy(&backend_mutex);
    return 0;
}
