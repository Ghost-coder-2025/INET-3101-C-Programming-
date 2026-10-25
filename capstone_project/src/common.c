#include <string.h>
#include "common.h"

/* The protocol names, in the same order as the resource_t enum. */
static const char *const resource_names[RES_COUNT] = {
    "COMPUTE_CPU",
    "COMPUTE_RAM",
    "STORAGE",
    "NET_IN",
    "NET_OUT"
};

const char *resource_to_string(resource_t type)
{
    if ((int)type < 0 || (int)type >= RES_COUNT)
        return "UNKNOWN";
    return resource_names[type];
}

status_t resource_from_string(const char *name, resource_t *out)
{
    if (name == NULL || out == NULL)
        return BILL_ERR_INVALID;

    for (int i = 0; i < RES_COUNT; i++) {
        if (strcmp(name, resource_names[i]) == 0) {
            *out = (resource_t)i;
            return BILL_OK;
        }
    }
    return BILL_ERR_INVALID;
}

int resource_is_gauge(resource_t type)
{
    return type == RES_STORAGE;
}

const char *status_to_string(status_t status)
{
    switch (status) {
    case BILL_OK:           return "ok";
    case BILL_ERR_INVALID:  return "invalid input";
    case BILL_ERR_NOT_FOUND: return "not found";
    case BILL_ERR_EXISTS:   return "already exists";
    case BILL_ERR_IO:       return "file read or write failed";
    case BILL_ERR_NOMEM:    return "out of memory";
    case BILL_ERR_LOCK:     return "lock failed or timed out";
    case BILL_ERR_OVERFLOW: return "arithmetic overflow";
    case BILL_ERR_CORRUPT:  return "saved file failed validation";
    }
    return "unknown status";
}
