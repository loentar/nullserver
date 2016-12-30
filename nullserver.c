#include "nullserver.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

//#define assert(a)


#define EPOLL_TIMEOUT -1 // infinite
#define MAX_EVENTS 256
#define MAX_SOCKET_FD 32768
#define MAX_CONCURRENCY 1024
#define MAX_REQUEST_SIZE 512

typedef struct {
    char used;
    char buffer[MAX_REQUEST_SIZE];
    ssize_t length;
    ssize_t bufferSize;
    off_t responseOffset;
} context;

static int isStopping = 0;
static int fdServer = 0;
static int fdEpoll = 0;
static struct epoll_event event;
static struct epoll_event events[MAX_EVENTS];
static char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "Server: null\r\n"
        "Date: Nul, 00 Nul 0000 00:00:00 GMT\r\n"
        "\r\n"
        "Hello, World!";
static const int responseSize = sizeof(response) - 1;
static ssize_t cachedRequestSize = 0;

// buffers
static context contexts[MAX_CONCURRENCY];
static int fdMap[MAX_SOCKET_FD];

#define MAX(a, b) ((a) > (b) ? (a) : (b))

//inline char* token(char** curr) {
//    char* start = *curr;
//    while (**curr != ' ' && **curr != '\0')
//        ++*curr;
//    **curr = '\0';
//    ++*curr;
//    return start;
//}

static int nullserver_createServerSocket(const char *ip, const char *port)
{
    struct addrinfo hints;
    struct addrinfo* addr;
    struct addrinfo* curr;
    int sock = -1;
    const char* lastError;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    int res = getaddrinfo(ip, port, &hints, &addr);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        return -1;
    }

    for (curr = addr; curr; curr = curr->ai_next) {
        sock = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol);
        if (sock == -1)
            continue;

        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        res = bind(sock, curr->ai_addr, curr->ai_addrlen);
        if (res == 0) {
            /* We managed to bind successfully! */
            break;
        }

        lastError = strerror(errno);
        close(sock);
    }

    freeaddrinfo(addr);

    if (!curr) {
        fprintf(stderr, "Could not bind to %s:%s: %s", ip, port, lastError);
        return -1;
    }

    fprintf(stderr, "Accepting connections on %s:%s\n\n", ip, port);

    return sock;
}


static inline int nullserver_setupNonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("Error getting socket flags");
        return 0;
    }

    flags |= O_NONBLOCK;
    int res = fcntl(fd, F_SETFL, flags);
    if (res == -1) {
        perror("Error setting socket flags");
        return 0;
    }

    return 1;
}

static inline void nullserver_closeConnection(int fd)
{
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    int res = epoll_ctl(fdEpoll, EPOLL_CTL_DEL, fd, &event);
    if (res == -1)
        perror("Failed to remove client");

    close(fd);

    assert(fd < MAX_SOCKET_FD);
    int index = fdMap[fd];
    assert(index != -1);
    context* ctx = &contexts[index];
    ctx->used = 0;
    ctx->length = 0;
    ctx->bufferSize = 0;
    ctx->responseOffset = 0;
    fdMap[fd] = -1;
}


static inline int nullserver_handleIncomingConnection()
{
    struct sockaddr_storage inAddr;
    socklen_t inLen = sizeof(inAddr);
    while (!isStopping) {
        int fd = accept(fdServer, (struct sockaddr*)&inAddr, &inLen);
        if (fd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                // We have processed all incoming connections.
                break;
            } else {
                perror("accept");
                break;
            }
        }
        assert(fd < MAX_SOCKET_FD);

        int res = nullserver_setupNonblock(fd);
        if (res == -1) {
            close(fd);
            continue;
        }

        int nodelayOpt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelayOpt, sizeof(nodelayOpt));

        // add it to the list of fds to monitor.
        event.data.fd = fd;
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        res = epoll_ctl(fdEpoll, EPOLL_CTL_ADD, fd, &event);
        if (res == -1) {
            perror("Failed to add client");
            nullserver_closeConnection(fd);
        }

        int index = fdMap[fd];
        assert(index == -1);

        for (int i = 0; i < MAX_CONCURRENCY; ++i) {
            if (!contexts[i].used) {
                index = i;
                break;
            }
        }

        assert(index != -1);
        fdMap[fd] = index;

        context* ctx = &contexts[index];
        ctx->used = 1;
    }

    return 1;
}

static inline void nullserver_readyWrite(int fd, context* ctx) {
    if (ctx->responseOffset < responseSize) {
        ssize_t res = send(fd, response + ctx->responseOffset, responseSize - ctx->responseOffset, 0);
        if (res <= 0) {
            perror("failed to write response");
            nullserver_closeConnection(fd);
            return;
        }

        ctx->responseOffset += res;
    }
}


