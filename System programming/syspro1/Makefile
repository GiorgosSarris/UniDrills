CC = cc
CFLAGS = -Wall -Wextra -std=c11 -g -O2

TARGETS = jms_coord jms_console jms_pool

all: $(TARGETS)

$(TARGETS): %: %.o
	$(CC) $(CFLAGS) $< -o $@
%.o: %.c jms_common.h
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -f $(TARGETS) *.o
	rm -f jms_in jms_out
.PHONY: all clean
