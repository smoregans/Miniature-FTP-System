#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>

#include "myftp.h"

int setup_server(int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, BACKLOG) < 0) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int handle_data_connection(int client_sock) {
    int data_sock;
    struct sockaddr_in data_addr;
    socklen_t addr_len = sizeof(data_addr);

    data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
        write(client_sock, "EError creating data socket\n", 28);
        return -1;
    }

    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0;

    if (bind(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        write(client_sock, "EError binding data socket\n", 27);
        close(data_sock);
        return -1;
    }

    if (listen(data_sock, BACKLOG) < 0) {
        write(client_sock, "EError listening on data socket\n", 32);
        close(data_sock);
        return -1;
    }

    if (getsockname(data_sock, (struct sockaddr *)&data_addr, &addr_len) < 0) {
        write(client_sock, "EError getting socket name\n", 27);
        close(data_sock);
        return -1;
    }

    int port = ntohs(data_addr.sin_port);
    char response[32];
    snprintf(response, sizeof(response), "A%d\n", port);
    write(client_sock, response, strlen(response));

    return data_sock;
}

void handle_rcd(int client_sock, const char *pathname) {
    pid_t pid = getpid();
    if (pathname == NULL || strlen(pathname) == 0) {
        write(client_sock, "EPath required for 'C' command\n", 32);
        return;
    }

    if (chdir(pathname) == 0) {
        write(client_sock, "A\n", 2);
        printf("Child %d: changed directory to '%s'\n", pid, pathname);
        fflush(stdout);
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "EError changing directory: %s\n", strerror(errno));
        write(client_sock, error_msg, strlen(error_msg));
    }
}

void handle_rls(int client_sock, int data_sock) {
    int data_conn = accept(data_sock, NULL, NULL);
    if (data_conn < 0) {
        write(client_sock, "EError accepting data connection\n", 33);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(data_sock);
        dup2(data_conn, STDOUT_FILENO);
        dup2(data_conn, STDERR_FILENO);
        close(data_conn);
        execlp("ls", "ls", "-l", NULL);
        _exit(EXIT_FAILURE);
    } else if (pid > 0) {
        close(data_conn);
        waitpid(pid, NULL, 0);
        write(client_sock, "A\n", 2);
    } else {
        write(client_sock, "EError forking for ls command\n", 30);
    }
}


void handle_get(int client_sock, int data_sock, const char *pathname) {
    pid_t pid = getpid();
    int data_conn = accept(data_sock, NULL, NULL);
    if (data_conn < 0) {
        write(client_sock, "EError accepting data connection\n", 33);
        return;
    }

    int file_fd = open(pathname, O_RDONLY);
    if (file_fd < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "EError opening file: %s\n", strerror(errno));
        write(client_sock, error_msg, strlen(error_msg));
        close(data_conn);
        return;
    }

    write(client_sock, "A\n", 2);
    printf("Child %d: Transmitting file '%s' to client\n", pid, pathname);
    fflush(stdout);

    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (write(data_conn, buffer, bytes_read) < 0) {
            break;
        }
    }

    close(file_fd);
    close(data_conn);
}

void handle_put(int client_sock, int data_sock, const char *pathname) {
    pid_t pid = getpid();
    int data_conn = accept(data_sock, NULL, NULL);
    if (data_conn < 0) {
        write(client_sock, "EError accepting data connection\n", 33);
        return;
    }

    int file_fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "EError creating file: %s\n", strerror(errno));
        write(client_sock, error_msg, strlen(error_msg));
        close(data_conn);
        return;
    }

    write(client_sock, "A\n", 2);
    printf("Child %d: Receiving file '%s' from client\n", pid, pathname);
    fflush(stdout);

    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(data_conn, buffer, sizeof(buffer))) > 0) {
        if (write(file_fd, buffer, bytes_read) < 0) {
            break;
        }
    }

    close(file_fd);
    close(data_conn);
}

int receive_command(int sock_fd, char *buffer, size_t buffer_size) {
    ssize_t total_read = 0;
    while (1) {
        char c;
        ssize_t r = read(sock_fd, &c, 1);
        if (r <= 0) {
            return -1;
        }

        if (c == '\n') {
            buffer[total_read] = '\0';
            break;
        }

        if (total_read < (ssize_t)(buffer_size - 1)) {
            buffer[total_read++] = c;
        } else {
            buffer[total_read] = '\0';
            break;
        }
    }
    return 0;
}

void handle_client(int client_sock) {
    pid_t pid = getpid();
    int data_listen_fd = -1;
    int data_fd = -1;
    char buffer[BUFFER_SIZE];

    while (1) {
        if (receive_command(client_sock, buffer, sizeof(buffer)) < 0) {
            break;
        }

        if (buffer[0] == '\0') {
            continue;
        }

        char cmd = buffer[0];
        char *arg = buffer + 1;
        while (*arg == ' ') arg++;

        if (cmd == 'D') {
            if (*arg != '\0') {
                write(client_sock, "E D command takes no arguments\n", 31);
            } else {
                data_listen_fd = handle_data_connection(client_sock);
                if (data_listen_fd < 0) {
                    write(client_sock, "E data connection failed\n", 25);
                }
            }

        } else if (cmd == 'C') {
            if (*arg == '\0') {
                write(client_sock, "E Path required for 'C' command\n", 33);
            } else {
                handle_rcd(client_sock, arg);
            }

        } else if (cmd == 'L') {
            if (*arg != '\0') {
                write(client_sock, "E L command takes no arguments\n", 31);
            } else if (data_listen_fd < 0) {
                write(client_sock, "E No data connection established\n", 33);
            } else {
                handle_rls(client_sock, data_listen_fd);
                close(data_listen_fd);
                data_listen_fd = -1;
            }

        } else if (cmd == 'G') {
            if (data_listen_fd < 0) {
                write(client_sock, "E No data connection established\n", 33);
            } else {
                handle_get(client_sock, data_listen_fd, arg);
                close(data_listen_fd);
                data_listen_fd = -1;
            }

        } else if (cmd == 'P') {
            if (data_listen_fd < 0) {
                write(client_sock, "E No data connection established\n", 33);
            } else {
                handle_put(client_sock, data_listen_fd, arg);
                close(data_listen_fd);
                data_listen_fd = -1;
            }

        } else if (cmd == 'Q') {
            if (*arg == '\0') {
                write(client_sock, "A\n", 2);
                break;
            } else {
                write(client_sock, "E Q command takes no arguments\n", 31);
            }

        } else {
            write(client_sock, "E Unknown command\n", 18);
        }
    }

    if (data_fd >= 0) {
        close(data_fd);
        data_fd = -1;
    }
    if (data_listen_fd >= 0) {
        close(data_listen_fd);
        data_listen_fd = -1;
    }

    close(client_sock);
    printf("Child %d: Quitting\n", pid);
    fflush(stdout);

    exit(0);
}

void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void client_connection(int server_sock) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock;

    signal(SIGCHLD, handle_sigchld);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                fprintf(stderr, "Error: %s\n", strerror(errno));
                fflush(stdout);
                break;
            }
        }

        printf("Connection established with client.\n");
        fflush(stdout);

        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
            fflush(stdout);
            close(client_sock);
        } else if (pid == 0) {
            close(server_sock);
            handle_client(client_sock);
        } else {
            close(client_sock);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    if (port <= 0) {
        fprintf(stderr, "Error: invalid port number\n");
        exit(EXIT_FAILURE);
    }

    int server_sock = setup_server(port);
    client_connection(server_sock);

    close(server_sock);
    return 0;
}