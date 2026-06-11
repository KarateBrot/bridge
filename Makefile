# betaflight-bridge — convenience wrapper around ESP-IDF's idf.py.
#
#   make                 show this help (the board list is read from boards/)
#   make <board>         build the flash/OTA image for <board>
#   make clean           remove build artefacts
#
# It sources the vendored ESP-IDF (./esp-idf/export.sh) automatically if idf.py
# is not already on PATH, so a plain `make <board>` works after install.sh.

SHELL := /bin/bash

# CMake project() name: the build artefact is build/$(PROJECT).bin and the
# published per-board image is dist/$(PROJECT)-<board>.bin.
PROJECT := betaflight-bridge

# Discover boards from the directory list — never hardcoded. Anything under
# boards/ with an sdkconfig.defaults is a valid target.
BOARDS := $(sort $(notdir $(patsubst %/sdkconfig.defaults,%,$(wildcard boards/*/sdkconfig.defaults))))

# The default board is whatever CMakeLists.txt defaults BOARD to.
DEFAULT_BOARD := $(shell grep 'BOARD "' CMakeLists.txt | head -1 | cut -d'"' -f2)

# Remembers the last board built so a board switch triggers a clean reconfigure
# (flash size / partitions / PSRAM differ between boards).
LAST_BOARD := build/.last_board

.DEFAULT_GOAL := help
.PHONY: help list clean $(BOARDS)

help:
	@echo "betaflight-bridge — ESP32-S3 USB-host-to-WiFi bridge"
	@echo ""
	@echo "Usage:"
	@echo "  make <board>   build the flash/OTA image for <board>"
	@echo "  make clean     remove build artefacts"
	@echo "  make help      show this message"
	@echo ""
	@echo "Available boards:"
	@for b in $(BOARDS); do \
	  if [ "$$b" = "$(DEFAULT_BOARD)" ]; then \
	    printf "  %-28s (default)\n" "$$b"; \
	  else \
	    printf "  %s\n" "$$b"; \
	  fi; \
	done
	@echo ""
	@echo "The build image is written to dist/$(PROJECT)-<board>.bin"

# Bare board names (machine-readable, one per line) for scripting/CI.
list:
	@for b in $(BOARDS); do echo "$$b"; done

$(BOARDS):
	@echo "==> Building $(PROJECT) for board: $@"
	@set -e; \
	if ! command -v idf.py >/dev/null 2>&1; then \
	  if [ ! -f esp-idf/export.sh ]; then \
	    echo "ESP-IDF not found. First-time setup:"; \
	    echo "  git submodule update --init --depth 1 esp-idf"; \
	    echo "  ./esp-idf/install.sh esp32s3"; \
	    exit 1; \
	  fi; \
	  . ./esp-idf/export.sh >/dev/null; \
	fi; \
	if [ "$$(cat $(LAST_BOARD) 2>/dev/null)" != "$@" ]; then \
	  echo "==> Board changed (or first build); reconfiguring"; \
	  rm -f sdkconfig; \
	  idf.py fullclean >/dev/null 2>&1 || true; \
	  idf.py -DBOARD=$@ set-target esp32s3; \
	fi; \
	idf.py -DBOARD=$@ build; \
	mkdir -p dist; \
	cp build/$(PROJECT).bin dist/$(PROJECT)-$@.bin; \
	echo "$@" > $(LAST_BOARD); \
	echo ""; \
	echo "==> Done. Flash/OTA image: dist/$(PROJECT)-$@.bin"; \
	echo "    Serial flash:  idf.py -DBOARD=$@ -p <PORT> flash monitor"; \
	echo "    OTA:           upload dist/$(PROJECT)-$@.bin from the web UI"

clean:
	@if command -v idf.py >/dev/null 2>&1; then \
	  idf.py fullclean >/dev/null 2>&1 || true; \
	elif [ -f esp-idf/export.sh ]; then \
	  ( . ./esp-idf/export.sh >/dev/null && idf.py fullclean >/dev/null 2>&1 ) || true; \
	fi; \
	rm -rf build dist sdkconfig sdkconfig.old
	@echo "==> Cleaned build/, dist/, sdkconfig"