static inline void nullserver_route(int fd, const char* url, context* ctx)
{
//    if (!strcmp(url, "/plaintext")) {
        ctx->responseOffset = 0;
        nullserver_readyWrite(fd, ctx);
//    } else {
//        fprintf(stderr, "Can't handle URL: %s", url);
//        nullserver_closeConnection(fd);
//    }
}

static inline void nullserver_processRequest(int fd, context* ctx)
{
//    char* curr = ctx->buffer;
////    const char* method = token(&curr);
//    char* next = strchr(curr, ' ');
//    *next = 0;
//    ++next;
//    assert(!strcmp(curr, "GET"));

//    curr = next;
//    next = strchr(curr, ' ');
//    *next = 0;
////    ++next;

//    const char* url = token(&curr);

//    nullserver_route(fd, curr, ctx);
    nullserver_route(fd, 0, ctx);

    // prepare for the next request
    if (ctx->bufferSize > ctx->length) {
        memmove(ctx->buffer, ctx->buffer + ctx->length, ctx->bufferSize - ctx->length);
        ctx->bufferSize -= ctx->length;
        ctx->length = 0;
    } else {
        ctx->length = 0;
        ctx->bufferSize = 0;
    }
}

static inline void nullserver_readyRead(int fd)
{
    assert(fd < MAX_SOCKET_FD);
    int index = fdMap[fd];
    assert(index != -1);
    context* ctx = &contexts[index];
    const int64_t bytesAvail = MAX_REQUEST_SIZE - ctx->length;
    ssize_t count = recv(fd, ctx->buffer + ctx->length, bytesAvail, 0);

    if (count <= 0) {
        nullserver_closeConnection(fd);
        return;
    }

    ctx->bufferSize += count;

    if (cachedRequestSize) { // for optimization we assume all requests has the same size :)
        if ((ctx->length + count) >= cachedRequestSize) {
            ctx->length = cachedRequestSize;
            nullserver_processRequest(fd, ctx);
        } else {
            ctx->length += count;
        }
    } else {
        const char* startFind = (ctx->length > 4) ? (ctx->buffer - 3) : ctx->buffer;

        ctx->buffer[ctx->length + count] = 0;
        const char* pos = strstr(startFind, "\r\n\r\n");
        if (pos) {
            ctx->length += (pos - ctx->buffer) + 4;
            cachedRequestSize = ctx->length;
            nullserver_processRequest(fd, ctx);
        } else {
            ctx->length += count;
        }
    }
}


int nullserver_create(const char* ip, const char* port)
{
    memset(&event, 0, sizeof(event));
    memset(&contexts, 0, sizeof(contexts));
    for (int i = 0; i < MAX_CONCURRENCY; ++i)
        fdMap[i] = -1;

    fdServer = nullserver_createServerSocket(ip, port);
    if (fdServer == -1)
        return 0;

    if (!nullserver_setupNonblock(fdServer))
        return 0;

    int res = listen(fdServer, SOMAXCONN);
    if (res == -1) {
        perror("listen");
        return 0;
    }

    fdEpoll = epoll_create1(0);
    if (fdEpoll == -1) {
        perror("epoll_create");
        return 0;
    }

    event.data.fd = fdServer;
    event.events = EPOLLIN | EPOLLET;
    res = epoll_ctl(fdEpoll, EPOLL_CTL_ADD, fdServer, &event);
    if (res == -1) {
        perror("epoll_ctl");
        return 0;
    }

    return 1;
}

void nullserver_destroy()
{
    if (fdServer != 0)
        close(fdServer);
}


int nullserver_exec()
{
    while (!isStopping) {
        int n = epoll_wait(fdEpoll, events, MAX_EVENTS, EPOLL_TIMEOUT);
        for (int i = 0; i < n && !isStopping; ++i) {
            const uint32_t res = events[i].events;
            if ((events[i].events & EPOLLERR) ||
                   (events[i].events & EPOLLHUP) ||
                   (!(events[i].events & (EPOLLIN | EPOLLOUT)))) {
                if (errno != EAGAIN && errno != EPIPE) {
                    fprintf(stderr, "epoll error: %s. Client #%d\n", strerror(errno), events[i].data.fd);
                }
                nullserver_closeConnection(events[i].data.fd);
            } else if (fdServer == events[i].data.fd) {
                nullserver_handleIncomingConnection();
            } else {
                if (res & EPOLLIN) {
                    nullserver_readyRead(events[i].data.fd);
                } else if (res & EPOLLOUT) {
                    int fd = events[i].data.fd;
                    assert(fd < MAX_SOCKET_FD);
                    int index = fdMap[fd];
                    assert(index != -1);
                    context* ctx = &contexts[index];
                    nullserver_readyWrite(fd, ctx);
                } else {
                    perror("Unknown EPOLL event");
                }
            }
        }
    }

    return EXIT_SUCCESS;
}

void nullserver_quit()
{
    isStopping = 1;
}

