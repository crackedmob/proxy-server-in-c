# HTTP Proxy Server with Caching (GET-only)

This is a multithreaded HTTP proxy server built in C, designed to intercept and forward HTTP GET requests to web servers, cache the responses, and serve them directly from memory for repeated requests. This implementation uses POSIX threads, semaphores, and a custom LRU cache for performance.

Currently supports only GET requests.

---

## Features

* **Multithreaded**: Handles multiple client connections concurrently using pthreads.
* **LRU Caching**: Stores recently accessed web content in memory for faster response on repeated requests.
* **Basic Logging**: Prints connection, method, and caching info to the terminal.
* **Semaphore Locking**: Ensures thread-safe access to shared resources (cache).
* **Memory-managed**: Uses `malloc`, `realloc`, and `free` to handle dynamic content size.

---

## Build Instructions (WSL/Linux)

Make sure you're inside your WSL environment.

### Option 1: Compile manually (if Makefile is not present)

```bash
gcc proxy_server_with_cache.c proxy_parse.c -o proxy_server -lpthread
```

### Option 2: Use the Makefile

```bash
make
```

---

## Run Instructions

Start the server on port 8080:

```bash
./proxy_server 8080
```

Then in another terminal or browser or using `curl`, test the proxy:

```bash
curl -v -x http://localhost:8080 http://neverssl.com
```

* First time: It fetches from the actual web server.
* Second time: Response is served directly from cache.

---

## In Progress (Planned Improvements)

These are the upcoming features and ideas that are being worked on or considered:

* [ ] **Support for POST, PUT, DELETE**: Extend the proxy to handle other HTTP request methods in addition to GET.
* [ ] **Persistent Disk Caching**: Store cached data on disk to survive server restarts.
* [ ] **Improved Logging and Debugging**: Add detailed logs and error messages for better traceability.
* [ ] **Configurable Cache Size**: Allow users to configure maximum memory usage for caching.
* [ ] **Graceful Shutdown**: Handle termination signals properly to close sockets and free memory.
* [ ] **Modular Design Refactor**: Clean separation of request handling, parsing, and caching logic.

---

## Acknowledgments

Built by following a tutorial by **Lovepreet Singh** on YouTube.

Commented, debugged, and extended in a Linux (WSL) development environment using **GCC** and **POSIX libraries**.
