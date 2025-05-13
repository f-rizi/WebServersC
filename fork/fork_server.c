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

// Handler for SIGCHLD to reap terminated child processes
void sigchld_handler(int signo)
{
    (void)signo;
    // Reap all exited child processes to prevent zombie processes
    // (e.g., after forked clients exit)
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
}

// Handler for SIGINT/SIGTERM to trigger graceful shutdown
void sigint_handler(int signo)
{
    (void)signo;
    keep_running = 0;
}

// Handle a single client connection
void handle_client(int client_fd)
{
    char buf[BUF_SIZE];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);

    if (n > 0)
    {
        buf[n] = '\0';

        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello from fork server!\n";

        ssize_t written = write(client_fd, resp, strlen(resp));
        if (written == -1)
        {
            perror("write");
        }
    }

    close(client_fd);
}

int main(int argc, char *argv[])
{
    int server_fd;
    int client_fd;

    struct sockaddr_in addr;

    int port = DEFAULT_PORT;
    socklen_t addrlen = sizeof(addr);

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

    // Set up a SIGCHLD handler to automatically reap zombie child processes.
    // Without this, terminated child processes remain in the zombie state until manually waited on.
    struct sigaction sa = {.sa_handler = sigchld_handler};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction(SIGCHLD)");
        exit(EXIT_FAILURE);
    }

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

    printf("Server listening on port %d (PID %d)\n", port, getpid());

    while (keep_running)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);

        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                // interrupted by signal
                continue;
            }

            perror("accept");
            break;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(client_fd);
        }
        else if (pid == 0)
        {
            // Child process
            close(server_fd);
            handle_client(client_fd);
            _exit(EXIT_SUCCESS);
        }
        else
        {
            // Parent process
            close(client_fd);
        }
    }

    printf("Shutting down server.\n");
    close(server_fd);
    return EXIT_SUCCESS;
}