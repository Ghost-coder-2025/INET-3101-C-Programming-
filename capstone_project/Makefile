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

INVOICE_OBJS = $(BUILD)/invoice.o $(LEDGER_OBJS) $(PRICING_OBJS)

.PHONY: test_invoice

$(BUILD)/test_invoice: tests/test_invoice.c $(INVOICE_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(INVOICE_OBJS) -o $@ $(LDFLAGS)

test_invoice: $(BUILD)/test_invoice
	./$(BUILD)/test_invoice

PROTOCOL_OBJS = $(BUILD)/protocol.o $(BUILD)/common.o

.PHONY: test_protocol

$(BUILD)/test_protocol: tests/test_protocol.c $(PROTOCOL_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(PROTOCOL_OBJS) -o $@ $(LDFLAGS)

test_protocol: $(BUILD)/test_protocol
	./$(BUILD)/test_protocol

SERVER_OBJS = $(BUILD)/server.o $(BUILD)/common.o $(BUILD)/account.o $(BUILD)/meter.o $(BUILD)/ledger.o $(BUILD)/pricing.o $(BUILD)/invoice.o $(BUILD)/protocol.o

.PHONY: server

$(BUILD)/billing_server: $(SERVER_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o $@ $(LDFLAGS)

server: $(BUILD)/billing_server

ADMIN_OBJS = $(BUILD)/admin.o $(BUILD)/common.o $(BUILD)/account.o $(BUILD)/meter.o

.PHONY: admin

$(BUILD)/billing_admin: $(ADMIN_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $(ADMIN_OBJS) -o $@ $(LDFLAGS)

admin: $(BUILD)/billing_admin

CLIENT_OBJS = $(BUILD)/client.o $(BUILD)/common.o

.PHONY: client

$(BUILD)/billing_client: $(CLIENT_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $(CLIENT_OBJS) -o $@ $(LDFLAGS)

client: $(BUILD)/billing_client
