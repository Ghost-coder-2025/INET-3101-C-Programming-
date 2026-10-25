#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "common.h"

#define REPLY_TIMEOUT_SEC 5      /* give up if the server is silent this long */
#define NUM_USAGE         5      /* how many USAGE lines the script sends */

/* The usage this fake cloud machine reports. */
typedef struct {
    resource_t type;
    long long  quantity;
} usage_t;

static const usage_t SCRIPT[NUM_USAGE] = {
    { RES_COMPUTE_CPU, 3600 },   /* 1 vCPU-hour, in seconds */
    { RES_COMPUTE_RAM, 7200 },   /* 2 GB-hours, in seconds */
    { RES_STORAGE,     2048 },   /* MB stored */
    { RES_NET_IN,       500 },   /* MB received */
    { RES_NET_OUT,     2000 }    /* MB sent */
};

/* Send all of data. Returns 0 on success, -1 on failure. */
static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        data += n;
        len  -= (size_t)n;
    }
    return 0;
}

/* Read one reply line (up to the newline) into buf. Returns 0 on success,
   -1 if the server closed the connection, timed out, or sent too much. */
static int read_reply(int fd, char *buf, size_t buflen)
{
    size_t used = 0;

    while (used + 1 < buflen) {
        char    c;
        ssize_t n = recv(fd, &c, 1, 0);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        if (c == '\n') {
            buf[used] = '\0';
            return 0;
        }
        buf[used++] = c;
    }
    buf[used] = '\0';
    return -1;
}

/* Send one command and print it with the server's reply.
   Returns 0 for an OK reply, 1 for an ERR reply, -1 if talking failed. */
static int run_command(int fd, const char *command)
{
    char line[MAX_LINE_LEN];
    char reply[MAX_LINE_LEN];
    int  len = snprintf(line, sizeof(line), "%s\n", command);

    if (len < 0 || (size_t)len >= sizeof(line))
        return -1;

    printf("> %s\n", command);
    if (send_all(fd, line, (size_t)len) != 0) {
        fprintf(stderr, "client: send failed\n");
        return -1;
    }
    if (read_reply(fd, reply, sizeof(reply)) != 0) {
        fprintf(stderr, "client: no reply from the server\n");
        return -1;
    }
    printf("< %s\n", reply);

    return strncmp(reply, "ERR", 3) == 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
    struct sockaddr_in addr;
    struct timeval     timeout;
    char               commands[NUM_USAGE + 2][128];
    unsigned long      id;
    char              *end = NULL;
    int                fd;
    int                failed = 0;
    size_t             i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <account_id>\n", argv[0]);
        return 2;
    }

    /* The account id must be digits only, from 1 to 4294967295 */
    errno = 0;
    id = strtoul(argv[1], &end, 10);
    if (!isdigit((unsigned char)argv[1][0]) || *end != '\0' ||
        errno != 0 || id == 0 || id > UINT32_MAX) {
        fprintf(stderr, "client: the account id must be a whole number "
                        "from 1 to 4294967295\n");
        return 2;
    }

    /* Writing to a server that has gone away must not kill the client */
    signal(SIGPIPE, SIG_IGN);

    /* Build the whole script first: the USAGE lines, then TOTAL, then QUIT */
    for (i = 0; i < NUM_USAGE; i++)
        snprintf(commands[i], sizeof(commands[i]), "USAGE %lu %s %lld",
                 id, resource_to_string(SCRIPT[i].type), SCRIPT[i].quantity);
    snprintf(commands[NUM_USAGE], sizeof(commands[NUM_USAGE]),
             "TOTAL %lu NET_OUT", id);
    snprintf(commands[NUM_USAGE + 1], sizeof(commands[NUM_USAGE + 1]), "QUIT");

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("client: socket");
        return 1;
    }

    /* Never wait forever for the server */
    timeout.tv_sec  = REPLY_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("client: connect (is the server running?)");
        close(fd);
        return 1;
    }

    for (i = 0; i < NUM_USAGE + 2; i++) {
        int rc = run_command(fd, commands[i]);

        if (rc < 0) {                /* the connection is broken: stop */
            failed = 1;
            break;
        }
        if (rc > 0)                  /* the server said ERR: keep going */
            failed = 1;
    }

    close(fd);
    return failed ? 1 : 0;
}
