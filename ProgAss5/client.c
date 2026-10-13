#include <stdio.h>        // printf, fgets, fwrite, perror
#include <string.h>       // strlen
#include <unistd.h>       // close
#include <sys/socket.h>   // socket, connect, send, recv
#include <netinet/in.h>   // sockaddr_in, htons (internet address structures)
#include <arpa/inet.h>    // inet_pton (IP address conversion)
# include <stdlib.h>



int main(int argc, char *argv[]) {
    const char *host = argc >= 2 ? argv[1] : "127.0.0.1";
    int port = argc >= 3 ? atoi(argv[2]) : 8080;
    // ask the user for the filename
    char filename[256];
    printf("Enter filename: ");
    if (fgets(filename, sizeof(filename), stdin) == NULL) {
        fprintf(stderr, "No input\n");
        return 1;
    }
    // note: fgets keeps the '\n', which we send as the end-of-request marker

    // create a new TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    // describe the server address (IPv4, port 8080, localhost)
    struct sockaddr_in server = {0};     
    server.sin_family = AF_INET;   // zero the whole struct
    server.sin_port = htons(port);
    inet_pton(AF_INET, host, &server.sin_addr);
    
    // connect to the server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");   // e.g. "Connection refused" if the server isn't running
        return 1;
    }

    // send the request (the filename)
    if (send(sock, filename, strlen(filename), 0) < 0) {
        perror("send");
        return 1;
    }

    // receive and print the response
    char buffer[1024];
    ssize_t n;
    // loop because TCP is a stream: one send may arrive as several recvs
    // recv returns 0 when the server closes the connection
    while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0)
        fwrite(buffer, 1, n, stdout);   // print exactly n bytes (no '\0' needed)
    if (n < 0) perror("recv");

    // clean up
    close(sock);
    return 0;
}