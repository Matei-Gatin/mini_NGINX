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
#include <fcntl.h>

#include "./include/proxy.h"

//
#define PROXY_PORT "8080"
#define BACKEND_IP "127.0.0.1"
#define MAX_EVENTS 10

// PROTOTYPES
void pass_data(int client_fd, int backend_fd);

const char *backend_ports[] = {"9000", "9001"};
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
} ConnectionState;

// HELPER FUNCTIONS
int set_non_blocking__(const int sock_fd) {
    // set the socket to non-blocking mode
    if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fnctl failed");
        close(sock_fd);
        return -1;
    }

    return 1;
}

// FUNCTIONS
int main(void) {
    int server_fd, current_backend = 0, epoll_fd, epoll_ctl_, nfds, n;
    char *target_port;
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

    ConnectionState *server_state = (struct ConnectionState*) malloc(sizeof(struct ConnectionState));
    server_state->fd = server_fd;
    server_state->type = TYPE_SERVER;
    server_state->partner = NULL;

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
            // load balance between Java apps
            target_port = backend_ports[current_backend];

            ConnectionState *active_state = (ConnectionState *) events[n].data.ptr;

            if (active_state->type == TYPE_SERVER) {
                const int client_fd = accept(server_fd, NULL, NULL);

                if (client_fd == -1) {
                    perror("Failed to accept");
                    continue;
                }

                printf("Browser connected! Forwarding to backend...\n");

                if (set_non_blocking__(client_fd) == -1) {
                    continue;
                }

                ConnectionState *browser_state = (struct ConnectionState*) malloc(sizeof(struct ConnectionState));
                browser_state->fd = client_fd;
                browser_state->type = TYPE_BROWSER;
                browser_state->partner = NULL;

                ev.events = EPOLLIN;
                ev.data.ptr = browser_state;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                }
            } else {
                const ssize_t bytes_received = recv(active_state->fd, request_buffer, sizeof(request_buffer), 0);
                if (bytes_received <= 0) {
                    close(active_state->fd);
                    if (active_state->partner != NULL) {
                        close(active_state->partner->fd);
                        free(active_state->partner);
                    }

                    free(active_state);
                    continue;
                }

                if (active_state->type == TYPE_BROWSER) {
                    if (active_state->partner == NULL) {
                        const int backend_fd = connect_to_backend(BACKEND_IP, target_port);
                        current_backend = (current_backend + 1) % num_backends;
                        set_non_blocking__(backend_fd);

                        ConnectionState *backend_state = malloc(sizeof(ConnectionState));
                        backend_state->fd = backend_fd;
                        backend_state->type = TYPE_BACKEND;
                        backend_state->partner = active_state;

                        active_state->partner = backend_state;

                        send(backend_fd, request_buffer, bytes_received, 0);

                        ev.events = EPOLLIN;
                        ev.data.ptr = backend_state;
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, backend_fd, &ev);
                    } else {
                        send(active_state->partner->fd, request_buffer, bytes_received, 0);
                    }
                } else if (active_state->type == TYPE_BACKEND) {
                    send(active_state->partner->fd, request_buffer, bytes_received, 0);
                }
            }
        }
    }

    close(server_fd);
    return 0;
}