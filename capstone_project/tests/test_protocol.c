#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "protocol.h"

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do {                          \
    if (cond) { printf("PASS: %s\n", msg); passed++; } \
    else      { printf("FAIL: %s\n", msg); failed++; } \
} while (0)

/* Write all of data to fd. Returns 0 on success, -1 on failure. */
static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n <= 0)
            return -1;
        data += n;
        len  -= (size_t)n;
    }
    return 0;
}

int main(void)
{
    command_t      cmd;
    char           line[128];
    char           buf[MAX_LINE_LEN];
    char           big[400];
    char           small[4];
    const char    *cpu = resource_to_string(RES_COMPUTE_CPU);
    conn_reader_t *reader = NULL;
    int            sv[2];

    /* ---------- Parsing: valid commands ---------- */
    snprintf(line, sizeof(line), "USAGE 7 %s 3600", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_OK, "USAGE parses");
    CHECK(cmd.kind == CMD_USAGE && cmd.account_id == 7 &&
          cmd.type == RES_COMPUTE_CPU && cmd.quantity == 3600,
          "USAGE fields filled in");

    snprintf(line, sizeof(line), "TOTAL 4294967295 %s", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_OK &&
          cmd.kind == CMD_TOTAL && cmd.account_id == 4294967295U,
          "TOTAL with the largest account id");

    CHECK(protocol_parse("INVOICE 7 0 1800000000", &cmd) == PROTO_OK &&
          cmd.kind == CMD_INVOICE && cmd.from == 0 && cmd.to == 1800000000,
          "INVOICE parses with from and to");

    CHECK(protocol_parse("INVOICE 7 0 9223372036854775807", &cmd) == PROTO_OK &&
          cmd.to == INT64_MAX, "INVOICE accepts INT64_MAX");

    CHECK(protocol_parse("QUIT", &cmd) == PROTO_OK && cmd.kind == CMD_QUIT,
          "QUIT parses");

    snprintf(line, sizeof(line), "USAGE 7 %s 1000000000", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_OK, "quantity at MAX_QUANTITY");

    snprintf(line, sizeof(line), "USAGE 7 %s 3600", cpu);
    protocol_parse(line, &cmd);
    {
        char expect[128];
        snprintf(expect, sizeof(expect), "USAGE 7 %s 3600", cpu);
        CHECK(strcmp(line, expect) == 0, "parse does not change the line");
    }

    /* ---------- Parsing: bad command / format ---------- */
    CHECK(protocol_parse("", &cmd) == PROTO_ERR_BAD_COMMAND, "empty line");
    CHECK(protocol_parse("HELLO", &cmd) == PROTO_ERR_BAD_COMMAND, "unknown command");
    CHECK(protocol_parse("quit", &cmd) == PROTO_ERR_BAD_COMMAND, "lowercase command");
    CHECK(protocol_parse("QUIT now", &cmd) == PROTO_ERR_BAD_COMMAND, "QUIT with extra word");
    CHECK(protocol_parse("QUIT ", &cmd) == PROTO_ERR_BAD_COMMAND, "trailing space");
    CHECK(protocol_parse(" QUIT", &cmd) == PROTO_ERR_BAD_COMMAND, "leading space");
    CHECK(protocol_parse("QUIT\r", &cmd) == PROTO_ERR_BAD_COMMAND, "carriage return");
    CHECK(protocol_parse("INVOICE  7 0 10", &cmd) == PROTO_ERR_BAD_COMMAND, "double space");
    CHECK(protocol_parse("INVOICE 7 0", &cmd) == PROTO_ERR_BAD_COMMAND, "INVOICE missing a word");
    CHECK(protocol_parse("INVOICE 7 0 10 20", &cmd) == PROTO_ERR_BAD_COMMAND, "too many words");

    snprintf(line, sizeof(line), "TOTAL 0 %s", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_COMMAND, "account 0");
    snprintf(line, sizeof(line), "TOTAL 4294967296 %s", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_COMMAND, "account too big");
    snprintf(line, sizeof(line), "TOTAL -1 %s", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_COMMAND, "negative account");

    CHECK(protocol_parse("INVOICE 7 10 10", &cmd) == PROTO_ERR_BAD_COMMAND, "from == to");
    CHECK(protocol_parse("INVOICE 7 20 10", &cmd) == PROTO_ERR_BAD_COMMAND, "from > to");
    CHECK(protocol_parse("INVOICE 7 0 9223372036854775808", &cmd) == PROTO_ERR_BAD_COMMAND,
          "timestamp one past INT64_MAX");

    /* ---------- Parsing: bad type / quantity ---------- */
    CHECK(protocol_parse("USAGE 99 BOGUS -5", &cmd) == PROTO_ERR_BAD_TYPE,
          "USAGE 99 BOGUS -5 is bad_type (the header's example)");
    CHECK(protocol_parse("TOTAL 7 cpu", &cmd) == PROTO_ERR_BAD_TYPE, "lowercase type");

    snprintf(line, sizeof(line), "USAGE 7 %s 0", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_QUANTITY, "quantity 0");
    snprintf(line, sizeof(line), "USAGE 7 %s 1000000001", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_QUANTITY, "quantity over MAX_QUANTITY");
    snprintf(line, sizeof(line), "USAGE 7 %s -5", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_QUANTITY, "negative quantity");
    snprintf(line, sizeof(line), "USAGE 7 %s 12abc", cpu);
    CHECK(protocol_parse(line, &cmd) == PROTO_ERR_BAD_QUANTITY, "quantity with letters");

    /* ---------- Replies ---------- */
    protocol_reply_ok(buf, sizeof(buf), 250);
    CHECK(strcmp(buf, "OK 250\n") == 0, "reply OK 250");
    protocol_reply_bye(buf, sizeof(buf));
    CHECK(strcmp(buf, "OK bye\n") == 0, "reply OK bye");
    protocol_reply_err(buf, sizeof(buf), PROTO_ERR_BAD_TYPE);
    CHECK(strcmp(buf, "ERR bad_type\n") == 0, "reply ERR bad_type");
    protocol_reply_ok(small, sizeof(small), 123456);
    CHECK(strlen(small) == 3, "reply into a tiny buffer is cut off safely");

    CHECK(protocol_error_from_status(BILL_ERR_NOT_FOUND) == PROTO_ERR_UNKNOWN_ACCOUNT,
          "NOT_FOUND -> unknown_account");
    CHECK(protocol_error_from_status(BILL_ERR_LOCK) == PROTO_ERR_BUSY, "LOCK -> busy");
    CHECK(protocol_error_from_status(BILL_ERR_IO) == PROTO_ERR_INTERNAL, "IO -> internal");

    /* ---------- Line reader (two connected sockets, no network) ---------- */
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    CHECK(conn_reader_create(sv[1], &reader) == BILL_OK, "reader created");

    /* Two lines in one write */
    send_all(sv[0], "QUIT\nHELLO\n", 11);
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_OK &&
          strcmp(buf, "QUIT") == 0, "first of two lines");
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_OK &&
          strcmp(buf, "HELLO") == 0, "second of two lines");

    /* Exactly the limit: 255 characters */
    memset(big, 'B', 255);
    big[255] = '\n';
    send_all(sv[0], big, 256);
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_OK &&
          strlen(buf) == 255, "255-character line is accepted");

    /* One over the limit, then a normal line */
    memset(big, 'A', 256);
    big[256] = '\n';
    send_all(sv[0], big, 257);
    send_all(sv[0], "QUIT\n", 5);
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_TOO_LONG,
          "256-character line is too long");
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_OK &&
          strcmp(buf, "QUIT") == 0, "next line after a too-long one is fine");

    /* NUL byte inside a line, then a normal line */
    send_all(sv[0], "QU\0IT\nQUIT\n", 11);
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_BAD,
          "line with a NUL byte is rejected");
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_OK &&
          strcmp(buf, "QUIT") == 0, "next line after a NUL line is fine");

    /* \r is kept, so the parser can reject it */
    send_all(sv[0], "QUIT\r\n", 6);
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_OK &&
          strcmp(buf, "QUIT\r") == 0, "carriage return is kept in the line");

    /* Client disconnects in the middle of a line */
    send_all(sv[0], "QUI", 3);
    close(sv[0]);
    CHECK(conn_reader_read_line(reader, buf, sizeof(buf)) == READ_LINE_EOF,
          "half line then disconnect is EOF");

    conn_reader_destroy(reader);
    close(sv[1]);

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
