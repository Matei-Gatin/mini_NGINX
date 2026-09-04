// main.c

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>

//
#include <string.h>

#include "./include/proxy.h"

//
#define PROXY_PORT "8080"
#define BACKEND_IP "127.0.0.1"
#define MAX_EVENTS 10

// PROTOTYPES

char *backend_ports[] = {"9000", "9001"};
const int num_backends = 2;

typedef enum {
    TYPE_SERVER,
    TYPE_BROWSER,
    TYPE_BACKEND
} fd_type_t;

typedef struct ConnectionState {
    int fd;
    fd_type_t type;
    struct ConnectionState *partner;

    uint8_t pending_data[2048];
    ssize_t pending_len;
} ConnectionState;

ConnectionState* create_connection_state__(int fd, fd_type_t type, ConnectionState* partner, uint8_t request_buffer[2048], ssize_t request_len);
void free_connection_state__(ConnectionState* connection_state);
void handle_new_connection__(int server_fd, int epoll_fd, struct epoll_event* ev);
void handle_browser_event__(ConnectionState* active_state, int epoll_fd, uint32_t events, uint8_t request_buffer[2048], struct epoll_event* ev);
void handle_backend_event__(ConnectionState* active_state, int epoll_fd, uint32_t events, uint8_t* request_buffer, struct epoll_event* ev);

// FUNCTIONS
ConnectionState* create_connection_state__(int fd, fd_type_t type, ConnectionState* partner, uint8_t request_buffer[2048], ssize_t request_len) {
    ConnectionState* connection_state = (ConnectionState *) malloc(sizeof(struct ConnectionState));
    if (connection_state == NULL) return NULL;

    connection_state->fd = fd;
    connection_state->type = type;
    connection_state->partner = partner;

    if (request_len > 0) {
        memcpy(connection_state->pending_data, request_buffer, request_len);
    }

    connection_state->pending_len = request_len;

    return connection_state;
}

void free_connection_state__(ConnectionState* connection_state) {
    if (connection_state == NULL) return;

    close(connection_state->fd);

    if (connection_state->partner != NULL) {
        close(connection_state->partner->fd);
        free(connection_state->partner);
    }

    free(connection_state);
}

void handle_new_connection__(
    const int server_fd,
    const int epoll_fd,
    struct epoll_event* ev) {

    const int client_fd = accept(server_fd, NULL, NULL);

    if (client_fd == -1) {
        perror("Failed to accept");
        return;
    }

    printf("Browser connected! Forwarding to backend...\n");

    if (set_non_blocking__(client_fd) == -1) {
        return;
    }

    ConnectionState* browser_state = create_connection_state__(client_fd, TYPE_BROWSER, NULL, 0, 0);
    if (browser_state == NULL) {
        perror("create_connection_state__: browser_state");
        close(client_fd);
        return;
    }

    ev->events = EPOLLIN;
    ev->data.ptr = browser_state;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, ev) == -1) {
        perror("epoll_ctl: client_fd");
        close(client_fd);
        free(browser_state);
    }
}

void handle_browser_event__(
    ConnectionState* active_state,
    int epoll_fd,
    const uint32_t events,
    uint8_t request_buffer[2048],
    struct epoll_event* ev) {

    if (events & EPOLLIN) {
        const ssize_t bytes_received = recv(active_state->fd, request_buffer, 2048, 0);
        if (bytes_received <= 0) {
            free_connection_state__(active_state);
            return;
        }

        // parse HTTP request
        char method[256] = {0};
        char path[1024] = {0};
        parse_http_request__(request_buffer, method, path);

        printf("Browser request METHOD: %s | PATH: %s\n", method, path);

        if (active_state->partner == NULL) {
            const char* target_port;

            if (strncmp(path, "/hello", 6) == 0) {
                target_port = backend_ports[0]; // port 9000
            } else if (strncmp(path, "/goodbye", 8) == 0) {
                target_port = backend_ports[1]; // port 9001
            } else {
                target_port = backend_ports[0]; // port 9000
            }

            printf("Routing traffic to PORT: %s\n", target_port);

            const int backend_fd = connect_to_backend(BACKEND_IP, target_port);

            ConnectionState* backend_state =
                create_connection_state__(backend_fd, TYPE_BACKEND, active_state, request_buffer, bytes_received);

            active_state->partner = backend_state;

            ev->events = EPOLLOUT;
            ev->data.ptr = backend_state;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, backend_fd, ev);
        } else {
            send(active_state->partner->fd, request_buffer, bytes_received, 0);
        }
    }
}

void handle_backend_event__(
    ConnectionState* active_state,
    int epoll_fd,
    const uint32_t events,
    uint8_t* request_buffer,
    struct epoll_event* ev) {

    if (events & EPOLLIN) {
        const ssize_t bytes_received = recv(active_state->fd, request_buffer, 2048, 0);

        if (bytes_received <= 0) {
            free_connection_state__(active_state);
            return;
        }

        send(active_state->partner->fd, request_buffer, bytes_received, 0);
    }

    else if (events & EPOLLOUT) {
        send(active_state->fd, active_state->pending_data, active_state->pending_len, 0);

        ev->events = EPOLLIN;
        ev->data.ptr = active_state;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, active_state->fd, ev);
    }
}

int main(void) {
    int server_fd, epoll_fd, epoll_ctl_, nfds, n;
    uint8_t request_buffer[2048];
    struct epoll_event ev, events[MAX_EVENTS];

    signal(SIGCHLD, SIG_IGN);

    server_fd = create_server_socket(PROXY_PORT);
    if (server_fd == -1) {
        return EXIT_FAILURE;
    }

    printf("Listening on PORT: %s...\n", PROXY_PORT);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return EXIT_FAILURE;
    }

    ConnectionState *server_state = create_connection_state__(server_fd, TYPE_SERVER, NULL, 0, 0);
    if (server_state == NULL) {
        perror("create_connection_state__: server_state");
        return EXIT_FAILURE;
    }

    ev.events = EPOLLIN;
    ev.data.ptr = server_state;
    epoll_ctl_ = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
    if (epoll_ctl_ == -1) {
        perror("epoll_ctl: listen_sock");
        return EXIT_FAILURE;
    }

    while (true) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            return EXIT_FAILURE;
        }

        for (n = 0; n < nfds; ++n) {
            ConnectionState *active_state = (ConnectionState *) events[n].data.ptr;

            if (active_state->type == TYPE_SERVER) {
                handle_new_connection__(server_fd, epoll_fd, &ev);
            } else if (active_state->type == TYPE_BROWSER) {
                handle_browser_event__(active_state, epoll_fd, events[n].events, request_buffer, &ev);
            } else if (active_state->type == TYPE_BACKEND) {
                handle_backend_event__(active_state, epoll_fd, events[n].events, request_buffer, &ev);
            }
        }
    }
}