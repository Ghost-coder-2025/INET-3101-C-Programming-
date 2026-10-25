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
