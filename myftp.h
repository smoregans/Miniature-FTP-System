#ifndef MYFTP_H
#define MYFTP_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define BACKLOG 4

int setup_server(int port);
int handle_data_connection(int client_sock);
void handle_client(int client_sock);
void handle_rcd(int client_sock, const char *pathname);
void handle_rls(int client_sock, int data_sock);
void handle_get(int client_sock, int data_sock, const char *pathname);
void handle_put(int client_sock, int data_sock, const char *pathname);
int receive_command(int sock_fd, char *buffer, size_t buffer_size);
void handle_client(int client_sock);
void handle_sigchld(int sig);
void client_connection(int server_sock);

int connect_to_server(const char *hostname, int port);
int setup_data_connection(int control_sock, const char *server_address);
void exit_command(int control_sock);
void cd(const char *pathname);
void rcd(int control_sock, const char *pathname);
void ls();
void rls(int control_sock, const char *server_address);
void get(int control_sock, const char *server_address, const char *filename);
void show(int control_sock, const char *server_address, const char *pathname);
void put(int control_sock, const char *server_address, const char *pathname);

#endif
