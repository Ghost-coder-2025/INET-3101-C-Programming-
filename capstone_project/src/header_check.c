#include "account.h"
#include "common.h"
#include "invoice.h"
#include "ledger.h"
#include "meter.h"
#include "pricing.h"
#include "protocol.h"

/* Compile-time checks: the build fails if a struct has hidden padding */
_Static_assert(sizeof(account_t) == 80, "account_t should be exactly 80 bytes");
_Static_assert(sizeof(meter_t) == 16, "meter_t should be exactly 16 bytes");
