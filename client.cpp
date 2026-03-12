#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT 8784
#define BUFFER_SIZE 1024
#define MAX_HOSTNAME_LENGTH 256

int main(int argc, char *argv[]) {
    char hostname[MAX_HOSTNAME_LENGTH];

    int client_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};
    struct hostent *h;

    // Usage: ./client <server_hostname>
    if (argc < 2) { 
        printf("Please provide the hostname: ");
        fgets(hostname, MAX_HOSTNAME_LENGTH, stdin);

        char *newline = strchr(hostname, '\n');
        if (newline) *newline = '\0';
    }
    else {
        strncpy(hostname, argv[1], MAX_HOSTNAME_LENGTH - 1);
        hostname[MAX_HOSTNAME_LENGTH - 1] = '\0';
    }

    // Resolve the given hostname
    if ((h = gethostbyname(hostname)) == NULL) {
        perror("Unknown host");
        exit(EXIT_FAILURE);
    }

    // Print all IP addresses
    printf("IP Addresses:\n");
    char **addr_list;
    for (addr_list = h->h_addr_list; *addr_list != NULL; addr_list++) {
        printf("\t%s\n", inet_ntoa(*(struct in_addr *)*addr_list));
    }

    // Create socket using TCP (SOCK_STREAM) with IPv4 (AF_INET)
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy((char *) &server_addr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
    server_addr.sin_port = htons(PORT);

    // Handle the connection from client to the server
    if (connect(client_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Cannot connect");
        exit(EXIT_FAILURE);
    }

    printf("Connect to server %s successfully!\n", inet_ntoa(server_addr.sin_addr));
     
    return 0;
}