# Host tools; override CC/AR/CPPFLAGS/CFLAGS/LDFLAGS for a cross build.
CC = cc
AR = ar
PYTHON = python3
SANITIZER_CC ?= clang
ASAN_OPTIONS ?= detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wwrite-strings
BUILD = build
CORE = $(BUILD)/ms2x.o $(BUILD)/sha256.o $(BUILD)/profiles.o
CAN_OFFLINE = $(BUILD)/can_offline.o
SESSION = $(BUILD)/session.o $(BUILD)/can_sim_loop.o $(BUILD)/resource_server.o
FIRMWARE ?= ./051-ms2.bin
RESOURCE_DIR ?= $(dir $(FIRMWARE))

.PHONY: all check sanitize clean cross
all: $(BUILD)/ms2x-offline $(BUILD)/ms2-updater $(BUILD)/libms2x.a $(BUILD)/libms2x.so $(BUILD)/test-core $(BUILD)/test-can-offline
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: %.c include/ms2x.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -fPIC -c $< -o $@
$(BUILD)/can_offline.o: include/ms2x_can_offline.h
$(BUILD)/can_sim_loop.o: include/ms2x_can_sim_loop.h include/ms2x_can_offline.h
$(BUILD)/resource_server.o: include/ms2x_resources.h
$(BUILD)/session.o $(BUILD)/can_sim_loop.o: include/ms2x_session.h include/ms2x_resources.h
$(BUILD)/socketcan.o $(BUILD)/linux_main.o: include/ms2x_linux.h include/ms2x_session.h
$(BUILD)/ms2-updater: $(BUILD)/linux_main.o $(BUILD)/socketcan.o $(SESSION) $(BUILD)/libms2x.a
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
$(BUILD)/libms2x.a: $(CORE) $(CAN_OFFLINE)
	$(AR) rcs $@ $^
$(BUILD)/libms2x.so: $(CORE) $(CAN_OFFLINE)
	$(CC) $(LDFLAGS) -shared $^ -o $@
$(BUILD)/ms2x-offline: $(BUILD)/offline.o $(BUILD)/libms2x.a
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
$(BUILD)/test-core: tests/test_core.c $(BUILD)/libms2x.a include/ms2x.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(BUILD)/libms2x.a $(LDFLAGS) -o $@
$(BUILD)/test-can-offline: tests/test_can_offline.c $(BUILD)/libms2x.a include/ms2x_can_offline.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(BUILD)/libms2x.a $(LDFLAGS) -o $@
$(BUILD)/test-can-sim: tests/test_can_sim_loop.c $(SESSION) $(BUILD)/libms2x.a include/ms2x_can_sim_loop.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(SESSION) $(BUILD)/libms2x.a $(LDFLAGS) -Wl,--wrap=ms2x_tx_processed -o $@
$(BUILD)/test-resources: tests/test_resources.c $(BUILD)/resource_server.o $(BUILD)/libms2x.a include/ms2x_resources.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(BUILD)/resource_server.o $(BUILD)/libms2x.a $(LDFLAGS) -o $@
$(BUILD)/linux-test-main.o: linux_main.c tests/linux_test_entry.h include/ms2x_linux.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Dmain=ms2x_cli_main -include tests/linux_test_entry.h -c $< -o $@
$(BUILD)/test-socketcan: tests/test_socketcan.c $(BUILD)/linux-test-main.o $(BUILD)/socketcan.o $(SESSION) $(BUILD)/libms2x.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $^ $(LDFLAGS) $(foreach call,socket setsockopt bind close if_nametoindex clock_gettime poll write recvmsg,-Wl$(comma)--wrap=$(call)) -o $@
comma := ,
check: all $(BUILD)/test-can-sim $(BUILD)/test-resources $(BUILD)/test-socketcan
	$(BUILD)/test-core "$(FIRMWARE)"
	$(BUILD)/test-can-offline "$(FIRMWARE)"
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=.. MS2X_BUILD="$(abspath $(BUILD))" MS2X_FIRMWARE="$(FIRMWARE)" $(PYTHON) -B -m unittest discover -s tests -p 'test_*.py' -v
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=.. $(PYTHON) -B tests/session_tail.py "$(RESOURCE_DIR)" "$(BUILD)/test-artifacts/session-tail.events"
	$(BUILD)/test-can-sim "$(FIRMWARE)" "$(BUILD)/test-artifacts/normal.events" ../../traces/srseii-v1.8.1a-passive.candump "$(BUILD)/test-artifacts/session-tail.events"
	$(BUILD)/test-resources "$(RESOURCE_DIR)"
	$(BUILD)/test-socketcan "$(FIRMWARE)"
