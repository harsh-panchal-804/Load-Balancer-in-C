#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define MAX_BACKEND 3
#define LISTEN_PORT 3000
#define BUFFER_SIZE (1024 * 1024)

const char *backends_ip[] = {
    "ec2-3-111-86-156.ap-south-1.compute.amazonaws.com",
    "ec2-3-111-86-156.ap-south-1.compute.amazonaws.com",
    "ec2-3-111-86-156.ap-south-1.compute.amazonaws.com"
};

const int backend_port[] = {3001, 3002, 3003};

int backend_index = 0;
pthread_mutex_t backend_mutex = PTHREAD_MUTEX_INITIALIZER;
SSL_CTX *global_ctx = NULL;

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

        if (sock < 0)
            continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return sock;
}

typedef struct {
    int client_sock;
} thread_arg;

void *handle_client(void *p) {
    thread_arg *arg = (thread_arg *)p;
    int client = arg->client_sock;
    free(arg);

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL *ssl = SSL_new(global_ctx);

    if (!ssl) {
        ERR_print_errors_fp(stderr);
        close(client);
        return NULL;
    }

    SSL_set_fd(ssl, client);

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(client);
        return NULL;
    }

    char *buf = malloc(BUFFER_SIZE);

    if (!buf) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);
        return NULL;
    }

    ssize_t req_len = 0;
    ssize_t n;

    while ((n = SSL_read(ssl,
                         buf + req_len,
                         BUFFER_SIZE - req_len - 1)) > 0) {

        req_len += n;
        buf[req_len] = '\0';

        if (req_len >= 4 && strstr(buf, "\r\n\r\n"))
            break;

        if (req_len >= BUFFER_SIZE - 1)
            break;
    }

    if (n <= 0 || req_len == 0) {
        free(buf);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);
        return NULL;
    }

    int backend_idx = get_next_backend();
    int backend = connect_to_backend(backend_idx);

    if (backend < 0) {
        free(buf);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);
        return NULL;
    }

    ssize_t sent = 0;

    while (sent < req_len) {
        ssize_t w = send(
            backend,
            buf + sent,
            req_len - sent,
            0
        );

        if (w <= 0) {
            close(backend);
            free(buf);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(client);
            return NULL;
        }

        sent += w;
    }

    while ((n = recv(backend, buf, BUFFER_SIZE, 0)) > 0) {
        ssize_t offset = 0;

        while (offset < n) {
            ssize_t w = SSL_write(
                ssl,
                buf + offset,
                n - offset
            );

            if (w <= 0)
                break;

            offset += w;
        }
    }

    close(backend);
    free(buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client);

    return NULL;
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    global_ctx = SSL_CTX_new(TLS_server_method());

    if (!global_ctx) {
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if (SSL_CTX_use_certificate_chain_file(
            global_ctx,
            "/etc/letsencrypt/live/harshpanchal.duckdns.org/fullchain.pem"
        ) <= 0 ||
        SSL_CTX_use_PrivateKey_file(
            global_ctx,
            "/etc/letsencrypt/live/harshpanchal.duckdns.org/privkey.pem",
            SSL_FILETYPE_PEM
        ) <= 0) {

        ERR_print_errors_fp(stderr);
        exit(1);
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;

    setsockopt(
        listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener,
             (struct sockaddr *)&addr,
             sizeof(addr)) < 0 ||
        listen(listener, SOMAXCONN) < 0) {

        perror("bind/listen");
        exit(1);
    }

    printf("HTTPS Load balancer listening on port %d\n", LISTEN_PORT);
    fflush(stdout);

    while (1) {
        int client = accept(listener, NULL, NULL);

        if (client < 0)
            continue;

        thread_arg *arg = malloc(sizeof(*arg));

        if (!arg) {
            close(client);
            continue;
        }

        arg->client_sock = client;

        pthread_t tid;

        if (pthread_create(
                &tid,
                NULL,
                handle_client,
                arg
            ) != 0) {

            close(client);
            free(arg);
            continue;
        }

        pthread_detach(tid);
    }

    close(listener);
    SSL_CTX_free(global_ctx);
    EVP_cleanup();

    return 0;
}
