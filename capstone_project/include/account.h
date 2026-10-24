#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stdint.h>
#include "common.h"

/* One customer account. Stored as a fixed-size record in accounts.dat. */
typedef struct {
    int64_t  created_at;             /* when the account was made (seconds since 1970) */
    uint32_t id;                     /* account number, starts at 1 */
    uint8_t  deleted;                /* 0 = active, 1 = deleted */
    uint8_t  reserved[3];            /* unused, keeps the struct size free of hidden padding */
    char     name[MAX_NAME_LEN];     /* customer name, always ends with '\0' */
} account_t;

/* CRUD operations. "path" is the accounts file, e.g. "data/accounts.dat". */

/* Create: add a new account. On success, *new_id holds its number. */
status_t account_create(const char *path, const char *name, uint32_t *new_id);

/* Read: copy account number id into *out. Deleted accounts count as not found. */
status_t account_read(const char *path, uint32_t id, account_t *out);

/* Update: change an account's name. */
status_t account_update(const char *path, uint32_t id, const char *new_name);

/* Delete: mark an account as deleted. The record stays in the file. */
status_t account_delete(const char *path, uint32_t id);

#endif /* ACCOUNT_H */
