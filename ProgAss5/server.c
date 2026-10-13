#include <stdio.h>        // printf, fopen, fread, perror
#include <stdlib.h>       // atoi
#include <string.h>       // strlen, strcspn, strchr, strstr
#include <signal.h>       // sigaction, signal
#include <unistd.h>       // close
#include <sys/socket.h>   // socket, bind, listen, accept, send, recv
#include <netinet/in.h>   // sockaddr_in, htons, htonl
#include <sys/stat.h>     // asks what kind thing is this file?
#include <sys/time.h>     // struct timeval     

#define MAX_FILE 255      // max allowed file size in bytes
#define MAX_NAME 255      // max request size in bytes     <-- ADD


static volatile sig_atomic_t running = 1;   // set to 0 by Ctrl+C

// signal handler: just flip the flag, the main loop does the cleanup
static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

// send a text message to the client
static void reply(int client, const char *msg) {
    send(client, msg, strlen(msg), 0);
}

// Read the request into name until a newline arrives.
// Returns 0 = ok, -1 = client left or timed out, -2 = too long, -3 = null byte
static int read_request(int client, char *name, size_t size) {
    size_t used = 0;
    while (used < size - 1) {                      // never write past the buffer
        ssize_t n = recv(client, name + used, size - 1 - used, 0);
        if (n <= 0) return -1;                     // disconnect, timeout, or error
        if (memchr(name + used, '\0', (size_t)n)) return -3;   // null byte injection
        used += (size_t)n;

        char *nl = memchr(name, '\n', used);
        if (nl) {
            *nl = '\0';                            // end the string at the newline
            if (nl > name && nl[-1] == '\r') nl[-1] = '\0';
            return 0;
        }
    }
    return -2;                                     // buffer full, still no newline
}

// A name is valid if it can only refer to a normal file in this folder
static int valid_name(const char *name) {
    if (name[0] == '\0' || name[0] == '.') return 0;   // empty, "..", hidden files
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') return 0;         // no folders
        if ((unsigned char)*p < 32) return 0;          // no control characters
    }
    return 1;
}

// Handle one client. Just return on any error: main closes the socket.
static void handle_client(int client) {
    struct timeval tv = {5, 0};                        // give up after 5 seconds
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char name[MAX_NAME + 1];
    int r = read_request(client, name, sizeof(name));
    if (r == -1) return;
    if (r == -2) { reply(client, "ERROR: Request too long\n"); return; }
    if (r == -3 || !valid_name(name)) {
        reply(client, "ERROR: Invalid filename\n");
        return;
    }

    struct stat st;
    if (stat(name, &st) < 0 || !S_ISREG(st.st_mode)) {
        reply(client, "ERROR: File not found\n");
        return;
    }

    FILE *fp = fopen(name, "r");
    if (fp == NULL) {
        reply(client, "ERROR: File not found\n");
        return;
    }

    // read up to 256 bytes: if we get all 256, the file is too big
    char content[MAX_FILE + 1];
    size_t len = fread(content, 1, sizeof(content), fp);
    fclose(fp);

    if (len > MAX_FILE)
        reply(client, "ERROR: File too large (max 255 bytes)\n");
    else
        send(client, content, len, 0);
}

int main(int argc, char *argv[]) {
    int port = argc >= 2 ? atoi(argv[1]) : 9090;   // port from command line

    // set up Ctrl+C handling (no SA_RESTART, so accept() gets interrupted)
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);   // don't die if a client disconnects mid-send

    // create the listening socket
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }

    // allow restarting the server right away without "Address already in use"
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // describe the address to bind to (any local IP, our port)
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // bind to the port, then start listening
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }
    if (listen(listener, 5) < 0) {
        perror("listen");
        close(listener);
        return 1;
    }
    printf("Server listening on port %d (Ctrl+C to stop)\n", port);

    

        // handle one client at a time until Ctrl+C
    while (running) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) continue;   // Ctrl+C interrupts accept; loop then ends

        handle_client(client);
        close(client);              // the one place every client socket is closed
    }


        // Ctrl+C was pressed: release the port
    close(listener);
    printf("\nServer shut down cleanly.\n");
    return 0;
}