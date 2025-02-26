# Miniature-FTP-System
A miniature FTP system implemented in C, utilizing Unix TCP/IP sockets for client-server communication over IPv4.

1. Name and Email Address:
  - Name: Morgan Martin
  - Email: morgan.martin@wsu.edu

2. List of Files:
  - Makefile: Makefile to compile both myftp and myftpserve programs
  - myftp.c: Client file
  - myftpserve.c: Server file
  - myftp.h: Header file for both myftp.c and myftpserve.c

3. Compiler/Interpreter Version:
  - GCC (GNU Compiler Collection) 9.4.0 or later

4. Compile Instructions:
  - Run the Makefile using 'make' command in Linux terminal
  - Make sure all files are in the same directory.

5. Run Instructions:
  - To run the program, use the following command:

     ./myftpserve <port>
     ./myftp <port> <server_ip>

  - Example(s):

     ./myftpserve 2121
     ./myftp 2121 127.0.0.1
