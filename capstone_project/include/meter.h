#ifndef METER_H
#define METER_H

#include <stdint.h>
#include "common.h"

/* A cached total of one resource for one account. Stored as a fixed-size
   record in meters.dat. The LEDGER is the source of truth: a meter is only
   a shortcut so TOTAL can answer quickly, and it is rebuilt from the ledger
   at startup.
   Meter for (account id, type) lives in slot (id-1)*RES_COUNT + type. */
typedef struct {
    int64_t  total_quantity;         /* sum so far (or the peak, for a gauge resource) */
    uint32_t account_id;             /* which account this meter belongs to */
    uint8_t  type;                   /* holds a resource_t value (RES_NET_OUT, etc.) */
    uint8_t  deleted;                /* 0 = active, 1 = deleted */
    uint8_t  reserved[2];            /* unused, keeps the struct free of hidden padding */
} meter_t;

/* The open meters file. Its contents are hidden (defined in meter.c).
   The store owns its own mutex, so every function below is safe to call
   from several threads. It also takes flock on the file for other processes. */
typedef struct meter_store meter_store_t;

/* Open the meters file at path, creating it if it doesn't exist.
   On success, *out points to the new store. */
status_t meter_store_open(const char *path, meter_store_t **out);

/* Close the file and free the store. Safe to call with NULL. */
void meter_store_close(meter_store_t *store);

/* Create: add a meter starting at zero. Returns BILL_ERR_EXISTS if an
   active one already exists. */
status_t meter_create(meter_store_t *store, uint32_t account_id,
                      resource_t type);

/* Read: copy the meter into *out. Deleted meters count as not found. */
status_t meter_read(meter_store_t *store, uint32_t account_id,
                    resource_t type, meter_t *out);

/* Update: apply one usage event. For a gauge resource (see
   resource_is_gauge) the meter keeps the highest value seen; for every
   other resource the quantity is added to the total. Returns
   BILL_ERR_OVERFLOW if the total would overflow. */
status_t meter_apply_usage(meter_store_t *store, uint32_t account_id,
                           resource_t type, int64_t quantity);

/* Overwrite a meter's total. Used at startup to rebuild meters from the
   ledger, and nowhere else. */
status_t meter_set_total(meter_store_t *store, uint32_t account_id,
                         resource_t type, int64_t total);

/* Delete: mark the meter as deleted. The record stays in the file. */
status_t meter_delete(meter_store_t *store, uint32_t account_id,
                      resource_t type);

#endif /* METER_H */
