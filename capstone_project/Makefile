CC      = clang
CFLAGS  = -std=gnu11 -Wall -Wextra -Werror -g -Iinclude -MMD -MP
LDFLAGS = -pthread
BUILD   = build

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

.PHONY: all clean

all: $(OBJS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)

TEST_OBJS = $(BUILD)/common.o $(BUILD)/account.o $(BUILD)/meter.o

.PHONY: test

$(BUILD)/test_stores: tests/test_stores.c $(TEST_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(TEST_OBJS) -o $@ $(LDFLAGS)

test: $(BUILD)/test_stores
	./$(BUILD)/test_stores

LEDGER_OBJS = $(BUILD)/common.o $(BUILD)/ledger.o

.PHONY: test_ledger

$(BUILD)/test_ledger: tests/test_ledger.c $(LEDGER_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(LEDGER_OBJS) -o $@ $(LDFLAGS)

test_ledger: $(BUILD)/test_ledger
	./$(BUILD)/test_ledger

PRICING_OBJS = $(BUILD)/pricing.o

.PHONY: test_pricing

$(BUILD)/test_pricing: tests/test_pricing.c $(PRICING_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(PRICING_OBJS) -o $@ $(LDFLAGS)

test_pricing: $(BUILD)/test_pricing
	./$(BUILD)/test_pricing
