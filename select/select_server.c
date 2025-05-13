#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BACKLOG 16    // pending connections queue size
#define BUF_SIZE 4096 // read buffer size

static volatile sig_atomic_t keep_running = 1;

// Handler for SIGINT/SIGTERM to trigger graceful shutdown
void sigint_handler(int signo)
{
    (void)signo;
    keep_running = 0;
}

// Robust write: handles partial writes and EINTR
static void safe_write(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0)
        {
            off += (size_t)n;
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

    // Handle Ctrl+C (SIGINT) by setting a flag to stop the server loop gracefully.
    // Without this, the server would exit immediately without cleaning up (e.g., closing sockets).
    signal(SIGINT, sigint_handler);

    // Handle termination signal (SIGTERM), such as from `kill`.
    // Without this, the server could terminate abruptly without shutting down cleanly.
    signal(SIGTERM, sigint_handler);

    // Ignore SIGPIPE to prevent the server from terminating when writing to a closed socket.
    // Without this, writing to a disconnected client may cause the process to receive SIGPIPE and exit.
    signal(SIGPIPE, SIG_IGN);

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

    fd_set master_set, read_set;
    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    int maxfd = server_fd;

    printf("Server listening on port %d (PID %d)\n", port, getpid());

    while (keep_running)
    {
        read_set = master_set;

        // Wait for activity on any socket (including the server_fd).
        // Blocks until one or more file descriptors become ready to read.
        // Without this, we’d be busy looping or missing new events.
        int ready = select(maxfd + 1, &read_set, NULL, NULL, NULL);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue; // interrupted by signal
            }

            perror("select");
            break;
        }

        for (int fd = 0; fd <= maxfd && ready; fd++)
        {
            if (!FD_ISSET(fd, &read_set))
            {
                continue;
            }

            ready--;

            if (fd == server_fd)
            {
                // New incoming connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd,
                                       (struct sockaddr *)&client_addr,
                                       &client_len);
                if (client_fd < 0)
                {
                    perror("accept");
                    continue;
                }

                // Add the new client socket to the master set so we can monitor it with select().
                // Without this, select() wouldn't detect when the client sends data.
                FD_SET(client_fd, &master_set);

                if (client_fd > maxfd)
                {
                    maxfd = client_fd;
                }
            }
            else
            {
                // Data from an existing client
                char buf[BUF_SIZE];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);
                if (n <= 0)
                {
                    // Connection closed or error
                    close(fd);

                    // Remove the client socket from the master set after closing it.
                    // Without this, select() would still monitor a closed socket,
                    // leading to errors or wasted resources.
                    FD_CLR(fd, &master_set);
                }
                else
                {
                    buf[n] = '\0';
                    const char *resp =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Hello from select server!\n";
                    safe_write(fd, resp, strlen(resp));
                    close(fd);
                    FD_CLR(fd, &master_set);
                }
            }
        }
    }

    // Cleanup all fds
    for (int fd = 0; fd <= maxfd; fd++)
    {
        if (FD_ISSET(fd, &master_set))
        {
            close(fd);
        }
    }
    return EXIT_SUCCESS;
}