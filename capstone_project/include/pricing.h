#ifndef PRICING_H
#define PRICING_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"

/* Round a storage amount in MB down to whole GB (MB / 1024). */
int64_t pricing_storage_gb(int64_t mb);

/* Work out the cost of one resource type, given the TOTAL quantity used.
   quantity is in the resource's unit (seconds, MB, ...), as reported by
   clients. The result is in micro-dollars and goes into *out_cost.
   Returns BILL_ERR_INVALID for a negative quantity or unknown type,
   and also if the math would overflow. */
status_t pricing_cost(resource_t type, int64_t quantity, money_t *out_cost);

/* Turn micro-dollars into text like "$12.34", rounded to the nearest cent.
   Writes at most buflen bytes into buf, always ending with '\0'. */
void pricing_format_usd(money_t amount, char *buf, size_t buflen);

#endif /* PRICING_H */
