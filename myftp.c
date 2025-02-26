#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "myftp.h"

int connect_to_server(const char* hostname, int port) {
	int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;

    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int setup_data_connection(int control_sock, const char *server_address) {
    if (write(control_sock, "D\n", 2) < 0) {
        fprintf(stderr, "Error: Unable to send data connection request\n");
        return -1;
    }

    char buffer[BUFFER_SIZE];
    int bytes_read = read(control_sock, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        fprintf(stderr, "Error: Unable to read server response for data connection\n");
        return -1;
    }

    buffer[bytes_read] = '\0';

    if (buffer[0] != 'A' || strlen(buffer) <= 1) {
        fprintf(stderr, "Error: Invalid or missing port in server response: %s\n", buffer);
        return -1;
    }

    int port = atoi(buffer + 1);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number received from server: %d\n", port);
        return -1;
    }

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
        fprintf(stderr, "Error: Unable to create data socket\n");
        return -1;
    }

    struct sockaddr_in data_addr;
    struct hostent *server = gethostbyname(server_address);
    if (server == NULL) {
        fprintf(stderr, "Error: Unable to resolve server address: %s\n", server_address);
        close(data_sock);
        return -1;
    }

    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    memcpy(&data_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    data_addr.sin_port = htons(port);

    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        fprintf(stderr, "Error: Unable to connect to data port\n");
        close(data_sock);
        return -1;
    }

    return data_sock;
}

void exit_command(int control_sock) {
    if (write(control_sock, "Q\n", 2) < 0) {
        fprintf(stderr, "Error: Unable to send quit command to server\n");
    }
    close(control_sock);
    exit(0);
}

void cd(const char *pathname) {
	if (pathname == NULL || strlen(pathname) == 0) {
		fprintf(stderr, "Usage: cd <path>\n");
		return;
	}

    if (chdir(pathname) < 0) {
        fprintf(stderr, "Error changing directory to '%s': %s\n", pathname, strerror(errno));
    }
}

void rcd(int control_sock, const char *pathname) {
    if (pathname == NULL || strlen(pathname) == 0) {
        fprintf(stderr, "Error: Missing pathname for rcd command\n");
        return;
    }

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "C%s\n", pathname);
    if (write(control_sock, buffer, strlen(buffer)) < 0) {
        fprintf(stderr, "Error: Unable to send rcd command\n");
        return;
    }

    memset(buffer, 0, sizeof(buffer));
    int bytes_read = read(control_sock, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        fprintf(stderr, "Error: No response from server for rcd command\n");
        return;
    }

    if (buffer[0] == 'A') {
        printf("Remote directory changed to %s\n", pathname);
    } else if (buffer[0] == 'E') {
        fprintf(stderr, "Error from server: %s\n", buffer + 1);
    } else {
        fprintf(stderr, "Unexpected response from server: %s\n", buffer);
    }
}

void ls() {
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        fprintf(stderr, "Error: pipe failed\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: fork failed\n");
        return;
    }

    if (pid == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) {
            fprintf(stderr, "Error: dup2 failed for STDOUT\n");
            exit(EXIT_FAILURE);
        }
        close(pipe_fd[1]);
        execlp("ls", "ls", "-l", NULL);
        fprintf(stderr, "Error: execlp failed for ls\n");
        exit(EXIT_FAILURE);
    } else {
        close(pipe_fd[1]);
        pid_t more_pid = fork();
        if (more_pid < 0) {
            fprintf(stderr, "Error: fork failed for more\n");
            close(pipe_fd[0]);
            return;
        }

        if (more_pid == 0) {
            if (dup2(pipe_fd[0], STDIN_FILENO) < 0) {
                fprintf(stderr, "Error: dup2 failed for STDIN\n");
                exit(EXIT_FAILURE);
            }
            close(pipe_fd[0]);
            execlp("more", "more", "-20", NULL);
            fprintf(stderr, "Error: execlp failed for more\n");
            exit(EXIT_FAILURE);
        } else {
            close(pipe_fd[0]);
            wait(NULL);
        }

        wait(NULL);
    }
}

void rls(int control_sock, const char *server_address) {
    int data_sock = setup_data_connection(control_sock, server_address);
    if (data_sock < 0) {
        fprintf(stderr, "Error: Failed to establish data connection\n");
        return;
    }

    if (write(control_sock, "L\n", 2) < 0) {
        fprintf(stderr, "Error: Failed to send rls command\n");
        close(data_sock);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: fork failed\n");
        close(data_sock);
        return;
    }

    if (pid == 0) {
        if (dup2(data_sock, STDIN_FILENO) < 0) {
            fprintf(stderr, "Error: dup2 failed\n");
            exit(EXIT_FAILURE);
        }
        close(data_sock);
        execlp("more", "more", "-20", NULL);
        fprintf(stderr, "Error: execlp failed for more\n");
        exit(EXIT_FAILURE);
    } else {
        close(data_sock);
        wait(NULL);
    }

    char buffer[BUFFER_SIZE] = {0};
    int bytes_read = read(control_sock, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
    } else {
        fprintf(stderr, "Error: Failed to read server acknowledgment\n");
    }
}

