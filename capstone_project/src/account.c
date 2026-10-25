#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include "account.h"

/* The open accounts file. The mutex keeps threads apart, and flock keeps
   other programs (like the admin program) apart. */
struct account_store {
    FILE           *fp;
    pthread_mutex_t mutex;
};

/* ---- Locking: always mutex first, then flock ---- */

/* mode is LOCK_SH (reading) or LOCK_EX (writing). */
static status_t lock_store(account_store_t *store, int mode)
{
    pthread_mutex_lock(&store->mutex);
    if (flock(fileno(store->fp), mode) != 0) {
        pthread_mutex_unlock(&store->mutex);
        return BILL_ERR_LOCK;
    }
    return BILL_OK;
}

static void unlock_store(account_store_t *store)
{
    flock(fileno(store->fp), LOCK_UN);
    pthread_mutex_unlock(&store->mutex);
}

/* ---- Reading and writing one record (locks must already be held) ---- */

/* Account number id lives in slot id-1, so jump straight to it. */
static status_t read_slot(FILE *fp, uint32_t id, account_t *out)
{
    if (id == 0)
        return BILL_ERR_NOT_FOUND;
    if (fseeko(fp, (off_t)(id - 1) * (off_t)sizeof(account_t), SEEK_SET) != 0)
        return BILL_ERR_IO;
    if (fread(out, sizeof *out, 1, fp) != 1)
        return BILL_ERR_NOT_FOUND;       /* past the end of the file */
    return BILL_OK;
}

static status_t write_slot(FILE *fp, const account_t *rec)
{
    off_t offset = (off_t)(rec->id - 1) * (off_t)sizeof(account_t);
    if (fseeko(fp, offset, SEEK_SET) != 0 ||
        fwrite(rec, sizeof *rec, 1, fp) != 1)
        return BILL_ERR_IO;
    return BILL_OK;
}

/* A valid name is 1 to MAX_NAME_LEN-1 characters. */
static int name_is_valid(const char *name)
{
    if (name == NULL)
        return 0;
    size_t len = strnlen(name, MAX_NAME_LEN);
    return len > 0 && len < MAX_NAME_LEN;
}

/* Shared by account_read and account_read_any. */
static status_t read_locked(account_store_t *store, uint32_t id,
                            account_t *out, int include_deleted)
{
    if (store == NULL || out == NULL)
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_SH);
    if (st != BILL_OK)
        return st;

    account_t rec;
    st = read_slot(store->fp, id, &rec);
    unlock_store(store);
    if (st != BILL_OK)
        return st;

    if (rec.deleted && !include_deleted)
        return BILL_ERR_NOT_FOUND;
    *out = rec;
    return BILL_OK;
}

/* ---- Open and close ---- */

status_t account_store_open(const char *path, account_store_t **out)
{
    if (path == NULL || out == NULL)
        return BILL_ERR_INVALID;

    account_store_t *store = calloc(1, sizeof *store);
    if (store == NULL)
        return BILL_ERR_NOMEM;

    int fd = open(path, O_RDWR | O_CREAT, 0644);   /* create the file if missing */
    if (fd < 0) {
        free(store);
        return BILL_ERR_IO;
    }

    store->fp = fdopen(fd, "r+b");
    if (store->fp == NULL) {
        close(fd);
        free(store);
        return BILL_ERR_IO;
    }

    /* No stdio buffering, so reads always see what is really in the file. */
    setvbuf(store->fp, NULL, _IONBF, 0);
    pthread_mutex_init(&store->mutex, NULL);

    *out = store;
    return BILL_OK;
}

void account_store_close(account_store_t *store)
{
    if (store == NULL)
        return;
    fclose(store->fp);
    pthread_mutex_destroy(&store->mutex);
    free(store);
}

/* ---- Create, Read, Update, Delete ---- */

status_t account_create(account_store_t *store, const char *name,
                        uint32_t *new_id)
{
    if (store == NULL || new_id == NULL || !name_is_valid(name))
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    /* The file size tells us how many accounts exist. */
    off_t size = -1;
    if (fseeko(store->fp, 0, SEEK_END) == 0)
        size = ftello(store->fp);

    if (size < 0) {
        st = BILL_ERR_IO;
    } else {
        account_t rec;
        memset(&rec, 0, sizeof rec);             /* no garbage bytes on disk */
        rec.id = (uint32_t)(size / (off_t)sizeof(account_t)) + 1;
        rec.created_at = (int64_t)time(NULL);
        memcpy(rec.name, name, strlen(name));    /* length already checked */

        st = write_slot(store->fp, &rec);
        if (st == BILL_OK)
            *new_id = rec.id;
    }

    unlock_store(store);
    return st;
}

status_t account_read(account_store_t *store, uint32_t id, account_t *out)
{
    return read_locked(store, id, out, 0);
}

/* Same as account_read, but deleted accounts are allowed (for invoicing). */
status_t account_read_any(account_store_t *store, uint32_t id,
                          account_t *out)
{
    return read_locked(store, id, out, 1);
}

status_t account_update(account_store_t *store, uint32_t id,
                        const char *new_name)
{
    if (store == NULL || !name_is_valid(new_name))
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    account_t rec;
    st = read_slot(store->fp, id, &rec);
    if (st == BILL_OK && rec.deleted)
        st = BILL_ERR_NOT_FOUND;
    if (st == BILL_OK) {
        memset(rec.name, 0, sizeof rec.name);
        memcpy(rec.name, new_name, strlen(new_name));
        st = write_slot(store->fp, &rec);
    }

    unlock_store(store);
    return st;
}

status_t account_delete(account_store_t *store, uint32_t id)
{
    if (store == NULL)
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    account_t rec;
    st = read_slot(store->fp, id, &rec);
    if (st == BILL_OK && rec.deleted)
        st = BILL_ERR_NOT_FOUND;
    if (st == BILL_OK) {
        rec.deleted = 1;                         /* the record stays in the file */
        st = write_slot(store->fp, &rec);
    }

    unlock_store(store);
    return st;
}

/* Call fn for every active account, in id order. */
status_t account_for_each(account_store_t *store, account_visit_fn fn,
                          void *user_data)
{
    if (store == NULL || fn == NULL)
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_SH);
    if (st != BILL_OK)
        return st;

    account_t rec;
    for (uint32_t id = 1; read_slot(store->fp, id, &rec) == BILL_OK; id++) {
        if (!rec.deleted)
            fn(&rec, user_data);
    }

    unlock_store(store);
    return BILL_OK;
}
