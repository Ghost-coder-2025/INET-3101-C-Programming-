#ifndef LEDGER_H
#define LEDGER_H

#include <stdint.h>
#include "common.h"

/* The ledger itself. The struct's contents are hidden: they are defined
   only in ledger.c, so other files can hold a pointer but can't reach inside. */
typedef struct ledger ledger_t;

/* One usage event. Callers receive these read-only. */
typedef struct {
    uint64_t seq;                    /* sequence number, starts at 1 */
    int64_t  timestamp;              /* set by the server when the event is recorded */
    int64_t  quantity;               /* amount used, in the resource's unit */
    uint32_t account_id;
    uint8_t  type;                   /* holds a resource_t value */
} ledger_entry_t;

/* Called once per entry by ledger_for_each. user_data is passed through untouched. */
typedef void (*ledger_visit_fn)(const ledger_entry_t *entry, void *user_data);

/* Create the ledger and load any existing entries from the log file at
   log_path (e.g. "data/ledger.log"). If the file doesn't exist yet, the
   ledger starts empty. On success, *out points to the new ledger. */
status_t ledger_create(const char *log_path, ledger_t **out);

/* Free all memory and close the log file. Safe to call with NULL. */
void ledger_destroy(ledger_t *ledger);

/* Append one event. Writes it to the log file and to memory. The ledger
   assigns the sequence number and timestamp. On success, *out_seq holds
   the new sequence number. There is deliberately no edit or remove. */
status_t ledger_append(ledger_t *ledger, uint32_t account_id,
                       resource_t type, int64_t quantity, uint64_t *out_seq);

/* Add up all of one account's usage of one resource type into *out_total. */
status_t ledger_total(ledger_t *ledger, uint32_t account_id,
                      resource_t type, int64_t *out_total);

/* Call fn once for each of one account's entries, oldest first. */
status_t ledger_for_each(ledger_t *ledger, uint32_t account_id,
                         ledger_visit_fn fn, void *user_data);

#endif /* LEDGER_H */
