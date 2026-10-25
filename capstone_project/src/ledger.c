#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ledger.h"

/* The ledger: every usage event, kept in memory and in the log file. */
struct ledger {
    FILE           *fp;         /* the log file */
    ledger_entry_t *entries;    /* every entry, oldest first */
    size_t          count;      /* how many entries are in the list */
    size_t          capacity;   /* how many fit before the list must grow */
    uint64_t        next_seq;   /* the number the next entry will get */
    off_t           log_size;   /* size of the log file, in bytes */
    pthread_mutex_t mutex;      /* makes threads take turns */
};

/* ---- The in-memory list ---- */

/* Make sure there is room for one more entry. */
static status_t list_reserve(ledger_t *ledger)
{
    if (ledger->count < ledger->capacity)
        return BILL_OK;

    size_t new_capacity = ledger->capacity == 0 ? 64 : ledger->capacity * 2;
    ledger_entry_t *bigger = realloc(ledger->entries,
                                     new_capacity * sizeof *bigger);
    if (bigger == NULL)
        return BILL_ERR_NOMEM;

    ledger->entries = bigger;
    ledger->capacity = new_capacity;
    return BILL_OK;
}

/* ---- Loading the log file ---- */

/* Turn one log line into an entry. Returns 1 if the line is valid.
   A line looks like: 1,7,NET_OUT,250,1761350412 */
static int parse_line(const char *line, ledger_entry_t *out)
{
    unsigned long long seq;
    unsigned int account;
    char type_name[16];
    long long quantity, timestamp;
    int used = 0;
    resource_t type;

    if (sscanf(line, "%llu,%u,%15[A-Z_],%lld,%lld%n",
               &seq, &account, type_name, &quantity, &timestamp, &used) != 5)
        return 0;

    if (line[used] != '\n' || account == 0 || quantity <= 0 ||
        quantity > MAX_QUANTITY ||
        resource_from_string(type_name, &type) != BILL_OK)
        return 0;

    out->seq = seq;
    out->timestamp = timestamp;
    out->quantity = quantity;
    out->account_id = account;
    out->type = (uint8_t)type;
    return 1;
}

/* Read the whole log into the list. */
static status_t load_log(ledger_t *ledger)
{
    FILE *fp = ledger->fp;
    char line[256];
    off_t good_size = 0;

    /* A file opened for appending may start at the end, so go to the start. */
    if (fseeko(fp, 0, SEEK_SET) != 0)
        return BILL_ERR_IO;

    while (fgets(line, sizeof line, fp) != NULL) {
        size_t len = strlen(line);
        if (len == 0)
            return BILL_ERR_CORRUPT;

        if (line[len - 1] != '\n') {
            if (feof(fp))
                break;                      /* half-written last line: ignore it */
            return BILL_ERR_CORRUPT;        /* a line too long to be real */
        }

        ledger_entry_t entry;
        if (!parse_line(line, &entry) || entry.seq != ledger->next_seq)
            return BILL_ERR_CORRUPT;

        status_t st = list_reserve(ledger);
        if (st != BILL_OK)
            return st;

        ledger->entries[ledger->count++] = entry;
        ledger->next_seq++;
        good_size += (off_t)len;
    }

    if (ferror(fp))
        return BILL_ERR_IO;

    /* Cut off any half-written last line, so new entries start on a fresh line. */
    if (ftruncate(fileno(fp), good_size) != 0)
        return BILL_ERR_IO;
    ledger->log_size = good_size;
    return BILL_OK;
}

/* ---- Open, close, and append ---- */

status_t ledger_create(const char *log_path, ledger_t **out)
{
    if (log_path == NULL || out == NULL)
        return BILL_ERR_INVALID;

    ledger_t *ledger = calloc(1, sizeof *ledger);
    if (ledger == NULL)
        return BILL_ERR_NOMEM;
    ledger->next_seq = 1;

    ledger->fp = fopen(log_path, "a+");       /* creates the file if missing */
    if (ledger->fp == NULL) {
        free(ledger);
        return BILL_ERR_IO;
    }

    status_t st = load_log(ledger);
    if (st != BILL_OK) {
        fclose(ledger->fp);
        free(ledger->entries);
        free(ledger);
        return st;
    }

    pthread_mutex_init(&ledger->mutex, NULL);
    *out = ledger;
    return BILL_OK;
}

void ledger_destroy(ledger_t *ledger)
{
    if (ledger == NULL)
        return;
    fclose(ledger->fp);
    pthread_mutex_destroy(&ledger->mutex);
    free(ledger->entries);
    free(ledger);
}

