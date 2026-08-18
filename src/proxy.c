// proxy.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

//
#include "../include/proxy.h"

//
#define BACKLOCK 10

/*
 * creates the server socket or returns -1 if fails
 */
int create_server_socket(const char *port) {
    int status;
    struct addrinfo hints;
    struct addrinfo *server_info;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(NULL, port, &hints, &server_info);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    int sock_fd = -1;
    struct addrinfo *res;
    int yes = 1;

    for (res = server_info; res != NULL; res = res->ai_next) {
        // create a new socket
        sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock_fd == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        // bind the socket to the port
        if (bind(sock_fd, res->ai_addr, res->ai_addrlen)) {
            close(sock_fd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(server_info);

    if (res == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }

    // listen for new sockets
    const int is_listening = listen(sock_fd, BACKLOCK);
    if (is_listening == -1) {
        perror("Failed to listen on PORT: 8080");
        return -1;
    }

    return sock_fd;
}

int connect_to_backend(const char *backend_ip, const char *backend_port) {
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(backend_ip, backend_port, &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    int sock_fd = -1;
    struct addrinfo *p;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        freeaddrinfo(servinfo);
        return -1;
    }

    freeaddrinfo(servinfo);

    return sock_fd;
}