#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include "meter.h"

/* The open meters file. The mutex keeps threads apart, and flock keeps
   other programs apart. */
struct meter_store {
    FILE           *fp;
    pthread_mutex_t mutex;
};

/* ---- Locking: always mutex first, then flock ---- */

/* mode is LOCK_SH (reading) or LOCK_EX (writing). */
static status_t lock_store(meter_store_t *store, int mode)
{
    pthread_mutex_lock(&store->mutex);
    if (flock(fileno(store->fp), mode) != 0) {
        pthread_mutex_unlock(&store->mutex);
        return BILL_ERR_LOCK;
    }
    return BILL_OK;
}

static void unlock_store(meter_store_t *store)
{
    flock(fileno(store->fp), LOCK_UN);
    pthread_mutex_unlock(&store->mutex);
}

/* ---- Reading and writing one record (locks must already be held) ---- */

/* The meter for (account id, type) lives in slot (id-1)*RES_COUNT + type. */
static off_t slot_offset(uint32_t account_id, resource_t type)
{
    off_t slot = (off_t)(account_id - 1) * RES_COUNT + (off_t)type;
    return slot * (off_t)sizeof(meter_t);
}

static int args_are_valid(uint32_t account_id, resource_t type)
{
    return account_id != 0 && (int)type >= 0 && (int)type < RES_COUNT;
}

/* Read a meter. An empty slot, a slot past the end of the file, and a
   deleted meter all count as "not found". */
static status_t read_slot(FILE *fp, uint32_t account_id, resource_t type,
                          meter_t *out)
{
    if (fseeko(fp, slot_offset(account_id, type), SEEK_SET) != 0)
        return BILL_ERR_IO;
    if (fread(out, sizeof *out, 1, fp) != 1)
        return BILL_ERR_NOT_FOUND;
    if (out->account_id == 0 || out->deleted)
        return BILL_ERR_NOT_FOUND;
    return BILL_OK;
}

static status_t write_slot(FILE *fp, const meter_t *rec)
{
    off_t offset = slot_offset(rec->account_id, (resource_t)rec->type);
    if (fseeko(fp, offset, SEEK_SET) != 0 ||
        fwrite(rec, sizeof *rec, 1, fp) != 1)
        return BILL_ERR_IO;
    return BILL_OK;
}

/* ---- Open and close ---- */

status_t meter_store_open(const char *path, meter_store_t **out)
{
    if (path == NULL || out == NULL)
        return BILL_ERR_INVALID;

    meter_store_t *store = calloc(1, sizeof *store);
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

void meter_store_close(meter_store_t *store)
{
    if (store == NULL)
        return;
    fclose(store->fp);
    pthread_mutex_destroy(&store->mutex);
    free(store);
}

/* ---- Create, Read, Update, Delete ---- */

status_t meter_create(meter_store_t *store, uint32_t account_id,
                      resource_t type)
{
    if (store == NULL || !args_are_valid(account_id, type))
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    meter_t rec;
    st = read_slot(store->fp, account_id, type, &rec);
    if (st == BILL_OK) {
        st = BILL_ERR_EXISTS;                    /* an active meter is already here */
    } else if (st == BILL_ERR_NOT_FOUND) {
        memset(&rec, 0, sizeof rec);             /* no garbage bytes on disk */
        rec.account_id = account_id;
        rec.type = (uint8_t)type;
        st = write_slot(store->fp, &rec);
    }

    unlock_store(store);
    return st;
}

status_t meter_read(meter_store_t *store, uint32_t account_id,
                    resource_t type, meter_t *out)
{
    if (store == NULL || out == NULL || !args_are_valid(account_id, type))
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_SH);
    if (st != BILL_OK)
        return st;

    meter_t rec;
    st = read_slot(store->fp, account_id, type, &rec);
    unlock_store(store);

    if (st == BILL_OK)
        *out = rec;
    return st;
}

status_t meter_apply_usage(meter_store_t *store, uint32_t account_id,
                           resource_t type, int64_t quantity)
{
    if (store == NULL || !args_are_valid(account_id, type) || quantity < 0)
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    meter_t rec;
    st = read_slot(store->fp, account_id, type, &rec);
    if (st == BILL_OK) {
        if (resource_is_gauge(type)) {
            if (quantity > rec.total_quantity)   /* a gauge keeps its peak */
                rec.total_quantity = quantity;
            st = write_slot(store->fp, &rec);
        } else if (quantity > INT64_MAX - rec.total_quantity) {
            st = BILL_ERR_OVERFLOW;              /* the sum would be too big */
        } else {
            rec.total_quantity += quantity;
            st = write_slot(store->fp, &rec);
        }
    }

    unlock_store(store);
    return st;
}

status_t meter_set_total(meter_store_t *store, uint32_t account_id,
                         resource_t type, int64_t total)
{
    if (store == NULL || !args_are_valid(account_id, type) || total < 0)
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    meter_t rec;
    st = read_slot(store->fp, account_id, type, &rec);
    if (st == BILL_OK) {
        rec.total_quantity = total;
        st = write_slot(store->fp, &rec);
    }

    unlock_store(store);
    return st;
}

status_t meter_delete(meter_store_t *store, uint32_t account_id,
                      resource_t type)
{
    if (store == NULL || !args_are_valid(account_id, type))
        return BILL_ERR_INVALID;

    status_t st = lock_store(store, LOCK_EX);
    if (st != BILL_OK)
        return st;

    meter_t rec;
    st = read_slot(store->fp, account_id, type, &rec);
    if (st == BILL_OK) {
        rec.deleted = 1;                         /* the record stays in the file */
        st = write_slot(store->fp, &rec);
    }

    unlock_store(store);
    return st;
}
