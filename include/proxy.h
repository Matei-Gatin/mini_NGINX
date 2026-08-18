#ifndef PROXY_H
#define PROXY_H

int create_server_socket(const char *port);

int connect_to_backend(const char *backend_ip, const char *backend_port);

#endif
