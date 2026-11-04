BUILD_DIR := build-debug
PRESET    := debug

.PHONY: all lab1 lab2 configure clean

all: configure
	cmake --build $(BUILD_DIR)

configure: $(BUILD_DIR)/build.ninja

$(BUILD_DIR)/build.ninja:
	cmake --preset $(PRESET)

lab1 lab2: configure
	cmake --build $(BUILD_DIR) --target $@
	./$(BUILD_DIR)/$@

clean:
	cmake --build $(BUILD_DIR) --target clean
