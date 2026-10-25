#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stdint.h>
#include "common.h"

/* One customer account. Stored as a fixed-size record in accounts.dat.
   Account number N lives in slot N-1 of the file, so ids start at 1 and
   records are never moved or compacted. */
typedef struct {
    int64_t  created_at;             /* when the account was made (seconds since 1970) */
    uint32_t id;                     /* account number, starts at 1 */
    uint8_t  deleted;                /* 0 = active, 1 = deleted */
    uint8_t  reserved[3];            /* unused, keeps the struct size free of hidden padding */
    char     name[MAX_NAME_LEN];     /* customer name, always ends with '\0' */
} account_t;

/* The open accounts file. Its contents are hidden (defined in account.c).
   The store owns its own mutex, so every function below is safe to call
   from several threads. It also takes flock on the file for other processes. */
typedef struct account_store account_store_t;

/* Open the accounts file at path, creating it if it doesn't exist.
   On success, *out points to the new store. */
status_t account_store_open(const char *path, account_store_t **out);

/* Close the file and free the store. Safe to call with NULL. */
void account_store_close(account_store_t *store);

/* Create: add a new account. A name that is empty or longer than
   MAX_NAME_LEN-1 characters is rejected with BILL_ERR_INVALID.
   On success, *new_id holds its number. */
status_t account_create(account_store_t *store, const char *name,
                        uint32_t *new_id);

/* Read: copy an active account into *out. A deleted account is reported
   as BILL_ERR_NOT_FOUND. Use this when validating new usage. */
status_t account_read(account_store_t *store, uint32_t id, account_t *out);

/* Read, including deleted accounts. Use this for invoicing, because a
   deleted account can still owe for usage it already reported. */
status_t account_read_any(account_store_t *store, uint32_t id,
                          account_t *out);

/* Update: change an active account's name (same length rule as create). */
status_t account_update(account_store_t *store, uint32_t id,
                        const char *new_name);

/* Delete: mark an account as deleted. The record stays in the file.
   Deleting an account that is already deleted returns BILL_ERR_NOT_FOUND. */
status_t account_delete(account_store_t *store, uint32_t id);

/* Called once per active account by account_for_each. */
typedef void (*account_visit_fn)(const account_t *account, void *user_data);

/* Call fn once for each active account, in id order. The callback runs
   while the store is locked, so it must be quick and must not call any
   account_* function. */
status_t account_for_each(account_store_t *store, account_visit_fn fn,
                          void *user_data);

#endif /* ACCOUNT_H */
