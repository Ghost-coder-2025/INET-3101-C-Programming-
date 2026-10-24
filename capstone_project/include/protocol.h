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

/* The error reasons from the spec. These match the text after "ERR ". */
typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_UNKNOWN_ACCOUNT,       /* "unknown_account" */
    PROTO_ERR_BAD_TYPE,              /* "bad_type" */
    PROTO_ERR_BAD_QUANTITY,          /* "bad_quantity" */
    PROTO_ERR_BAD_COMMAND,           /* "bad_command" */
    PROTO_ERR_LINE_TOO_LONG          /* "line_too_long" */
} proto_error_t;

/* Parse one line from a client (without the trailing newline) into *out.
   Returns PROTO_OK, or the reason the line was rejected. Never modifies
   line, and never reads past its '\0'. A line of MAX_LINE_LEN or more
   characters is rejected as too long. */
proto_error_t protocol_parse(const char *line, command_t *out);

/* Build a reply line like "OK 250\n" into buf. At most buflen bytes are
   written, always ending with '\0'. */
void protocol_reply_ok(char *buf, size_t buflen, int64_t value);

/* Build the reply to QUIT: "OK bye\n". */
void protocol_reply_bye(char *buf, size_t buflen);

/* Build an error reply like "ERR bad_type\n". */
void protocol_reply_err(char *buf, size_t buflen, proto_error_t err);

#endif /* PROTOCOL_H */
