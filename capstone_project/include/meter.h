#ifndef METER_H
#define METER_H

#include <stdint.h>
#include "common.h"

/* A running total of one resource for one account.
   Stored as a fixed-size record in meters.dat. */
typedef struct {
    int64_t  total_quantity;         /* sum of all usage so far, in the resource's unit */
    uint32_t account_id;             /* which account this meter belongs to */
    uint8_t  type;                   /* holds a resource_t value (RES_NET_OUT, etc.) */
    uint8_t  deleted;                /* 0 = active, 1 = deleted */
    uint8_t  reserved[2];            /* unused, keeps the struct free of hidden padding */
} meter_t;

/* CRUD operations. "path" is the meters file, e.g. "data/meters.dat".
   A meter is identified by its account id plus its resource type. */

/* Create: add a meter starting at zero. Fails if one already exists. */
status_t meter_create(const char *path, uint32_t account_id, resource_t type);

/* Read: copy the meter into *out. Deleted meters count as not found. */
status_t meter_read(const char *path, uint32_t account_id, resource_t type,
                    meter_t *out);

/* Update: add "quantity" to the meter's running total. */
status_t meter_add_usage(const char *path, uint32_t account_id,
                         resource_t type, int64_t quantity);

/* Delete: mark the meter as deleted. The record stays in the file. */
status_t meter_delete(const char *path, uint32_t account_id, resource_t type);

#endif /* METER_H */
