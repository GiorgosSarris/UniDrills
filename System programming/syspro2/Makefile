CC = cc
CFLAGS = -Wall -Wextra -std=c11 -g -O2
LDFLAGS = -pthread

TARGETS = jms_coord jms_console

all: $(TARGETS)

$(TARGETS): %: %.o
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

%.o: %.c jms_common.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS) *.o
	rm -rf outputs_*

.PHONY: all clean
