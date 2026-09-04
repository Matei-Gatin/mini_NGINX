#ifndef PROXY_H
#define PROXY_H

int create_server_socket(const char *port);

int connect_to_backend(const char *backend_ip, const char *backend_port);

int set_non_blocking__(int sock_fd);

void parse_http_request__(const uint8_t* buffer, char* method, char* path);

#endif
