#ifndef PRICING_H
#define PRICING_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"

/* Round a storage amount in MB down to whole GB (MB / 1024). */
int64_t pricing_storage_gb(int64_t mb);

/* Work out the cost of one resource type for one invoice period.
   quantity is the usage for that period, in the resource's unit (seconds,
   MB, ...); for a gauge resource (storage) it is the peak value. Free
   allowances and tier thresholds apply once to this quantity.
   The result is in micro-dollars and goes into *out_cost. Each cost rounds
   half-up to the nearest micro-dollar.
   Returns BILL_ERR_INVALID for a negative quantity or unknown type, and
   BILL_ERR_OVERFLOW if the math would overflow. */
status_t pricing_cost(resource_t type, int64_t quantity, money_t *out_cost);

/* Round an amount to a whole cent (10,000 micro-dollars), half-up. This is
   the amount a customer is actually charged. Returns BILL_ERR_OVERFLOW if
   rounding would overflow, and BILL_ERR_INVALID for a negative amount. */
status_t pricing_round_to_cents(money_t amount, money_t *out_rounded);

/* Turn micro-dollars into text like "$12.34", rounded to the nearest cent.
   Writes at most buflen bytes into buf, always ending with '\0'. */
void pricing_format_usd(money_t amount, char *buf, size_t buflen);

#endif /* PRICING_H */
