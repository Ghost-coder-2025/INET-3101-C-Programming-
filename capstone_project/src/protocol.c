#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "protocol.h"

/* ---------- Part 1: replies ---------- */

/* The text a client sees after "ERR ". */
static const char *proto_error_text(proto_error_t err)
{
    switch (err) {
    case PROTO_OK:                  return "ok";
    case PROTO_ERR_UNKNOWN_ACCOUNT: return "unknown_account";
    case PROTO_ERR_BAD_TYPE:        return "bad_type";
    case PROTO_ERR_BAD_QUANTITY:    return "bad_quantity";
    case PROTO_ERR_BAD_COMMAND:     return "bad_command";
    case PROTO_ERR_LINE_TOO_LONG:   return "line_too_long";
    case PROTO_ERR_BUSY:            return "busy";
    case PROTO_ERR_INTERNAL:        return "internal";
    }
    return "internal";
}

proto_error_t protocol_error_from_status(status_t status)
{
    switch (status) {
    case BILL_OK:            return PROTO_OK;
    case BILL_ERR_NOT_FOUND: return PROTO_ERR_UNKNOWN_ACCOUNT;
    case BILL_ERR_INVALID:   return PROTO_ERR_BAD_COMMAND;
    case BILL_ERR_OVERFLOW:  return PROTO_ERR_BAD_QUANTITY;
    case BILL_ERR_LOCK:      return PROTO_ERR_BUSY;
    default:                 return PROTO_ERR_INTERNAL;
    }
}

void protocol_reply_ok(char *buf, size_t buflen, int64_t value)
{
    if (buf == NULL || buflen == 0)
        return;
    snprintf(buf, buflen, "OK %lld\n", (long long)value);
}

void protocol_reply_bye(char *buf, size_t buflen)
{
    if (buf == NULL || buflen == 0)
        return;
    snprintf(buf, buflen, "OK bye\n");
}

void protocol_reply_err(char *buf, size_t buflen, proto_error_t err)
{
    if (buf == NULL || buflen == 0)
        return;
    snprintf(buf, buflen, "ERR %s\n", proto_error_text(err));
}

/* ---------- Part 2: parsing ---------- */

#define MAX_WORDS 4                  /* the longest commands have 4 words */

/* Split s in place at single spaces. Fills words[] and returns how many
   words there are, or -1 if there is an empty word (leading, trailing or
   double space) or more than max words. */
static int split_words(char *s, char *words[], int max)
{
    int   n = 0;
    char *p = s;

    for (;;) {
        if (*p == '\0' || *p == ' ')
            return -1;               /* empty word */
        if (n == max)
            return -1;               /* too many words */
        words[n++] = p;

        p = strchr(p, ' ');
        if (p == NULL)
            return n;                /* that was the last word */
        *p = '\0';
        p++;
    }
}

/* Parse a string of digits only (no sign, no spaces) into *out.
   Returns 0 if it is all digits and no bigger than max, else -1. */
static int parse_digits(const char *s, int64_t max, int64_t *out)
{
    int64_t v = 0;

    if (*s == '\0')
        return -1;

    for (; *s != '\0'; s++) {
        int d;

        if (*s < '0' || *s > '9')
            return -1;
        d = *s - '0';
        if (v > (max - d) / 10)
            return -1;               /* v * 10 + d would be bigger than max */
        v = v * 10 + d;
    }

    *out = v;
    return 0;
}

