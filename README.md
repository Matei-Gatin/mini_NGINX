# Mini-NGINX: High-Performance Event-Driven Proxy

A lightweight, non-blocking Layer 7 reverse proxy and load balancer written in C.

This project was built to explore low-level systems engineering, network programming, and the Linux kernel's asynchronous I/O capabilities. By utilizing a single-threaded event loop architecture via `epoll`, this proxy efficiently handles thousands of concurrent connections without the massive memory overhead of traditional thread-per-connection models.

## Core Architecture & Engineering Highlights

* **Asynchronous Event Loop (`epoll`):** The routing engine relies on Linux's `epoll` facility to monitor file descriptors, ensuring CPU cycles are only spent on sockets actively ready for I/O.
* **Zero-Blocking TCP Handshakes:** Traditional proxies block while waiting for the OS to complete the 3-way TCP handshake. This architecture sets backend sockets to `O_NONBLOCK`, intercepts the `EINPROGRESS` signal, and delegates the wait-state to `EPOLLOUT`. The main loop never blocks.
* **Layer 7 HTTP Parsing:** Implements a custom text parser to inspect incoming byte streams, extract HTTP Methods (like `GET`), and identify URL paths before routing traffic to the appropriate backend.
* **Deterministic Memory Management:** Avoids mid-loop memory leaks through a strictly controlled `ConnectionState` structure. Each browser-to-backend connection is tracked, and resources (file descriptors, buffers, and pointers) are cleanly wiped and freed the moment a socket drops.
* **Buffer Overflow Protection:** Employs strict bounds checking and bounded string formatting during Layer 7 parsing to ensure malicious or malformed requests cannot overwrite adjacent memory blocks.

## Traffic Routing Logic

The proxy intercepts HTTP traffic on Port `8080` and acts as a Layer 7 router for backend applications:

* `GET /hello` ➔ Routes to Backend A (Port `9000`)
* `GET /goodbye` ➔ Routes to Backend B (Port `9001`)
* `Default` ➔ Fallback routing to Backend A

## Getting Started

### Prerequisites
* A Linux environment
* GCC Compiler

### Compilation & Execution
Compile the source code using `gcc`:
```bash
gcc main.c proxy.c -o mini_nginx
```
Run the proxy:
```bash
./mini_nginx
```
(The server will immediately begin listening on localhost:8080)