# Native core, both adapters and CLI are instrumented; ctypes uses the ordinary shared library.
# This avoids injecting an ASan runtime into the host Python interpreter.
sanitize: all
	$(MAKE) BUILD=$(BUILD)/sanitize CC=$(SANITIZER_CC) CFLAGS='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-fsanitize=address,undefined' $(BUILD)/sanitize/test-core $(BUILD)/sanitize/test-can-offline $(BUILD)/sanitize/test-can-sim $(BUILD)/sanitize/test-resources $(BUILD)/sanitize/test-socketcan $(BUILD)/sanitize/ms2x-offline
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(BUILD)/sanitize/test-core "$(FIRMWARE)"
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(BUILD)/sanitize/test-can-offline "$(FIRMWARE)"
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=.. MS2X_BUILD="$(abspath $(BUILD))" MS2X_CLI="$(abspath $(BUILD))/sanitize/ms2x-offline" MS2X_FIRMWARE="$(FIRMWARE)" $(PYTHON) -B -m unittest discover -s tests -p 'test_*.py' -v
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=.. $(PYTHON) -B tests/session_tail.py "$(RESOURCE_DIR)" "$(BUILD)/test-artifacts/session-tail.events"
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(BUILD)/sanitize/test-can-sim "$(FIRMWARE)" "$(BUILD)/test-artifacts/normal.events" ../../traces/srseii-v1.8.1a-passive.candump "$(BUILD)/test-artifacts/session-tail.events"
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(BUILD)/sanitize/test-resources "$(RESOURCE_DIR)"
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(BUILD)/sanitize/test-socketcan "$(FIRMWARE)"
clean:
	rm -rf build
# Separate output tree, compiler variables and no target-program execution.
cross:
	$(MAKE) -f Makefile.cross all
# Self-contained profile safety smoke test: no firmware images or CAN bus needed.
.PHONY: check-profiles
$(BUILD)/test-profiles: tests/test_profiles.c $(BUILD)/libms2x.a include/ms2x.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(BUILD)/libms2x.a $(LDFLAGS) -o $@
check-profiles: $(BUILD)/test-profiles
	$(BUILD)/test-profiles

# Offline rejection tests: synthetic buffers only, no real firmware or CAN.
.PHONY: check-firmware-reject
$(BUILD)/test-firmware-reject: tests/test_firmware_reject.c tests/synthetic_image.h $(BUILD)/libms2x.a include/ms2x.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(BUILD)/libms2x.a $(LDFLAGS) -o $@
check-firmware-reject: $(BUILD)/test-firmware-reject
	$(BUILD)/test-firmware-reject

# Self-contained RAM tests; the production CLI is built/read, never executed.
.PHONY: check-discovery check-recovery
check-discovery: $(BUILD)/test-can-sim
	$(BUILD)/test-can-sim --discovery-only
$(BUILD)/recovery-profiles.o: tests/recovery_profiles.c include/ms2x.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@
$(BUILD)/test-recovery: tests/test_recovery.c tests/synthetic_image.h $(BUILD)/recovery-profiles.o $(filter-out $(BUILD)/profiles.o,$(CORE)) $(CAN_OFFLINE) $(SESSION)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(filter-out tests/synthetic_image.h,$^) $(LDFLAGS) -o $@
check-recovery: check-discovery check-profiles check-firmware-reject $(BUILD)/test-recovery $(BUILD)/ms2-updater
	$(BUILD)/test-recovery
	$(PYTHON) -B tests/check_recovery_catalog.py $(BUILD)/ms2-updater $(BUILD)/test-recovery
