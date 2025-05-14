#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BACKLOG 16      // pending connections queue
#define MAX_EVENTS 1024 // max events for epoll_wait
#define BUF_SIZE 4096   // read buffer size

static volatile sig_atomic_t keep_running = 1;

// Handler for SIGINT/SIGTERM to trigger graceful shutdown
void sigint_handler(int signo)
{
    (void)signo;
    keep_running = 0;
}

static void safe_write(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0)
        {
            off += n;
        }
        else if (n == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            perror("write");
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int server_fd;

    int epoll_fd;

    struct sockaddr_in addr;

    int port = DEFAULT_PORT;

    if (argc > 1)
    {
        port = atoi(argv[1]);

        if (port <= 0 || port > 65535)
        {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    // Ignore SIGPIPE to prevent the server from terminating when writing to a closed socket.
    // Without this, writing to a disconnected client may cause the process to receive SIGPIPE and exit.
    signal(SIGPIPE, SIG_IGN);

    // Handle SIGINT (Ctrl+C) and SIGTERM to allow graceful shutdown of the server.
    // Without this, the server may terminate abruptly without closing the listening socket properly.
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    // Create TCP socket for IPv4
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Enable SO_REUSEADDR to allow immediate reuse of the port after the server restarts.
    // Without this, bind() may fail with "Address already in use" if the port is in TIME_WAIT.
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Clear the address struct to avoid garbage values
    memset(&addr, 0, sizeof(addr));
    // Set IPv4 as the protocol family (required for bind())
    addr.sin_family = AF_INET;
    // Accept connections on any local IP (not just 127.0.0.1)
    addr.sin_addr.s_addr = INADDR_ANY;
    // Set the port number; convert to network byte order so it's
    // interpreted correctly by the kernel
    addr.sin_port = htons(port);

    // Bind the socket to the local address and port; required before
    // listening for connections
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Create eploo instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        perror("epoll_create1");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Register server_fd for read events
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0)
    {
        perror("epoll_ctl: listen_sock");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];

    printf("Server listening on port %d (PID %d)\n", port, getpid());

    while (keep_running)
    {
        // Wait for I/O events on registered file descriptors (blocks until at least one is ready)
        int nready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (nready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        // Loop over each ready file descriptor to handle the corresponding event
        for (int i = 0; i < nready; i++)
        {
            if (events[i].data.fd == server_fd)
            {
                // New connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                
                if (client_fd < 0)
                {
                    perror("accept");
                    continue;
                }

                // Add client socket to epoll to be monitored and get notified when readable
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
            }
            else
            {
                // Data from client
                int client_fd = events[i].data.fd;
                char buf[BUF_SIZE];

                ssize_t n = read(client_fd, buf, sizeof(buf) - 1);

                if (n <= 0)
                {
                    // Client closed or error
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                }
                else
                {
                    buf[n] = '\0';
                    const char *resp =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Hello from epoll server!\n";

                    safe_write(client_fd, resp, strlen(resp));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                }
            }
        }
    }


    printf("Shutting down server.\n");
    close(epoll_fd);
    close(server_fd);
    return EXIT_SUCCESS;
}