status_t ledger_append(ledger_t *ledger, uint32_t account_id,
                       resource_t type, int64_t quantity, uint64_t *out_seq)
{
    if (ledger == NULL || out_seq == NULL || account_id == 0 ||
        (int)type < 0 || (int)type >= RES_COUNT ||
        quantity <= 0 || quantity > MAX_QUANTITY)
        return BILL_ERR_INVALID;

    pthread_mutex_lock(&ledger->mutex);

    /* Make room in memory first, so the last step below cannot fail. */
    status_t st = list_reserve(ledger);
    if (st == BILL_OK) {
        ledger_entry_t entry;
        memset(&entry, 0, sizeof entry);
        entry.seq = ledger->next_seq;
        entry.timestamp = (int64_t)time(NULL);
        entry.quantity = quantity;
        entry.account_id = account_id;
        entry.type = (uint8_t)type;

        /* Write the line to the file, and force it onto the disk. */
        int written = fprintf(ledger->fp, "%llu,%u,%s,%lld,%lld\n",
                              (unsigned long long)entry.seq,
                              (unsigned int)entry.account_id,
                              resource_to_string(type),
                              (long long)entry.quantity,
                              (long long)entry.timestamp);
        if (written < 0 || fflush(ledger->fp) != 0 ||
            fsync(fileno(ledger->fp)) != 0) {
            /* Undo any half-written line. Nothing changes in memory. */
            (void)ftruncate(fileno(ledger->fp), ledger->log_size);
            st = BILL_ERR_IO;
        } else {
            ledger->log_size += written;
            ledger->entries[ledger->count++] = entry;
            ledger->next_seq++;
            *out_seq = entry.seq;
        }
    }

    pthread_mutex_unlock(&ledger->mutex);
    return st;
}

/* ---- Questions about the ledger ---- */

/* Does this entry belong to the account and fall inside [from, to)? */
static int entry_matches(const ledger_entry_t *entry, uint32_t account_id,
                         int64_t from, int64_t to)
{
    return entry->account_id == account_id &&
           entry->timestamp >= from && entry->timestamp < to;
}

status_t ledger_totals(ledger_t *ledger, uint32_t account_id,
                       int64_t from, int64_t to,
                       int64_t out_totals[RES_COUNT], uint64_t *as_seq)
{
    if (ledger == NULL || out_totals == NULL || as_seq == NULL ||
        account_id == 0 || from >= to)
        return BILL_ERR_INVALID;

    memset(out_totals, 0, RES_COUNT * sizeof(int64_t));
    status_t st = BILL_OK;

    pthread_mutex_lock(&ledger->mutex);

    for (size_t i = 0; i < ledger->count && st == BILL_OK; i++) {
        const ledger_entry_t *entry = &ledger->entries[i];
        if (!entry_matches(entry, account_id, from, to))
            continue;

        if (resource_is_gauge((resource_t)entry->type)) {
            if (entry->quantity > out_totals[entry->type])
                out_totals[entry->type] = entry->quantity;   /* keep the peak */
        } else if (entry->quantity > INT64_MAX - out_totals[entry->type]) {
            st = BILL_ERR_OVERFLOW;                          /* the sum would be too big */
        } else {
            out_totals[entry->type] += entry->quantity;
        }
    }
    *as_seq = ledger->next_seq - 1;      /* the last entry the ledger had */

    pthread_mutex_unlock(&ledger->mutex);
    return st;
}

status_t ledger_current_totals(ledger_t *ledger, uint32_t account_id,
                               int64_t out_totals[RES_COUNT])
{
    uint64_t as_seq;
    return ledger_totals(ledger, account_id, 0, INT64_MAX, out_totals, &as_seq);
}

status_t ledger_for_each(ledger_t *ledger, uint32_t account_id,
                         int64_t from, int64_t to,
                         ledger_visit_fn fn, void *user_data)
{
    if (ledger == NULL || fn == NULL || account_id == 0 || from >= to)
        return BILL_ERR_INVALID;

    pthread_mutex_lock(&ledger->mutex);

    /* Count the matching entries, then copy them while the lock is held. */
    size_t matches = 0;
    for (size_t i = 0; i < ledger->count; i++) {
        if (entry_matches(&ledger->entries[i], account_id, from, to))
            matches++;
    }

    ledger_entry_t *copy = NULL;
    if (matches > 0) {
        copy = malloc(matches * sizeof *copy);
        if (copy == NULL) {
            pthread_mutex_unlock(&ledger->mutex);
            return BILL_ERR_NOMEM;
        }
        size_t n = 0;
        for (size_t i = 0; i < ledger->count; i++) {
            if (entry_matches(&ledger->entries[i], account_id, from, to))
                copy[n++] = ledger->entries[i];
        }
    }

    pthread_mutex_unlock(&ledger->mutex);

    /* The lock is released, so the callback can be slow or call the ledger. */
    for (size_t i = 0; i < matches; i++)
        fn(&copy[i], user_data);

    free(copy);
    return BILL_OK;
}