void get(int control_sock, const char *server_address, const char *filename) {
    if (filename == NULL || strlen(filename) == 0) {
        fprintf(stderr, "Error: Usage: get <filename>\n");
        return;
    }

    int data_sock = setup_data_connection(control_sock, server_address);
    if (data_sock < 0) {
        fprintf(stderr, "Error: Failed to establish data connection\n");
        return;
    }

    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "G%s\n", filename);
    if (write(control_sock, command, strlen(command)) < 0) {
        fprintf(stderr, "Error: Failed to send get command\n");
        close(data_sock);
        return;
    }

    char ack_buffer[BUFFER_SIZE];
    int ack_bytes = read(control_sock, ack_buffer, sizeof(ack_buffer) - 1);
    if (ack_bytes <= 0 || ack_buffer[0] != 'A') {
        ack_buffer[ack_bytes] = '\0';
        fprintf(stderr, "Error: Server failed to acknowledge get command: %s\n", ack_buffer);
        close(data_sock);
        return;
    }

    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        fprintf(stderr, "Error: Unable to open local file for writing: %s\n", filename);
        close(data_sock);
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = read(data_sock, buffer, sizeof(buffer))) > 0) {
        if (write(file_fd, buffer, bytes_read) < 0) {
            fprintf(stderr, "Error: Unable to write to local file: %s\n", filename);
            break;
        }
    }

    if (bytes_read < 0) {
        fprintf(stderr, "Error: Failed to read data from server\n");
    }

    close(file_fd);
    close(data_sock);
}

void show(int control_sock, const char *server_address, const char *pathname) {
    if (pathname == NULL || strlen(pathname) == 0) {
        fprintf(stderr, "Error: Missing pathname for show command\n");
        return;
    }

    int data_sock = setup_data_connection(control_sock, server_address);
    if (data_sock < 0) {
        fprintf(stderr, "Error: Failed to establish data connection\n");
        return;
    }

    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "G%s\n", pathname);
    if (write(control_sock, command, strlen(command)) < 0) {
        fprintf(stderr, "Error: Unable to send show command\n");
        close(data_sock);
        return;
    }

    char ack_buffer[BUFFER_SIZE];
    int ack_bytes = read(control_sock, ack_buffer, sizeof(ack_buffer) - 1);
    if (ack_bytes <= 0 || ack_buffer[0] != 'A') {
        ack_buffer[ack_bytes] = '\0';
        fprintf(stderr, "Error: Server failed to acknowledge show command: %s\n", ack_buffer);
        close(data_sock);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: fork failed\n");
        close(data_sock);
        return;
    }

    if (pid == 0) {
        if (dup2(data_sock, STDIN_FILENO) < 0) {
            fprintf(stderr, "Error: dup2 failed\n");
            exit(EXIT_FAILURE);
        }

        close(data_sock);

        execlp("more", "more", "-20", NULL);
        fprintf(stderr, "Error: execlp failed\n");
        exit(EXIT_FAILURE);
    } else {
        close(data_sock);
        wait(NULL);
    }
}

void put(int control_sock, const char *server_address, const char *filename) {
    if (filename == NULL || strlen(filename) == 0) {
        fprintf(stderr, "Error: Usage: put <filename>\n");
        return;
    }

    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        fprintf(stderr, "Error: Unable to open local file for reading: %s\n", filename);
        return;
    }

    int data_sock = setup_data_connection(control_sock, server_address);
    if (data_sock < 0) {
        fprintf(stderr, "Error: Failed to establish data connection\n");
        close(file_fd);
        return;
    }

    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "P%s\n", filename);
    if (write(control_sock, command, strlen(command)) < 0) {
        fprintf(stderr, "Error: Failed to send put command\n");
        close(file_fd);
        close(data_sock);
        return;
    }

    char ack_buffer[BUFFER_SIZE];
    int ack_bytes = read(control_sock, ack_buffer, sizeof(ack_buffer) - 1);
    if (ack_bytes <= 0 || ack_buffer[0] != 'A') {
        ack_buffer[ack_bytes] = '\0';
        fprintf(stderr, "Error: Server failed to acknowledge put command: %s\n", ack_buffer);
        close(file_fd);
        close(data_sock);
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (write(data_sock, buffer, bytes_read) < 0) {
            fprintf(stderr, "Error: Failed to send file data to server\n");
            break;
        }
    }

    if (bytes_read < 0) {
        fprintf(stderr, "Error: Failed to read from local file\n");
    }

    close(file_fd);
    close(data_sock);
}

void command_server(int control_sock, const char *server_address) {
    char buffer[BUFFER_SIZE];

    while (1) {
        printf("MFTP> ");
        fflush(stdout);

        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            fprintf(stderr, "Error: Unable to read user input\n");
            continue;
        }

        if (bytes_read == 1) {
            continue;
        }

        if (buffer[bytes_read - 1] == '\n') {
            buffer[bytes_read - 1] = '\0';
        }

        if (strncmp(buffer, "cd ", 3) == 0) {
            cd(buffer + 3);
        } else if (strcmp(buffer, "ls") == 0) {
            ls();
        } else if (strncmp(buffer, "rcd ", 4) == 0) {
            rcd(control_sock, buffer + 4);
        } else if (strcmp(buffer, "rls") == 0) {
            rls(control_sock, server_address);
        } else if (strncmp(buffer, "get ", 4) == 0) {
            get(control_sock, server_address, buffer + 4);
        } else if (strncmp(buffer, "show ", 5) == 0) {
            show(control_sock, server_address, buffer + 5);
        } else if (strncmp(buffer, "put ", 4) == 0) {
            put(control_sock, server_address, buffer + 4);
        } else if (strcmp(buffer, "exit") == 0) {
            exit_command(control_sock);
        } else {
            fprintf(stderr, "Unknown command: %s\n", buffer);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <hostname | IP address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    const char *hostname = argv[2];

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: invalid port number\n");
        exit(EXIT_FAILURE);
    }

    int sockfd = connect_to_server(hostname, port);
    printf("Connected to server at %s\n", hostname);

    command_server(sockfd, hostname);

    return 0;
}