proto_error_t protocol_parse(const char *line, command_t *out)
{
    char      copy[MAX_LINE_LEN];
    char     *words[MAX_WORDS];
    command_t cmd;
    int64_t   v;
    size_t    len, i;
    int       n, want = 0;

    if (line == NULL || out == NULL)
        return PROTO_ERR_BAD_COMMAND;

    /* Work on a copy so the caller's line is never changed */
    len = strlen(line);
    if (len >= sizeof(copy))
        return PROTO_ERR_BAD_COMMAND;
    memcpy(copy, line, len + 1);

    /* No control characters anywhere, including '\r' */
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)copy[i];
        if (c < 0x20 || c == 0x7f)
            return PROTO_ERR_BAD_COMMAND;
    }

    n = split_words(copy, words, MAX_WORDS);
    if (n < 1)
        return PROTO_ERR_BAD_COMMAND;

    memset(&cmd, 0, sizeof(cmd));

    /* Rule 1: the command word, and how many words it needs */
    if (strcmp(words[0], "USAGE") == 0) {
        cmd.kind = CMD_USAGE;
        want = 4;
    } else if (strcmp(words[0], "TOTAL") == 0) {
        cmd.kind = CMD_TOTAL;
        want = 3;
    } else if (strcmp(words[0], "INVOICE") == 0) {
        cmd.kind = CMD_INVOICE;
        want = 4;
    } else if (strcmp(words[0], "QUIT") == 0) {
        cmd.kind = CMD_QUIT;
        want = 1;
    } else {
        return PROTO_ERR_BAD_COMMAND;
    }

    /* Rule 2: the right number of words */
    if (n != want)
        return PROTO_ERR_BAD_COMMAND;

    /* Rule 3: account id, 1 to 4294967295 */
    if (cmd.kind != CMD_QUIT) {
        if (parse_digits(words[1], 4294967295LL, &v) != 0 || v < 1)
            return PROTO_ERR_BAD_COMMAND;
        cmd.account_id = (uint32_t)v;
    }

    /* Rule 4: resource type (USAGE and TOTAL) */
    if (cmd.kind == CMD_USAGE || cmd.kind == CMD_TOTAL) {
        if (resource_from_string(words[2], &cmd.type) != BILL_OK)
            return PROTO_ERR_BAD_TYPE;
    }

    /* Rule 5: quantity (USAGE only), 1 to MAX_QUANTITY */
    if (cmd.kind == CMD_USAGE) {
        if (parse_digits(words[3], MAX_QUANTITY, &v) != 0 || v < 1)
            return PROTO_ERR_BAD_QUANTITY;
        cmd.quantity = v;
    }

    /* Rule 6: from and to (INVOICE only), from must be before to */
    if (cmd.kind == CMD_INVOICE) {
        if (parse_digits(words[2], INT64_MAX, &cmd.from) != 0 ||
            parse_digits(words[3], INT64_MAX, &cmd.to) != 0 ||
            cmd.from >= cmd.to)
            return PROTO_ERR_BAD_COMMAND;
    }

    *out = cmd;
    return PROTO_OK;
}

/* ---------- Part 3: reading lines ---------- */

struct conn_reader {
    int    fd;                       /* the socket (not owned by the reader) */
    char   data[1024];               /* bytes received but not used yet */
    size_t pos;                      /* next unused byte in data */
    size_t len;                      /* how many bytes in data are valid */
};

status_t conn_reader_create(int fd, conn_reader_t **out)
{
    conn_reader_t *r;

    if (fd < 0 || out == NULL)
        return BILL_ERR_INVALID;

    r = malloc(sizeof(*r));
    if (r == NULL)
        return BILL_ERR_NOMEM;

    memset(r, 0, sizeof(*r));
    r->fd = fd;
    *out = r;
    return BILL_OK;
}

void conn_reader_destroy(conn_reader_t *reader)
{
    free(reader);                    /* free(NULL) is safe */
}

/* Get the next byte from the socket, calling read() only when the buffer
   is empty. Returns 1 with the byte in *c, 0 if the client closed the
   connection, or -1 if the read failed or timed out. */
static int next_byte(conn_reader_t *r, char *c)
{
    if (r->pos == r->len) {
        ssize_t got;

        do {
            got = read(r->fd, r->data, sizeof(r->data));
        } while (got < 0 && errno == EINTR);   /* interrupted by a signal: retry */

        if (got == 0)
            return 0;
        if (got < 0)
            return -1;

        r->pos = 0;
        r->len = (size_t)got;
    }

    *c = r->data[r->pos++];
    return 1;
}

read_line_result_t conn_reader_read_line(conn_reader_t *reader, char *buf,
                                         size_t buflen)
{
    size_t n = 0;
    int    too_long = 0;
    int    bad = 0;
    char   c;
    int    rc;

    if (reader == NULL || buf == NULL || buflen < MAX_LINE_LEN)
        return READ_LINE_ERROR;

    for (;;) {
        rc = next_byte(reader, &c);
        if (rc < 0)
            return READ_LINE_ERROR;
        if (rc == 0) {
            buf[0] = '\0';           /* a half line at disconnect is dropped */
            return READ_LINE_EOF;
        }

        if (c == '\n')
            break;                   /* end of the line */

        if (c == '\0') {
            bad = 1;                 /* keep reading to the end of the line */
            continue;
        }

        if (n < MAX_LINE_LEN - 1)
            buf[n++] = c;
        else
            too_long = 1;            /* keep reading, but stop saving */
    }

    if (too_long) {
        buf[0] = '\0';
        return READ_LINE_TOO_LONG;
    }
    if (bad) {
        buf[0] = '\0';
        return READ_LINE_BAD;
    }

    buf[n] = '\0';
    return READ_LINE_OK;
}
