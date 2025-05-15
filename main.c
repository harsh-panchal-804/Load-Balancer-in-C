#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_BACKEND 3
#define PORT 3000
#define BUFFER_SIZE 8192

const char *backends_ip[] = {"127.0.0.1", "127.0.0.1", "127.0.0.1"};
const int backend_port[] = {3001, 3002, 6970};
int backend_index = MAX_BACKEND-1;

void get_next_backend() {
    backend_index = (backend_index + 1) % MAX_BACKEND;
}

int connect_to_backend(int index) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(backend_port[index]);
    if (inet_pton(AF_INET, backends_ip[index], &addr.sin_addr) <= 0) {
        perror("inet_pton() failed");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect() to backend failed");
        close(sock);
        return -1;
    }

    fprintf(stderr, "Connected to backend %s:%d (index %d)\n",
            backends_ip[index], backend_port[index], index);
    return sock;
}

void handle_client(int client_sock) {
    fprintf(stderr, "New client connected (fd=%d)\n", client_sock);
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    ssize_t req_len = 0;
    ssize_t n;
    // Read request headers
    while ((n = recv(client_sock, buffer + req_len, BUFFER_SIZE - req_len, 0)) > 0) {
        req_len += n;
        if (strstr(buffer, "\r\n\r\n") != NULL) {
            break;
        }
        if (req_len >= BUFFER_SIZE) {
            fprintf(stderr, "Request is too large for loadbalancer\n");
            break;
        }
    }
    if (n < 0) {
        perror(" recv() from client failed");
        close(client_sock);
        return;
    }

    get_next_backend();
    int backend_sock = connect_to_backend(backend_index);
    if (backend_sock < 0) {
        close(client_sock);
        return;
    }

    // forward request to backend
    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t s = send(backend_sock, buffer + sent, req_len - sent, 0);
        if (s <= 0) {
            perror("send() to backend server failed");
            close(client_sock);
            close(backend_sock);
            return;
        }
        sent += s;
    }
    fprintf(stderr, "Forwarded %zd bytes to backend\n", sent);

    ///send backend respone back to client via load balancer
    memset(buffer, 0, sizeof(buffer));
    while ((n = recv(backend_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        ssize_t total = 0;
        while (total < n) {
            ssize_t s = send(client_sock, buffer + total, n - total, 0);
            if (s <= 0) {
                perror(" send() to client failed");
                break;
            }
            total += s;
        }
        if (total < n) break;
    }
    if (n < 0) {
        perror("recv() from backend failed");
    } 

    close(backend_sock);
    close(client_sock);
    fprintf(stderr, " Closed connection with client (fd=%d)\n", client_sock);
}

int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt() failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, SOMAXCONN) < 0) {
        perror("listen() failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Load balancer listening on port %d\n", PORT);

    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            perror("accept() failed");
            continue;
        }
        handle_client(client_sock);
    }

    close(server_sock);
    return 0;
}
