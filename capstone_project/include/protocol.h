#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"

/* The three commands a client can send. */
typedef enum {
    CMD_USAGE,                       /* USAGE <account_id> <TYPE> <quantity> */
    CMD_TOTAL,                       /* TOTAL <account_id> <TYPE> */
    CMD_QUIT                         /* QUIT */
} command_kind_t;

/* A parsed command. Which fields are filled in depends on kind:
   USAGE uses all of them, TOTAL ignores quantity, QUIT uses none. */
typedef struct {
    command_kind_t kind;
    uint32_t       account_id;
    resource_t     type;
    int64_t        quantity;
} command_t;

/* The error reasons a client can see. These match the text after "ERR ".
   protocol_parse can return only BAD_TYPE, BAD_QUANTITY and BAD_COMMAND.
   UNKNOWN_ACCOUNT comes from the server after it looks the account up,
   LINE_TOO_LONG from the line reader, and BUSY and INTERNAL from
   protocol_error_from_status. */
typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_UNKNOWN_ACCOUNT,       /* "unknown_account" */
    PROTO_ERR_BAD_TYPE,              /* "bad_type" */
    PROTO_ERR_BAD_QUANTITY,          /* "bad_quantity" */
    PROTO_ERR_BAD_COMMAND,           /* "bad_command" */
    PROTO_ERR_LINE_TOO_LONG,         /* "line_too_long" */
    PROTO_ERR_BUSY,                  /* "busy": a lock timed out, try again */
    PROTO_ERR_INTERNAL               /* "internal": server-side failure */
} proto_error_t;

/* ---- Reading lines from a connection ---- */

/* Collects bytes from a socket into whole lines. Contents are hidden
   (defined in protocol.c). One reader belongs to one connection and one
   thread. */
typedef struct conn_reader conn_reader_t;

typedef enum {
    READ_LINE_OK,                    /* a complete line is in buf */
    READ_LINE_TOO_LONG,              /* line was too long; the rest of it was discarded */
    READ_LINE_BAD,                   /* line contained a NUL byte; it was discarded */
    READ_LINE_EOF,                   /* the client closed the connection */
    READ_LINE_ERROR                  /* read failed or timed out */
} read_line_result_t;

/* Create a reader for the connected socket fd. The reader does not own the
   fd, so the caller still closes it. On success, *out points to the reader. */
status_t conn_reader_create(int fd, conn_reader_t **out);

/* Free the reader. Safe to call with NULL. Does not close the fd. */
void conn_reader_destroy(conn_reader_t *reader);

/* Read the next line into buf, which must hold at least MAX_LINE_LEN bytes.
   A line may have at most MAX_LINE_LEN-1 characters, not counting the
   newline. The trailing '\n' is removed and buf always ends with '\0'. A '\r'
   is NOT removed: it stays in the line, and protocol_parse rejects it.
   After TOO_LONG or BAD, the rest of that line has been thrown away, so the
   next call starts at the following line. If the socket has a receive
   timeout (SO_RCVTIMEO), a timeout returns READ_LINE_ERROR. */
read_line_result_t conn_reader_read_line(conn_reader_t *reader, char *buf,
                                         size_t buflen);

/* ---- Parsing a line ---- */

/* Parse one line (without the newline) into *out. Returns PROTO_OK or the
   reason the line was rejected. Never modifies line and never reads past its
   '\0'. Rules, checked in this order:
     1. The command word must be USAGE, TOTAL or QUIT, else BAD_COMMAND.
     2. Words are separated by exactly one space, and there must be exactly
        the right number of them (USAGE 4, TOTAL 3, QUIT 1), else BAD_COMMAND.
        Any '\r' or other control character gives BAD_COMMAND.
     3. The account id must be digits only, from 1 to 4294967295,
        else BAD_COMMAND.
     4. The type must match one of the five names exactly, else BAD_TYPE.
     5. The quantity (USAGE only) must be digits only, from 1 to
        MAX_QUANTITY, else BAD_QUANTITY. Zero is not valid.
   So "USAGE 99 BOGUS -5" is rejected as BAD_TYPE. Whether the account
   exists is the server's job, after parsing. */
proto_error_t protocol_parse(const char *line, command_t *out);

/* ---- Building replies ---- */

/* Turn an internal result code into the reply the client sees:
     BILL_OK                 -> PROTO_OK
     BILL_ERR_NOT_FOUND      -> PROTO_ERR_UNKNOWN_ACCOUNT
     BILL_ERR_INVALID        -> PROTO_ERR_BAD_COMMAND
     BILL_ERR_OVERFLOW       -> PROTO_ERR_BAD_QUANTITY
     BILL_ERR_LOCK           -> PROTO_ERR_BUSY
     everything else         -> PROTO_ERR_INTERNAL */
proto_error_t protocol_error_from_status(status_t status);

/* Build a reply line like "OK 250\n" into buf. At most buflen bytes are
   written, always ending with '\0'. */
void protocol_reply_ok(char *buf, size_t buflen, int64_t value);

/* Build the reply to QUIT: "OK bye\n". */
void protocol_reply_bye(char *buf, size_t buflen);

/* Build an error reply like "ERR bad_type\n". */
void protocol_reply_err(char *buf, size_t buflen, proto_error_t err);

#endif /* PROTOCOL_H */
