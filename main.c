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
    int server_fd, current_backend = 0, epoll_fd, epoll_ctl_, nfds, n, active_fd;
    char *target_port;
    char request_buffer[2048];
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

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
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

        // load balance between Java apps
        target_port = backend_ports[current_backend];

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
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
                ev.data.fd = client_fd;
                ev.data.ptr = browser_state;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                }
            } else {
                ConnectionState *active_state = (ConnectionState*) events[n].data.ptr;
                if (active_state->type == TYPE_BROWSER && active_state->partner == NULL) {
                    const ssize_t bytes_received = recv(active_state->fd, request_buffer, sizeof(request_buffer) - 1, 0);
                    if (bytes_received > 0) {
                        request_buffer[bytes_received] = '\0';
                        printf("The browser sent:\n%s\n", request_buffer);
                    }


                }
            }
        }
    }

    close(server_fd);
    return 0;
}