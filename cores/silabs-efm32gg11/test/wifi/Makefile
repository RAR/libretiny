# Host unit tests for the portable WiFi logic (station state machine, join
# timer and auto-reconnect decision, WF200 restart policy). Pure C, no
# hardware.
# Run: make -C cores/silabs-efm32gg11/test/wifi
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g
INCLUDES = -I../../base/wifi
TESTS    = test_reconnect test_sta test_recovery

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t && echo "PASS" || exit 1; done

test_reconnect: test_reconnect.c ../../base/wifi/lt_wifi_reconnect.c check.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_reconnect.c ../../base/wifi/lt_wifi_reconnect.c

test_sta: test_sta.c ../../base/wifi/lt_wifi_sta.c check.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_sta.c ../../base/wifi/lt_wifi_sta.c

test_recovery: test_recovery.c ../../base/wifi/lt_wifi_recovery.c ../../base/wifi/lt_wifi_reconnect.c check.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_recovery.c ../../base/wifi/lt_wifi_recovery.c \
		../../base/wifi/lt_wifi_reconnect.c

clean:
	rm -rf $(TESTS) $(addsuffix .dSYM,$(TESTS))
