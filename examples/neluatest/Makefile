BUILD_DIR=build
SOURCE_DIR=.
include $(N64_INST)/include/n64.mk

all: neluatest.z64

OBJS = $(BUILD_DIR)/neluatest.o

$(BUILD_DIR)/neluatest.elf: $(OBJS)

neluatest.z64: N64_ROM_TITLE="C++ test"

clean:
	rm -rf $(BUILD_DIR) neluatest.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
