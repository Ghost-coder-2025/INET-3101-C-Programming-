#include <stdio.h>
#include "pricing.h"

/* ---- The rates, in micro-dollars (1,000,000 = $1.00) ---- */

#define CPU_RATE_PER_HOUR     40000   /* per vCPU-hour */
#define RAM_RATE_PER_HOUR      5000   /* per GB-hour */
#define SECONDS_PER_HOUR       3600

#define STORAGE_TIER_GB         100   /* the first 100 GB cost more per GB */
#define STORAGE_RATE_FIRST    23000   /* per GB, for the first 100 GB */
#define STORAGE_RATE_AFTER    20000   /* per GB, above 100 GB */

#define NET_OUT_FREE_MB        1000   /* the first 1,000 MB are free */
#define NET_OUT_TIER_MB       10000   /* the next 10,000 MB cost more per MB */
#define NET_OUT_RATE_FIRST       90   /* per MB, for that next 10,000 MB */
#define NET_OUT_RATE_AFTER       85   /* per MB, above that */

#define MICRO_PER_CENT        10000LL

/* ---- Helpers, one per kind of resource ---- */

/* CPU and RAM: cost = seconds * rate / 3600, rounded half-up. */
static status_t hourly_cost(int64_t seconds, int64_t rate_per_hour,
                            money_t *out)
{
    if (seconds > (INT64_MAX - SECONDS_PER_HOUR / 2) / rate_per_hour)
        return BILL_ERR_OVERFLOW;
    *out = (seconds * rate_per_hour + SECONDS_PER_HOUR / 2) / SECONDS_PER_HOUR;
    return BILL_OK;
}

/* Storage: round down to whole GB, then two price tiers. */
static status_t storage_cost(int64_t mb, money_t *out)
{
    int64_t gb = pricing_storage_gb(mb);
    int64_t first = gb < STORAGE_TIER_GB ? gb : STORAGE_TIER_GB;
    int64_t after = gb - first;

    if (after > (INT64_MAX - first * STORAGE_RATE_FIRST) / STORAGE_RATE_AFTER)
        return BILL_ERR_OVERFLOW;
    *out = first * STORAGE_RATE_FIRST + after * STORAGE_RATE_AFTER;
    return BILL_OK;
}

/* Outbound network: a free allowance, then two price tiers. */
static status_t net_out_cost(int64_t mb, money_t *out)
{
    int64_t billable = mb > NET_OUT_FREE_MB ? mb - NET_OUT_FREE_MB : 0;
    int64_t first = billable < NET_OUT_TIER_MB ? billable : NET_OUT_TIER_MB;
    int64_t after = billable - first;

    if (after > (INT64_MAX - first * NET_OUT_RATE_FIRST) / NET_OUT_RATE_AFTER)
        return BILL_ERR_OVERFLOW;
    *out = first * NET_OUT_RATE_FIRST + after * NET_OUT_RATE_AFTER;
    return BILL_OK;
}

/* ---- Public functions ---- */

int64_t pricing_storage_gb(int64_t mb)
{
    if (mb < 0)
        return 0;
    return mb / 1024;                  /* integer division rounds down */
}

status_t pricing_cost(resource_t type, int64_t quantity, money_t *out_cost)
{
    if (out_cost == NULL || quantity < 0 ||
        (int)type < 0 || (int)type >= RES_COUNT)
        return BILL_ERR_INVALID;

    switch (type) {
    case RES_COMPUTE_CPU:
        return hourly_cost(quantity, CPU_RATE_PER_HOUR, out_cost);
    case RES_COMPUTE_RAM:
        return hourly_cost(quantity, RAM_RATE_PER_HOUR, out_cost);
    case RES_STORAGE:
        return storage_cost(quantity, out_cost);
    case RES_NET_IN:
        *out_cost = 0;                 /* inbound network is free */
        return BILL_OK;
    case RES_NET_OUT:
        return net_out_cost(quantity, out_cost);
    case RES_COUNT:
        break;
    }
    return BILL_ERR_INVALID;
}

status_t pricing_round_to_cents(money_t amount, money_t *out_rounded)
{
    if (out_rounded == NULL || amount < 0)
        return BILL_ERR_INVALID;
    if (amount > INT64_MAX - MICRO_PER_CENT / 2)
        return BILL_ERR_OVERFLOW;

    /* Add half a cent, cut off the rest: half-up rounding. */
    *out_rounded = (amount + MICRO_PER_CENT / 2) / MICRO_PER_CENT
                   * MICRO_PER_CENT;
    return BILL_OK;
}

void pricing_format_usd(money_t amount, char *buf, size_t buflen)
{
    if (buf == NULL || buflen == 0)
        return;

    const uint64_t cent = (uint64_t)MICRO_PER_CENT;
    const char *sign = "";
    uint64_t magnitude;

    if (amount < 0) {
        sign = "-";
        magnitude = (uint64_t)(-(amount + 1)) + 1;   /* safe for any negative */
    } else {
        magnitude = (uint64_t)amount;
    }

    /* Round to the nearest cent, then split into dollars and cents. */
    uint64_t cents = magnitude / cent + (magnitude % cent >= cent / 2 ? 1 : 0);

    snprintf(buf, buflen, "%s$%llu.%02llu", sign,
             (unsigned long long)(cents / 100),
             (unsigned long long)(cents % 100));
}
