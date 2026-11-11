# Host tests for the shared OTA code (CRC, metadata, image descriptor,
# bootloader bank selection, application switch/confirm), and a cross-check of
# the builder's descriptor tool against the C code. No hardware.
# Run: make -C cores/silabs-efm32gg11/test/ota

CC	?= cc
OTA	 = ../../base/ota
BOARD	 = ../../../../boards/_base/silabs-efm32gg11.json
FLASH	:= $(shell python3 flash_defs.py $(BOARD))
# -DNDEBUG: the tests use CHECK(), which does not depend on assert()
CFLAGS	?= -std=c11 -Wall -Wextra -Werror -g -O1 -DNDEBUG
CFLAGS	+= -I$(OTA) -I. $(FLASH)
IMAGE_PY = ../../../../builder/utils/efm32gg11_image.py
OTA_SRC	 = $(OTA)/lt_crc32.c $(OTA)/lt_ota_meta.c $(OTA)/lt_ota_image.c $(OTA)/lt_ota_boot.c $(OTA)/lt_ota_app.c host_flash.c
TESTS	 = test_crc32 test_meta test_image test_boot test_app

.PHONY: all clean
all: $(TESTS) test_pyimage
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done
	@echo "== test_pyimage (builder descriptor tool vs. C) =="
	python3 test_pyimage.py ./test_pyimage $(IMAGE_PY)

$(TESTS) test_pyimage: %: %.c test_image.h $(OTA_SRC) $(wildcard $(OTA)/*.h) host_flash.h
	$(CC) $(CFLAGS) -o $@ $< $(OTA_SRC)

clean:
	rm -f $(TESTS) test_pyimage
