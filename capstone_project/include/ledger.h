#ifndef LEDGER_H
#define LEDGER_H

#include <stdint.h>
#include "common.h"

/* The ledger is the SOURCE OF TRUTH for usage: every event ever reported,
   append-only. Meters are only a cache rebuilt from it.

   The struct's contents are hidden: they are defined only in ledger.c, so
   other files can hold a pointer but can't reach inside. The ledger owns its
   own mutex, so every function below is safe to call from several threads. */
typedef struct ledger ledger_t;

/* One usage event. Callers receive these read-only. */
typedef struct {
    uint64_t seq;                    /* sequence number, starts at 1 */
    int64_t  timestamp;              /* set by the ledger when the event is recorded */
    int64_t  quantity;               /* amount used, in the resource's unit */
    uint32_t account_id;
    uint8_t  type;                   /* holds a resource_t value */
} ledger_entry_t;

/* Called once per entry by ledger_for_each. user_data is passed through untouched. */
typedef void (*ledger_visit_fn)(const ledger_entry_t *entry, void *user_data);

/* Create the ledger and load any existing entries from the log file at
   log_path (e.g. "data/ledger.log"). If the file doesn't exist yet, the
   ledger starts empty. A half-written LAST line (from a crash) is ignored.
   A bad line anywhere else, or sequence numbers that aren't contiguous,
   returns BILL_ERR_CORRUPT. On success, *out points to the new ledger. */
status_t ledger_create(const char *log_path, ledger_t **out);

/* Free all memory and close the log file. Safe to call with NULL. */
void ledger_destroy(ledger_t *ledger);

/* Append one event. The ledger assigns the sequence number and timestamp.
   Success means the line is written to disk (flushed) AND recorded in memory;
   on failure neither changes and no sequence number is used. On success,
   *out_seq holds the new sequence number. There is deliberately no edit
   or remove. */
status_t ledger_append(ledger_t *ledger, uint32_t account_id,
                       resource_t type, int64_t quantity, uint64_t *out_seq);

/* Totals for one account over the period [from, to): from is included, to is
   not. Fills out_totals[RES_COUNT] under ONE lock, so all five numbers come
   from the same moment. A gauge resource (see resource_is_gauge) reports its
   peak value in the period; every other resource is summed. *as_seq holds the
   sequence number of the last entry in the ledger when the totals were taken.
   Returns BILL_ERR_OVERFLOW if a sum would overflow. */
status_t ledger_totals(ledger_t *ledger, uint32_t account_id,
                       int64_t from, int64_t to,
                       int64_t out_totals[RES_COUNT], uint64_t *as_seq);

/* Same as ledger_totals but over all time, for the TOTAL command. */
status_t ledger_current_totals(ledger_t *ledger, uint32_t account_id,
                               int64_t out_totals[RES_COUNT]);

/* Call fn once for each of one account's entries in [from, to), oldest
   first. The ledger copies the entries first and releases its lock BEFORE
   calling fn, so the callback may be slow and may call ledger functions. */
status_t ledger_for_each(ledger_t *ledger, uint32_t account_id,
                         int64_t from, int64_t to,
                         ledger_visit_fn fn, void *user_data);

#endif /* LEDGER_H */
