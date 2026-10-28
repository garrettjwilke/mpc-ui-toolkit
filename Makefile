# C UI toolkit for the MPC Sample. `make` cross-builds the device binaries.
# Objects and host tools land in build/. The binaries to load are in bin/.
# zig cc is only the cross-compiler.
WARN = -Wall -Wextra -std=c11
HOSTCC = cc
CFLAGS = -D_DEFAULT_SOURCE -g $(WARN) -I src
# Apple clang 17 deadlocks in ASan startup on macOS 26. Use Homebrew clang when it is installed.
TESTCC = $(HOSTCC)
ifeq ($(shell $(HOSTCC) --version 2>/dev/null | awk 'NR==1 { print $$1 $$2 }'),Appleclang)
  ifneq ($(wildcard /opt/homebrew/opt/llvm/bin/clang),)
    TESTCC = /opt/homebrew/opt/llvm/bin/clang
  endif
endif

BUILD = build
BIN = bin
HOST = $(BUILD)/host
TEST = $(BUILD)/test
DEV = $(BUILD)/device
ZIGCC = zig cc -target aarch64-linux-musl
ZIGCFLAGS = -O2 -D_DEFAULT_SOURCE $(WARN) -I src

EXAMPLES = clear boxes buttons highlight soft_keys knobs list pads anim sudoku text_edit diagnostics filebrowser
BINS = clear boxes buttons highlight soft-keys knobs list pads anim sudoku text-edit diagnostics filebrowser

.PHONY: all test preview clean

all: $(addprefix $(BIN)/,$(BINS))

test: $(BUILD)/ui-test
	./$(BUILD)/ui-test

preview: $(BUILD)/preview | $(BUILD)/shots
	./$(BUILD)/preview

# Host preview objects. Example objects are built without main.
$(HOST)/src/%.o: src/%.c | $(HOST)/src
	$(HOSTCC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(HOST)/examples/%.o: examples/%.c | $(HOST)/examples
	$(HOSTCC) $(CFLAGS) -DUI_NO_MAIN -MMD -MP -c -o $@ $<

HOST_CORE = $(HOST)/src/ui.o $(HOST)/src/widgets.o $(HOST)/src/filebrowser.o $(HOST)/src/input.o
HOST_EX = $(addprefix $(HOST)/examples/,$(EXAMPLES:=.o))

$(BUILD)/preview: $(HOST)/src/preview.o $(HOST_CORE) $(HOST_EX)
	$(HOSTCC) $(CFLAGS) -o $@ $^

# Sanitized objects, separate from the preview objects.
$(TEST)/src/%.o: src/%.c | $(TEST)/src
	$(TESTCC) $(CFLAGS) -fsanitize=address,undefined -MMD -MP -c -o $@ $<

$(TEST)/examples/%.o: examples/%.c | $(TEST)/examples
	$(TESTCC) $(CFLAGS) -fsanitize=address,undefined -DUI_NO_MAIN -MMD -MP -c -o $@ $<

TEST_CORE = $(TEST)/src/ui.o $(TEST)/src/widgets.o $(TEST)/src/filebrowser.o $(TEST)/src/input.o $(TEST)/src/test.o
TEST_EX = $(addprefix $(TEST)/examples/,$(EXAMPLES:=.o))

$(BUILD)/ui-test: $(TEST_CORE) $(TEST_EX)
	$(TESTCC) $(CFLAGS) -fsanitize=address,undefined -o $@ $^

# Device objects. One static binary per example, linked into bin/.
$(DEV)/src/%.o: src/%.c | $(DEV)/src
	$(ZIGCC) $(ZIGCFLAGS) -MMD -MP -c -o $@ $<

$(DEV)/examples/%.o: examples/%.c | $(DEV)/examples
	$(ZIGCC) $(ZIGCFLAGS) -MMD -MP -c -o $@ $<

DEV_COMMON = $(DEV)/src/app.o $(DEV)/src/screen.o $(DEV)/src/surface.o \
             $(DEV)/src/ui.o $(DEV)/src/widgets.o $(DEV)/src/filebrowser.o $(DEV)/src/input.o

define device
$(BIN)/$1: $(DEV_COMMON) $(DEV)/examples/$2.o | $(BIN)
	$(ZIGCC) -static -O2 -s -o $$@ $$^
endef

$(eval $(call device,clear,clear))
$(eval $(call device,boxes,boxes))
$(eval $(call device,buttons,buttons))
$(eval $(call device,highlight,highlight))
$(eval $(call device,soft-keys,soft_keys))
$(eval $(call device,knobs,knobs))
$(eval $(call device,list,list))
$(eval $(call device,pads,pads))
$(eval $(call device,anim,anim))
$(eval $(call device,sudoku,sudoku))
$(eval $(call device,text-edit,text_edit))
$(eval $(call device,diagnostics,diagnostics))
$(eval $(call device,filebrowser,filebrowser))

$(HOST)/src $(HOST)/examples $(TEST)/src $(TEST)/examples $(DEV)/src $(DEV)/examples $(BIN) $(BUILD)/shots:
	mkdir -p $@

-include $(wildcard $(HOST)/src/*.d $(HOST)/examples/*.d $(TEST)/src/*.d $(TEST)/examples/*.d $(DEV)/src/*.d $(DEV)/examples/*.d)

clean:
	rm -rf $(BUILD) $(BIN) \
	       ui-test ui-test.dSYM preview-bin preview-bin.dSYM shots \
	       clear boxes buttons highlight soft-keys knobs list pads anim sudoku text-edit diagnostics *.dSYM
