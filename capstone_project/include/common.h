#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <time.h>

/* Limits from the spec */
#define MAX_NAME_LEN   64            /* 63 characters + the terminating '\0' */
#define MAX_LINE_LEN   256           /* longest TCP line the server accepts */
#define SERVER_PORT    9090
#define MAX_QUANTITY   1000000000LL  /* largest usage amount one event may report */

/* Money is always whole micro-dollars (1 USD = 1,000,000) */
typedef int64_t money_t;

/* The five resource types a meter can track */
typedef enum {
    RES_COMPUTE_CPU = 0,
    RES_COMPUTE_RAM,
    RES_STORAGE,
    RES_NET_IN,
    RES_NET_OUT,
    RES_COUNT                        /* not a real type: the number of types */
} resource_t;

/* One set of return codes used by every module */
typedef enum {
    BILL_OK = 0,
    BILL_ERR_INVALID,                /* bad input */
    BILL_ERR_NOT_FOUND,              /* account or meter doesn't exist */
    BILL_ERR_EXISTS,                 /* it already exists */
    BILL_ERR_IO,                     /* file read or write failed */
    BILL_ERR_NOMEM,                  /* malloc failed */
    BILL_ERR_LOCK,                   /* flock failed or timed out */
    BILL_ERR_OVERFLOW,               /* arithmetic would overflow */
    BILL_ERR_CORRUPT                 /* a saved file failed validation */
} status_t;

/* Convert between enum values and protocol names like "NET_OUT" */
const char *resource_to_string(resource_t type);
status_t    resource_from_string(const char *name, resource_t *out);

/* 1 if the resource is a gauge (STORAGE: billed at its peak in the period),
   0 if it is a plain running amount (everything else). */
int resource_is_gauge(resource_t type);

/* Readable text for a result code, for server logs. */
const char *status_to_string(status_t status);

#endif /* COMMON_H */
